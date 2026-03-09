#include "controller.h"
#include "config.h"
#include "temperature.h"
#include "pid_autotune.h"
#include <PID_v1.h>
#include <Preferences.h>

// ─── Global State ───────────────────────────────────────────────────────────
SystemState gState;

// ─── PID Controllers ────────────────────────────────────────────────────────
static PID* pid[2];
static PIDAutotuner autotuner[2];
static unsigned long windowStartTime[2] = {0, 0};

static Preferences prefs;

// ─── Helpers ────────────────────────────────────────────────────────────────
static int getOutputPin(OutputID id) {
    if (id < NUM_OUTPUTS) return OUTPUT_PINS[id];
    return -1;
}

static void setOutputLow(int channel) {
    int pin = getOutputPin(gState.ch[channel].assignedOutput);
    if (pin >= 0) digitalWrite(pin, LOW);
}

void controller_init() {
    // --- Init ALL output pins LOW ---
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        pinMode(OUTPUT_PINS[i], OUTPUT);
        digitalWrite(OUTPUT_PINS[i], LOW);
    }

    // --- Load saved config or defaults ---
    controller_loadConfig();

    // --- Create PID objects ---
    for (int i = 0; i < 2; i++) {
        pid[i] = new PID(
            &gState.ch[i].temperature,
            &gState.ch[i].pidOutput,
            &gState.ch[i].setpoint,
            gState.ch[i].params.Kp,
            gState.ch[i].params.Ki,
            gState.ch[i].params.Kd,
            DIRECT
        );
        pid[i]->SetOutputLimits(PID_OUTPUT_MIN, PID_OUTPUT_MAX);
        pid[i]->SetMode(AUTOMATIC);
        pid[i]->SetSampleTime(SENSOR_READ_INTERVAL_MS);

        windowStartTime[i] = millis();

        // Configure autotuners from runtime config
        autotuner[i].setOutputRange(PID_OUTPUT_MIN, PID_OUTPUT_MAX);
        autotuner[i].setOutputStep(gState.ch[i].atCfg.outputStep);
        autotuner[i].setNoiseBand(gState.ch[i].atCfg.noiseBand);
        autotuner[i].setLookbackSec(gState.ch[i].atCfg.lookbackSec);
        autotuner[i].setMaxCycles(gState.ch[i].atCfg.maxCycles);
    }

    Serial.println("[CTRL] Controller initialized (SmartPID CUBE)");
    for (int i = 0; i < 2; i++) {
        const char* modeStr = (gState.ch[i].mode == MODE_PID) ? "PID" : "ON/OFF";
        OutputID oid = gState.ch[i].assignedOutput;
        const char* outName = (oid < NUM_OUTPUTS) ? OUTPUT_NAMES[oid] : "None";
        Serial.printf("[CTRL] Ch%d: SP=%.1f Kp=%.2f Ki=%.2f Kd=%.2f Mode=%s Out=%s\n",
            i, gState.ch[i].setpoint, gState.ch[i].params.Kp,
            gState.ch[i].params.Ki, gState.ch[i].params.Kd, modeStr, outName);
    }
}

