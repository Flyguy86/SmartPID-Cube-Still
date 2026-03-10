// ═══════════════════════════════════════════════════════════════════════════════
//  SmartPID CUBE — Beer/Spirits Distillation Controller
//  Hardware: ATSAMD21G18 (main) + ESP-WROOM-02 (WiFi co-processor)
//  Web interface served via ESP8266 AT commands on port 80
//  Default AP: "SmartPID-Still" / password "distill1"
//
//  Task Priorities:
//    CRITICAL — updateSensors + updatePID (safety, never starved)
//    HIGH     — wifiPoll (HTTP serving)
//    NORMAL   — updateAutoTune
//    LOW      — debugPrint, schedulerPrintStats
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "outputs.h"
#include "wifi_server.h"
#include "scheduler.h"
#include "buttons.h"
#include "display.h"
#include "runlog.h"
#include "pinscan.h"

// ─── Button + Display task wrapper ──────────────────────────────────────────
// Polls buttons and feeds events to display/menu handler.
// Also enforces emergency stop — if ESTOP is active, outputs stay off.

static void updateUI() {
    ButtonEvent evt = pollButtons();
    handleButton(evt);

    // Safety enforcement: if ESTOP is active, ensure outputs stay killed
    if (isEmergencyStopped()) {
        allOutputsOff();
    }
}

// ─── Debug serial print (every 5 s) ────────────────────────────────────────
// Note: interval is now managed by the scheduler, so no internal gating needed

static void debugPrint() {
    SerialUSB.print(F("[temp] lower="));
    if (isSensorConnected(0))
        SerialUSB.print(getSensorTemp(0), 1);
    else
        SerialUSB.print(F("N/C"));

    SerialUSB.print(F(" upper="));
    if (isSensorConnected(1))
        SerialUSB.print(getSensorTemp(1), 1);
    else
        SerialUSB.print(F("N/C"));

    RunState rs = getRunState();
    if (rs != RUN_IDLE) {
        const char* states[] = { "IDLE", "HEAT", "HOLD", "DONE" };
        SerialUSB.print(F("  run="));
        SerialUSB.print(states[rs]);
        SerialUSB.print(F(" PWM="));
        SerialUSB.print(getSSRPWM());
        if (rs == RUN_HOLDING) {
            SerialUSB.print(F(" rem="));
            SerialUSB.print(getHoldRemaining());
            SerialUSB.print(F("s"));
        }
    }
    SerialUSB.println();
}

// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    SerialUSB.begin(115200);

    // Wait up to 3 s for USB serial console
    unsigned long start = millis();
    while (!SerialUSB && millis() - start < 3000) delay(10);
    delay(300);

    SerialUSB.println();
    SerialUSB.println(F("=== SmartPID Still Controller v1.1 ==="));
    SerialUSB.println(F("    ATSAMD21G18 + ESP8266 WiFi"));
    SerialUSB.println();

    // Load persisted settings from flash
    loadSettings();
    Settings& s = getSettings();
    SerialUSB.print(F("WiFi: "));
    SerialUSB.println(s.wifi.configured ? s.wifi.ssid : "(not configured)");
    RunProfile& prof = s.profiles[s.activeProfile];
    SerialUSB.print(F("Profile "));
    SerialUSB.print(s.activeProfile);
    SerialUSB.print(F(" \""));
    SerialUSB.print(prof.name);
    SerialUSB.print(F("\": "));
    SerialUSB.print(prof.numSteps);
    SerialUSB.print(F(" steps"));
    if (prof.numSteps > 0 && prof.steps[0].numAssignments > 0) {
        SerialUSB.print(F(", step1: target="));
        SerialUSB.print(prof.steps[0].assignments[0].targetTemp, 1);
        SerialUSB.print(F("F maxPWM="));
        SerialUSB.print(prof.steps[0].assignments[0].maxPWM);
    }
    SerialUSB.println();
    SerialUSB.println();

    // Initialize subsystems
    initSensors();
    initOutputs();
    initPID();
    initButtons();
    initDisplay();          // Show splash immediately
    initRunLog();
    initWiFi();

    // ── Register tasks with priority scheduler ──────────────────────────────
    schedulerInit();

    // CRITICAL (Priority 0) — Safety: SSR toggle + sensors + PID + UI/ESTOP
    // These also run via yieldCritical() inside blocking WiFi waits
    schedulerAddTask(updateSSRPWM,   PRIORITY_CRITICAL, 0,    0, "SSR");  // first!
    schedulerAddTask(updateSensors,  PRIORITY_CRITICAL, 0,    0, "Sensors");
    schedulerAddTask(updatePID,      PRIORITY_CRITICAL, 0,    0, "PID");
    schedulerAddTask(updateUI,       PRIORITY_CRITICAL, 20,   0, "Buttons");

    // HIGH (Priority 1) — WiFi HTTP serving + display refresh
    // Time-budgeted: processes up to 128 bytes per call, yields back
    schedulerAddTask(wifiPoll,       PRIORITY_HIGH,     0,    0, "WiFi");
    schedulerAddTask(updateDisplay,  PRIORITY_HIGH,     250,  0, "Display");

    // NORMAL (Priority 2) — Auto-tune runs only when active
    schedulerAddTask(updateAutoTune, PRIORITY_NORMAL,   0,    0, "AutoTune");

    // LOW (Priority 3) — Debug output + scheduler diagnostics + sensor hot-plug
    schedulerAddTask(debugPrint,     PRIORITY_LOW,      5000, 0, "Debug");
    schedulerAddTask(schedulerPrintStats, PRIORITY_LOW, 30000, 0, "Stats");
    schedulerAddTask(rescanSensors,  PRIORITY_LOW,      5000, 0, "Rescan");
    schedulerAddTask(wifiCheckConnection, PRIORITY_LOW,  30000, 0, "WiFiChk");
    schedulerAddTask(checkTempLog,   PRIORITY_LOW,      2000, 0, "TempLog");

    SerialUSB.println(F("[sched] 12 tasks registered"));

    // Ready beep
    tone(PIN_BUZZER, 2000, 100);
    delay(150);
    tone(PIN_BUZZER, 2500, 100);

    SerialUSB.println();
    SerialUSB.println(F("=== Ready ==="));
    if (isWiFiReady()) {
        SerialUSB.println(F("Connect to open WiFi 'SmartPID-Still' (no password)"));
        SerialUSB.println(F("Open http://192.168.4.1/ in browser"));
    }
    SerialUSB.println();
}

