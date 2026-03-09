#include "outputs.h"
#include "sensors.h"
#include "storage.h"
#include "display.h"

// ─── Output state ───────────────────────────────────────────────────────────
static bool outputStates[NUM_OUTPUTS] = { false };
static int  ssrPWMDuty = 0;

// ─── PID state ──────────────────────────────────────────────────────────────
static float    pidIntegral  = 0;
static float    pidLastInput = 0;
static bool     pidFirstRun  = true;
static RunState runState     = RUN_IDLE;
static int      currentStep  = 0;
static unsigned long holdStartMs  = 0;
static unsigned long holdDurationMs = 0;
static unsigned long lastPIDTime  = 0;

// ─── Auto-Tune state ────────────────────────────────────────────────────────
static int8_t  atSensor    = -1;   // -1 = inactive
static float   atSetpoint  = 0;
static int     atDuty      = 0;    // bang-bang PWM amplitude
static bool    atAbove     = false;
static unsigned long atCrossings[12];
static float   atPeaks[12];
static int     atCrossCount = 0;
static unsigned long atStartMs = 0;

// ─── Output Control ─────────────────────────────────────────────────────────

void initOutputs() {
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        pinMode(OUTPUT_PINS[i], OUTPUT);
        digitalWrite(OUTPUT_PINS[i], LOW);
    }
    pinMode(PIN_SSR_PWM, OUTPUT);
    analogWrite(PIN_SSR_PWM, 0);
    SerialUSB.println(F("Outputs: all OFF"));
}

void setOutput(int index, bool state) {
    if (index < 0 || index >= NUM_OUTPUTS) return;
    outputStates[index] = state;
    digitalWrite(OUTPUT_PINS[index], state ? HIGH : LOW);
}

bool getOutput(int index) {
    if (index < 0 || index >= NUM_OUTPUTS) return false;
    return outputStates[index];
}

void setSSRPWM(int duty) {
    ssrPWMDuty = constrain(duty, 0, 255);
    analogWrite(PIN_SSR_PWM, ssrPWMDuty);
}

int getSSRPWM() {
    return ssrPWMDuty;
}

void allOutputsOff() {
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        setOutput(i, false);
    }
    setSSRPWM(0);
}

// ─── PID Controller ─────────────────────────────────────────────────────────

void initPID() {
    pidIntegral  = 0;
    pidLastInput = 0;
    pidFirstRun  = true;
    lastPIDTime  = millis();
}

void startProfile() {
    if (isAutoTuning()) return;  // Don't start profile during auto-tune
    Settings& s = getSettings();
    if (s.profile.numSteps == 0) return;
    currentStep = 0;
    initPID();
    runState = RUN_HEATING;
    holdDurationMs = (unsigned long)s.profile.steps[0].holdMinutes * 60UL * 1000UL;
    SerialUSB.print(F("Profile START: step 1/"));
    SerialUSB.print(s.profile.numSteps);
    SerialUSB.print(F(" target="));
    SerialUSB.print(s.profile.steps[0].targetTemp);
    SerialUSB.print(F("F hold="));
    SerialUSB.print(s.profile.steps[0].holdMinutes);
    SerialUSB.print(F("min sensor="));
    SerialUSB.print(s.profile.steps[0].sensorIndex);
    SerialUSB.print(F(" maxPWM="));
    SerialUSB.println(s.profile.steps[0].maxPWM);
}

void stopProfile() {
    runState = RUN_IDLE;
    setSSRPWM(0);
    allOutputsOff();
    SerialUSB.println(F("Profile STOPPED"));
}

RunState getRunState() {
    return runState;
}

int getCurrentStep() {
    return currentStep;
}

int getTotalSteps() {
    return getSettings().profile.numSteps;
}

unsigned long getHoldRemaining() {
    if (runState != RUN_HOLDING) return 0;
    unsigned long elapsed = millis() - holdStartMs;
    if (elapsed >= holdDurationMs) return 0;
    return (holdDurationMs - elapsed) / 1000UL;
}