void controller_update() {
    unsigned long now = millis();
    gState.uptime = now / 1000;

    for (int i = 0; i < 2; i++) {
        // Read temperature
        gState.ch[i].temperature = temperature_read(i);
        int pin = getOutputPin(gState.ch[i].assignedOutput);

        // Safety: sensor error -> turn off output
        if (isnan(gState.ch[i].temperature)) {
            if (pin >= 0) digitalWrite(pin, LOW);
            gState.ch[i].pidOutput = 0;
            continue;
        }

        // Disabled -> turn off output
        if (!gState.ch[i].enabled) {
            if (pin >= 0) digitalWrite(pin, LOW);
            gState.ch[i].pidOutput = 0;
            continue;
        }

        // No output assigned -> compute but don't drive
        if (pin < 0) {
            gState.ch[i].pidOutput = 0;
            continue;
        }

        // ─── Autotune mode (always PID-style relay test) ───
        if (gState.ch[i].autotuning) {
            double output = autotuner[i].update(gState.ch[i].temperature);
            gState.ch[i].pidOutput = output;

            if (autotuner[i].isComplete()) {
                gState.ch[i].params.Kp = autotuner[i].getKp();
                gState.ch[i].params.Ki = autotuner[i].getKi();
                gState.ch[i].params.Kd = autotuner[i].getKd();
                pid[i]->SetTunings(gState.ch[i].params.Kp, gState.ch[i].params.Ki, gState.ch[i].params.Kd);
                gState.ch[i].autotuning = false;
                gState.ch[i].mode = MODE_PID; // Switch to PID after autotune
                controller_saveConfig();
                Serial.printf("[CTRL] Ch%d autotune COMPLETE: Kp=%.2f Ki=%.2f Kd=%.2f\n",
                    i, autotuner[i].getKp(), autotuner[i].getKi(), autotuner[i].getKd());
            } else if (autotuner[i].getState() == PIDAutotuner::FAILED) {
                gState.ch[i].autotuning = false;
                Serial.printf("[CTRL] Ch%d autotune FAILED\n", i);
            }
        }
        // ─── ON/OFF with hysteresis ───
        else if (gState.ch[i].mode == MODE_ONOFF) {
            double temp = gState.ch[i].temperature;
            double sp = gState.ch[i].setpoint;
            double hyst = gState.ch[i].hysteresis;

            if (temp < sp - hyst) {
                gState.ch[i].pidOutput = PID_OUTPUT_MAX; // Full ON
            } else if (temp > sp + hyst) {
                gState.ch[i].pidOutput = PID_OUTPUT_MIN; // Full OFF
            }
            // else: maintain current state (inside deadband)

            // Direct ON/OFF for relays & DC outputs
            if (gState.ch[i].pidOutput > PID_OUTPUT_MAX / 2) {
                digitalWrite(pin, HIGH);
            } else {
                digitalWrite(pin, LOW);
            }
            continue; // Skip time-proportional section
        }
        // ─── Normal PID mode ───
        else {
            pid[i]->Compute();
        }

        // ─── Time-proportional output (PID + autotune modes) ───
        unsigned long ssrWin = gState.ch[i].ssrWindowMs;
        if (now - windowStartTime[i] >= ssrWin) {
            windowStartTime[i] = now;
        }

        unsigned long onTime = (unsigned long)(gState.ch[i].pidOutput * ssrWin / PID_OUTPUT_MAX);
        if (now - windowStartTime[i] < onTime) {
            digitalWrite(pin, HIGH);
        } else {
            digitalWrite(pin, LOW);
        }
    }
}

void controller_startAutotune(int channel) {
    if (channel < 0 || channel > 1) return;
    autotuner[channel].setOutputStep(gState.ch[channel].atCfg.outputStep);
    autotuner[channel].setNoiseBand(gState.ch[channel].atCfg.noiseBand);
    autotuner[channel].setLookbackSec(gState.ch[channel].atCfg.lookbackSec);
    autotuner[channel].setMaxCycles(gState.ch[channel].atCfg.maxCycles);
    autotuner[channel].setTargetSetpoint(gState.ch[channel].setpoint);
    autotuner[channel].start();
    gState.ch[channel].autotuning = true;
    gState.ch[channel].enabled = true;
    Serial.printf("[CTRL] Ch%d autotune STARTED at setpoint %.1f\n",
        channel, gState.ch[channel].setpoint);
}

void controller_cancelAutotune(int channel) {
    if (channel < 0 || channel > 1) return;
    autotuner[channel].cancel();
    gState.ch[channel].autotuning = false;
    setOutputLow(channel);
    gState.ch[channel].pidOutput = 0;
    Serial.printf("[CTRL] Ch%d autotune CANCELLED\n", channel);
}

void controller_setEnabled(int channel, bool enabled) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].enabled = enabled;
    if (!enabled) {
        setOutputLow(channel);
        gState.ch[channel].pidOutput = 0;
    }
}

void controller_setSetpoint(int channel, double setpoint) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].setpoint = constrain(setpoint, 0, 200);
}

void controller_setPIDParams(int channel, double kp, double ki, double kd) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].params.Kp = kp;
    gState.ch[channel].params.Ki = ki;
    gState.ch[channel].params.Kd = kd;
    pid[channel]->SetTunings(kp, ki, kd);
}

void controller_setAutotuneConfig(int channel, double outputStep, double noiseBand, int lookbackSec, int maxCycles) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].atCfg.outputStep  = constrain(outputStep, 1.0, 255.0);
    gState.ch[channel].atCfg.noiseBand   = constrain(noiseBand, 0.1, 10.0);
    gState.ch[channel].atCfg.lookbackSec = constrain(lookbackSec, 5, 300);
    gState.ch[channel].atCfg.maxCycles   = constrain(maxCycles, 3, 50);
}

void controller_setSSRWindow(int channel, unsigned long windowMs) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].ssrWindowMs = constrain(windowMs, 1000UL, 30000UL);
}

void controller_setMode(int channel, ControlMode mode) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].mode = mode;
}