// ─── Main Loop ──────────────────────────────────────────────────────────────
//
// All work is done by the scheduler in priority order:
//   1. CRITICAL: updateSensors → updatePID  (every cycle)
//   2. HIGH:     wifiPoll                    (every cycle, time-budgeted)
//   3. NORMAL:   updateAutoTune              (every cycle, self-gated)
//   4. LOW:      debugPrint (5s), stats (30s)
//
// Critical tasks also run inside blocking WiFi AT waits via yieldCritical()

// ─── Serial Pin Scanner (debug) ────────────────────────────────────────────
// Commands via SerialUSB:
//   pin N       Set Arduino pin N HIGH (previous pin turned off)
//   off         All scanned pins off
//   scan        Auto-cycle all safe pins, 3s each
//   pwm N V     analogWrite pin N with value V
static char serialCmd[32];
static int serialPos = 0;

static void processSerialCmd(const char* cmd) {
    if (strncmp(cmd, "pin ", 4) == 0) {
        int p = atoi(cmd + 4);
        scanAutoStop();
        scanSetPinState(p, true);
        SerialUSB.print(F("OK pin "));
        SerialUSB.print(p);
        SerialUSB.print(F(" "));
        SerialUSB.print(pinPortName(p));
        SerialUSB.println(F(" HIGH"));
    }
    else if (strncmp(cmd, "pinlo ", 6) == 0) {
        int p = atoi(cmd + 6);
        scanAutoStop();
        scanSetPinState(p, false);
        SerialUSB.print(F("OK pin "));
        SerialUSB.print(p);
        SerialUSB.print(F(" "));
        SerialUSB.print(pinPortName(p));
        SerialUSB.println(F(" LOW"));
    }
    else if (strncmp(cmd, "pwm ", 4) == 0) {
        // "pwm 10 255"
        int p = atoi(cmd + 4);
        const char* sp = strchr(cmd + 4, ' ');
        int v = sp ? atoi(sp + 1) : 255;
        pinMode(p, OUTPUT);
        analogWrite(p, v);
        SerialUSB.print(F("OK pwm "));
        SerialUSB.print(p);
        SerialUSB.print(F(" "));
        SerialUSB.println(v);
    }
    else if (strcmp(cmd, "off") == 0) {
        scanAllOff();
        SerialUSB.println(F("OK off"));
    }
    else if (strcmp(cmd, "scan") == 0) {
        scanAutoStart();
        SerialUSB.println(F("OK scan"));
    }
    else if (strcmp(cmd, "status") == 0) {
        SerialUSB.println(F("OK v1.2"));
    }
    else {
        SerialUSB.println(F("Commands: pin N | pinlo N | off | scan | pwm N V | status"));
    }
}

static void checkSerialCommands() {
    while (SerialUSB.available()) {
        char c = SerialUSB.read();
        SerialUSB.print(F("[rx:"));
        SerialUSB.print((int)c);
        SerialUSB.print(F("]"));
        if (c == '\n' || c == '\r') {
            if (serialPos > 0) {
                serialCmd[serialPos] = '\0';
                SerialUSB.print(F("[cmd:'"));
                SerialUSB.print(serialCmd);
                SerialUSB.println(F("']"));
                processSerialCmd(serialCmd);
                serialPos = 0;
            }
        } else if (serialPos < (int)sizeof(serialCmd) - 1) {
            serialCmd[serialPos++] = c;
        }
    }
    // Auto-scan advance
    scanAutoTick();
}

void loop() {
    schedulerRun();
    checkSerialCommands();}