// Drive the specified output
static void driveOutput(int outIdx, int pwm) {
    if (outIdx == OUT_SSR) {
        // SSR has dedicated PWM pin
        setSSRPWM(pwm);
    } else {
        // Digital outputs: on if pwm > 0
        setOutput(outIdx, pwm > 0);
        // Also mirror to SSR PWM for monitoring
        setSSRPWM(pwm);
    }
}

void updatePID() {
    if (runState == RUN_IDLE || runState == RUN_DONE) return;
    if (isAutoTuning()) return;  // Auto-tune has control
    if (isEmergencyStopped()) return;  // ESTOP kills everything

    unsigned long now = millis();
    if (now - lastPIDTime < PID_COMPUTE_MS) return;
    float dt = (now - lastPIDTime) / 1000.0f;
    lastPIDTime = now;

    Settings& s = getSettings();
    ProfileStep& step = s.profile.steps[currentStep];
    int sensorIdx = step.sensorIndex;

    // Safety: if sensor disconnected, shut down
    if (!isSensorConnected(sensorIdx)) {
        setSSRPWM(0);
        return;
    }

    float currentTemp = getSensorTemp(sensorIdx);
    float target      = step.targetTemp;
    PIDParams& pid    = s.sensorCfg[sensorIdx].pid;

    float error = target - currentTemp;

    // ── Proportional ──
    float P = pid.Kp * error;

    // ── Integral with anti-windup ──
    pidIntegral += error * dt;
    float iLimit = (pid.Ki > 0.001f) ? ((float)step.maxPWM / pid.Ki) : 1000.0f;
    pidIntegral = constrain(pidIntegral, -iLimit, iLimit);
    float I = pid.Ki * pidIntegral;

    // ── Derivative on measurement (avoids setpoint kick) ──
    float D = 0;
    if (!pidFirstRun) {
        float dInput = (currentTemp - pidLastInput) / dt;
        D = -pid.Kd * dInput;
    }
    pidFirstRun  = false;
    pidLastInput = currentTemp;

    // ── Output ──
    float output = P + I + D;
    int pwm = constrain((int)output, 0, (int)step.maxPWM);
    driveOutput(step.outputIndex, pwm);

    // ── State transitions ──
    const float hysteresis = 2.0f;  // °F

    if (runState == RUN_HEATING) {
        if (currentTemp >= target - hysteresis) {
            runState    = RUN_HOLDING;
            holdStartMs = now;
            holdDurationMs = (unsigned long)step.holdMinutes * 60UL * 1000UL;
            SerialUSB.print(F("Step ")); SerialUSB.print(currentStep + 1);
            SerialUSB.println(F(": target reached, HOLDING"));
            tone(PIN_BUZZER, 2000, 200);
        }
    } else if (runState == RUN_HOLDING) {
        if (now - holdStartMs >= holdDurationMs) {
            // Check if there are more steps
            if (currentStep < s.profile.numSteps - 1) {
                currentStep++;
                pidIntegral = 0;
                pidFirstRun = true;
                runState = RUN_HEATING;
                holdDurationMs = (unsigned long)s.profile.steps[currentStep].holdMinutes * 60UL * 1000UL;
                SerialUSB.print(F("Advancing to step "));
                SerialUSB.print(currentStep + 1);
                SerialUSB.print(F("/"));
                SerialUSB.print(s.profile.numSteps);
                SerialUSB.print(F(" target="));
                SerialUSB.print(s.profile.steps[currentStep].targetTemp);
                SerialUSB.println(F("F"));
                tone(PIN_BUZZER, 2000, 100);
                delay(150);
                tone(PIN_BUZZER, 2500, 100);
            } else {
                runState = RUN_DONE;
                setSSRPWM(0);
                allOutputsOff();
                SerialUSB.println(F("Profile: all steps complete, DONE"));
                for (int i = 0; i < 5; i++) {
                    tone(PIN_BUZZER, 2500, 300);
                    delay(500);
                }
            }
        }
    }
}

// ─── PID Auto-Tune (relay-based Ziegler-Nichols) ────────────────────────────