void controller_setHysteresis(int channel, double hysteresis) {
    if (channel < 0 || channel > 1) return;
    gState.ch[channel].hysteresis = constrain(hysteresis, 0.1, 20.0);
}

void controller_setOutput(int channel, OutputID output) {
    if (channel < 0 || channel > 1) return;
    // Turn off old output first
    setOutputLow(channel);
    gState.ch[channel].assignedOutput = output;
}

void controller_saveConfig() {
    prefs.begin(NVS_NAMESPACE, false);

    for (int i = 0; i < 2; i++) {
        String prefix = "ch" + String(i) + "_";
        prefs.putDouble((prefix + "sp").c_str(),      gState.ch[i].setpoint);
        prefs.putDouble((prefix + "kp").c_str(),      gState.ch[i].params.Kp);
        prefs.putDouble((prefix + "ki").c_str(),      gState.ch[i].params.Ki);
        prefs.putDouble((prefix + "kd").c_str(),      gState.ch[i].params.Kd);
        prefs.putBool((prefix + "en").c_str(),        gState.ch[i].enabled);
        prefs.putUChar((prefix + "mode").c_str(),     (uint8_t)gState.ch[i].mode);
        prefs.putDouble((prefix + "hyst").c_str(),    gState.ch[i].hysteresis);
        prefs.putUChar((prefix + "outid").c_str(),    (uint8_t)gState.ch[i].assignedOutput);
        prefs.putDouble((prefix + "at_step").c_str(), gState.ch[i].atCfg.outputStep);
        prefs.putDouble((prefix + "at_nb").c_str(),   gState.ch[i].atCfg.noiseBand);
        prefs.putInt((prefix + "at_lb").c_str(),      gState.ch[i].atCfg.lookbackSec);
        prefs.putInt((prefix + "at_mc").c_str(),      gState.ch[i].atCfg.maxCycles);
        prefs.putULong((prefix + "ssrw").c_str(),     gState.ch[i].ssrWindowMs);
    }

    prefs.end();
    Serial.println("[CTRL] Config saved to NVS");
}

void controller_loadConfig() {
    prefs.begin(NVS_NAMESPACE, true);

    for (int i = 0; i < 2; i++) {
        String prefix = "ch" + String(i) + "_";
        double defaultSp = (i == 0) ? DEFAULT_SETPOINT_1 : DEFAULT_SETPOINT_2;

        gState.ch[i].setpoint        = prefs.getDouble((prefix + "sp").c_str(), defaultSp);
        gState.ch[i].params.Kp       = prefs.getDouble((prefix + "kp").c_str(), PID_DEFAULT_KP);
        gState.ch[i].params.Ki       = prefs.getDouble((prefix + "ki").c_str(), PID_DEFAULT_KI);
        gState.ch[i].params.Kd       = prefs.getDouble((prefix + "kd").c_str(), PID_DEFAULT_KD);
        gState.ch[i].enabled         = prefs.getBool((prefix + "en").c_str(), false);
        gState.ch[i].mode            = (ControlMode)prefs.getUChar((prefix + "mode").c_str(), MODE_PID);
        gState.ch[i].hysteresis      = prefs.getDouble((prefix + "hyst").c_str(), DEFAULT_HYSTERESIS);
        gState.ch[i].assignedOutput  = (OutputID)prefs.getUChar((prefix + "outid").c_str(),
                                         i == 0 ? OUT_SSR : OUT_NONE);
        gState.ch[i].atCfg.outputStep  = prefs.getDouble((prefix + "at_step").c_str(), AUTOTUNE_OUTPUT_STEP);
        gState.ch[i].atCfg.noiseBand   = prefs.getDouble((prefix + "at_nb").c_str(),   AUTOTUNE_NOISE_BAND);
        gState.ch[i].atCfg.lookbackSec = prefs.getInt((prefix + "at_lb").c_str(),      AUTOTUNE_LOOKBACK_SEC);
        gState.ch[i].atCfg.maxCycles   = prefs.getInt((prefix + "at_mc").c_str(),      AUTOTUNE_MAX_CYCLES);
        gState.ch[i].ssrWindowMs       = prefs.getULong((prefix + "ssrw").c_str(),     SSR_WINDOW_SIZE);
        gState.ch[i].autotuning    = false;
        gState.ch[i].temperature   = NAN;
        gState.ch[i].pidOutput     = 0;
    }

    gState.sdCardPresent = false;
    gState.sdLogging = false;

    prefs.end();
    Serial.println("[CTRL] Config loaded from NVS");
}
