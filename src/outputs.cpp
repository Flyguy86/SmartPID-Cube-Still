#include "outputs.h"
#include "sensors.h"
#include "storage.h"
#include "display.h"
#include "runlog.h"

// ─── Output state ───────────────────────────────────────────────────────────
static bool outputStates[NUM_OUTPUTS] = { false };
static int  ssrPWMDuty = 0;

// ─── PID state ──────────────────────────────────────────────────────────────
static float    pidIntegral[MAX_ASSIGNMENTS_PER_STEP];
static float    pidLastInput[MAX_ASSIGNMENTS_PER_STEP];
static bool     pidFirstRun[MAX_ASSIGNMENTS_PER_STEP];
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
    // SSR hardware is on the PWM pin (PA18), not the digital pin (PA12)
    if (index == OUT_SSR) {
        setSSRPWM(state ? 255 : 0);
    }
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
    for (int i = 0; i < MAX_ASSIGNMENTS_PER_STEP; i++) {
        pidIntegral[i]  = 0;
        pidLastInput[i] = 0;
        pidFirstRun[i]  = true;
    }
    lastPIDTime  = millis();
}

void startProfile() {
    if (isAutoTuning()) return;  // Don't start profile during auto-tune
    Settings& s = getSettings();
    RunProfile& prof = s.profiles[s.activeProfile];
    if (prof.numSteps == 0) return;
    currentStep = 0;
    initPID();
    runState = RUN_HEATING;
    holdDurationMs = (unsigned long)prof.steps[0].holdMinutes * 60UL * 1000UL;

    logRunStart(prof.numSteps);
    float firstTarget = prof.steps[0].numAssignments > 0 ?
        prof.steps[0].assignments[0].targetTemp : 0;
    logStepStart(0, firstTarget);

    SerialUSB.print(F("Profile START: step 1/"));
    SerialUSB.print(prof.numSteps);
    SerialUSB.print(F(" assignments="));
    SerialUSB.print(prof.steps[0].numAssignments);
    SerialUSB.print(prof.steps[0].coolMode ? F(" COOL") : F(" HEAT"));
    if (prof.steps[0].numAssignments > 0) {
        SerialUSB.print(F(" target="));
        SerialUSB.print(firstTarget);
        SerialUSB.print(F("F"));
    }
    SerialUSB.println();
}

void stopProfile() {
    if (runState != RUN_IDLE && runState != RUN_DONE) {
        logRunStop();
    }
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
    Settings& s = getSettings();
    return s.profiles[s.activeProfile].numSteps;
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
    RunProfile& prof = s.profiles[s.activeProfile];
    ProfileStep& step = prof.steps[currentStep];

    bool allTargetsMet = (step.numAssignments > 0);

    for (int a = 0; a < step.numAssignments; a++) {
        SensorAssignment& sa = step.assignments[a];
        int sensorIdx = sa.sensorIndex;

        // Safety: if sensor disconnected, shut down this output
        if (!isSensorConnected(sensorIdx)) {
            driveOutput(sa.outputIndex, 0);
            allTargetsMet = false;
            continue;
        }

        float currentTemp = getSensorTemp(sensorIdx);
        float target      = sa.targetTemp;
        PIDParams& pid    = s.sensorCfg[sensorIdx].pid;

        // Error depends on cool/heat mode
        float error;
        if (step.coolMode) {
            error = currentTemp - target;  // positive when too hot → drive cooling
        } else {
            error = target - currentTemp;  // positive when too cold → drive heating
        }

        // ── Proportional ──
        float P = pid.Kp * error;

        // ── Integral with anti-windup ──
        pidIntegral[a] += error * dt;
        float iLimit = (pid.Ki > 0.001f) ? ((float)sa.maxPWM / pid.Ki) : 1000.0f;
        pidIntegral[a] = constrain(pidIntegral[a], -iLimit, iLimit);
        float I = pid.Ki * pidIntegral[a];

        // ── Derivative on measurement (avoids setpoint kick) ──
        float D = 0;
        if (!pidFirstRun[a]) {
            float dInput = (currentTemp - pidLastInput[a]) / dt;
            D = step.coolMode ? pid.Kd * dInput : -pid.Kd * dInput;
        }
        pidFirstRun[a]  = false;
        pidLastInput[a] = currentTemp;

        // ── Output ──
        float output = P + I + D;
        int pwm = constrain((int)output, 0, (int)sa.maxPWM);
        driveOutput(sa.outputIndex, pwm);

        // ── Check if this assignment has reached target ──
        const float hysteresis = 2.0f;  // °F
        if (step.coolMode) {
            if (currentTemp > target + hysteresis) allTargetsMet = false;
        } else {
            if (currentTemp < target - hysteresis) allTargetsMet = false;
        }
    }

    // ── State transitions ──
    if (runState == RUN_HEATING) {
        if (allTargetsMet) {
            runState    = RUN_HOLDING;
            holdStartMs = now;
            holdDurationMs = (unsigned long)step.holdMinutes * 60UL * 1000UL;
            float curTemp = step.numAssignments > 0 ?
                getSensorTemp(step.assignments[0].sensorIndex) : 0;
            logTargetHit(currentStep, curTemp);
            SerialUSB.print(F("Step ")); SerialUSB.print(currentStep + 1);
            SerialUSB.println(F(": all targets reached, HOLDING"));
            tone(PIN_BUZZER, 2000, 200);
        }
    } else if (runState == RUN_HOLDING) {
        if (now - holdStartMs >= holdDurationMs) {
            float curTemp = step.numAssignments > 0 ?
                getSensorTemp(step.assignments[0].sensorIndex) : 0;
            logHoldDone(currentStep, curTemp);
            // Check if there are more steps
            if (currentStep < prof.numSteps - 1) {
                allOutputsOff();
                currentStep++;
                initPID();
                runState = RUN_HEATING;
                holdDurationMs = (unsigned long)prof.steps[currentStep].holdMinutes * 60UL * 1000UL;
                float nextTarget = prof.steps[currentStep].numAssignments > 0 ?
                    prof.steps[currentStep].assignments[0].targetTemp : 0;
                logStepStart(currentStep, nextTarget);
                SerialUSB.print(F("Advancing to step "));
                SerialUSB.print(currentStep + 1);
                SerialUSB.print(F("/"));
                SerialUSB.println(prof.numSteps);
                tone(PIN_BUZZER, 2000, 100);
                delay(150);
                tone(PIN_BUZZER, 2500, 100);
            } else {
                logRunDone();
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
    RunProfile& prof = s.profiles[s.activeProfile];
    int basePWM = (prof.numSteps > 0 && prof.steps[0].numAssignments > 0) ?
        prof.steps[0].assignments[0].maxPWM : 128;
    atDuty       = basePWM / 2;
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