void startAutoTune(int sensorIdx) {
    if (sensorIdx < 0 || sensorIdx > 1) return;
    if (!isSensorConnected(sensorIdx)) return;
    if (runState != RUN_IDLE && runState != RUN_DONE) return;

    Settings& s = getSettings();
    atSensor     = sensorIdx;
    atSetpoint   = getSensorTemp(sensorIdx);  // Tune around current temp
    atDuty       = s.profile.steps[0].maxPWM / 2;
    if (atDuty < 30) atDuty = 30;
    atAbove      = false;
    atCrossCount = 0;
    atStartMs    = millis();

    SerialUSB.print(F("[AT] Start sensor "));
    SerialUSB.print(sensorIdx);
    SerialUSB.print(F(" setpoint="));
    SerialUSB.print(atSetpoint, 1);
    SerialUSB.print(F(" duty="));
    SerialUSB.println(atDuty);

    // Start heating
    driveOutput(sensorIdx, atDuty);
}

void stopAutoTune() {
    if (atSensor >= 0) {
        SerialUSB.println(F("[AT] Stopped"));
    }
    atSensor = -1;
    setSSRPWM(0);
    allOutputsOff();
}

bool isAutoTuning() {
    return atSensor >= 0;
}

int getAutoTuneSensor() {
    return atSensor;
}

void updateAutoTune() {
    if (atSensor < 0) return;

    float temp = getSensorTemp(atSensor);
    if (temp < -900.0f) { stopAutoTune(); return; }

    bool nowAbove = (temp >= atSetpoint);

    // Detect zero crossing
    if (nowAbove != atAbove && atCrossCount < 12) {
        atCrossings[atCrossCount] = millis();
        atPeaks[atCrossCount] = temp;
        atCrossCount++;
        atAbove = nowAbove;
    }

    // Bang-bang relay control
    if (nowAbove) {
        driveOutput(atSensor, 0);
    } else {
        driveOutput(atSensor, atDuty);
    }

    // Need 8 crossings (4 full cycles) for reliable estimate
    if (atCrossCount >= 8) {
        // Average period from crossing pairs
        float totalPeriod = 0;
        int nPeriods = 0;
        for (int i = 2; i < atCrossCount; i += 2) {
            totalPeriod += (float)(atCrossings[i] - atCrossings[i-2]) / 1000.0f;
            nPeriods++;
        }
        float Tu = (nPeriods > 0) ? totalPeriod / nPeriods : 10.0f;

        // Amplitude from peaks
        float sumHigh = 0, sumLow = 0;
        int nHigh = 0, nLow = 0;
        for (int i = 1; i < atCrossCount; i++) {
            if (atPeaks[i] > atSetpoint) { sumHigh += atPeaks[i]; nHigh++; }
            else { sumLow += atPeaks[i]; nLow++; }
        }
        float a = 0;
        if (nHigh > 0 && nLow > 0) {
            a = ((sumHigh / nHigh) - (sumLow / nLow)) / 2.0f;
        }

        if (Tu > 1.0f && a > 0.1f) {
            float Ku = (4.0f * (float)atDuty) / (3.14159f * a);

            // Ziegler-Nichols "no overshoot" variant
            float Kp = 0.33f * Ku;
            float Ki = 2.0f * Kp / Tu;
            float Kd = Kp * Tu / 3.0f;

            Settings& s = getSettings();
            s.sensorCfg[atSensor].pid = {Kp, Ki, Kd};
            saveSettings();

            SerialUSB.print(F("[AT] Done! Tu="));
            SerialUSB.print(Tu, 1);
            SerialUSB.print(F("s a="));
            SerialUSB.print(a, 2);
            SerialUSB.print(F(" Kp="));
            SerialUSB.print(Kp, 2);
            SerialUSB.print(F(" Ki="));
            SerialUSB.print(Ki, 3);
            SerialUSB.print(F(" Kd="));
            SerialUSB.println(Kd, 2);

            tone(PIN_BUZZER, 2000, 200);
            delay(300);
            tone(PIN_BUZZER, 2500, 200);
        } else {
            SerialUSB.println(F("[AT] Failed: insufficient oscillation data"));
        }

        stopAutoTune();
        return;
    }

    // Timeout after 30 minutes
    if (millis() - atStartMs > 1800000UL) {
        SerialUSB.println(F("[AT] Timeout (30 min)"));
        stopAutoTune();
    }
}
