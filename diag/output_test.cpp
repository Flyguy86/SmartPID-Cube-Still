// ═══════════════════════════════════════════════════════════════════════════════
//  SmartPID CUBE — Full GPIO Pin Scanner
//  Cycles through ALL safe SAMD21 GPIO pins one at a time.
//  Watch for SSR LED, relay click, buzzer, etc. and report back.
//
//  On boot: automatically starts scanning every pin HIGH for 3 seconds.
//  Serial commands (115200):
//    s       Start/restart full scan
//    0-29    Set specific pin index HIGH (all others LOW)
//    o       All pins LOW
//    ?       Show which pin is currently active
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// All Arduino Zero pin numbers that are safe to toggle as OUTPUT.
// Excludes: PA24/PA25 (USB), PA30/PA31 (SWD), PA03 (AREF)
static const int SAFE_PINS[] = {
     0,  // PA11 (Serial1 RX)
     1,  // PA10 (Serial1 TX)
     2,  // PA14 (Buzzer)
     3,  // PA09 (DS18B20 lower)
     4,  // PA08 (DS18B20 upper)
     5,  // PA15
     6,  // PA20 (Relay1?)
     7,  // PA21 (Relay2?)
     8,  // PA06 (Btn3)
     9,  // PA07 (Btn4)
    10,  // PA18 (TC3 PWM)
    11,  // PA16
    12,  // PA19
    13,  // PA17
    14,  // PA02 (A0/Btn1)
    15,  // PB08 (A1)
    16,  // PB09 (A2/NTC)
    17,  // PA04 (A3/Btn2)
    18,  // PA05 (A4)
    19,  // PB02 (A5)
    20,  // PA22 (SDA)
    21,  // PA23 (SCL)
    22,  // PA12 (SSR?)
    23,  // PB10 (MOSI)
    24,  // PB11 (SCK)
    25,  // PB03 (RX_LED)
    26,  // PA27 (DC1?)
    27,  // PA28 (DC2?)
    30,  // PB22 (ESP TX)
    31,  // PB23 (ESP RX)
};

static const char* PIN_NAMES[] = {
    "D0  PA11 (RX1)",
    "D1  PA10 (TX1)",
    "D2  PA14 (BUZZER)",
    "D3  PA09 (DS_LO)",
    "D4  PA08 (DS_UP)",
    "D5  PA15",
    "D6  PA20 (RL1?)",
    "D7  PA21 (RL2?)",
    "D8  PA06 (BTN3)",
    "D9  PA07 (BTN4)",
    "D10 PA18 (PWM)",
    "D11 PA16",
    "D12 PA19",
    "D13 PA17",
    "A0  PA02 (BTN1)",
    "A1  PB08",
    "A2  PB09 (NTC)",
    "A3  PA04 (BTN2)",
    "A4  PA05",
    "A5  PB02",
    "D20 PA22 (SDA)",
    "D21 PA23 (SCL)",
    "D22 PA12 (SSR?)",
    "D23 PB10 (MOSI)",
    "D24 PB11 (SCK)",
    "D25 PB03 (RXLED)",
    "D26 PA27 (DC1?)",
    "D27 PA28 (DC2?)",
    "D30 PB22 (ESP_TX)",
    "D31 PB23 (ESP_RX)",
};

static const int NUM_SAFE = sizeof(SAFE_PINS) / sizeof(SAFE_PINS[0]);
static int activePin = -1;
static bool scanning = false;
static int scanIdx = 0;

static char cmdBuf[32];
static int cmdPos = 0;

static void allOff() {
    for (int i = 0; i < NUM_SAFE; i++) {
        digitalWrite(SAFE_PINS[i], LOW);
    }
    analogWrite(10, 0);
    activePin = -1;
}

static void setPin(int idx) {
    allOff();
    activePin = idx;
    digitalWrite(SAFE_PINS[idx], HIGH);
    // Also try analogWrite for PWM-capable pins
    if (SAFE_PINS[idx] == 10) {
        analogWrite(10, 255);
    }
    SerialUSB.print(F(">>> PIN ON: ["));
    SerialUSB.print(idx);
    SerialUSB.print(F("] Arduino D"));
    SerialUSB.print(SAFE_PINS[idx]);
    SerialUSB.print(F(" - "));
    SerialUSB.println(PIN_NAMES[idx]);
}

static void doScan() {
    if (scanIdx >= NUM_SAFE) {
        scanning = false;
        allOff();
        SerialUSB.println(F("\n=== SCAN COMPLETE - All pins tested ==="));
        SerialUSB.println(F("Type 's' to scan again, or a number 0-29 to test one pin"));
        return;
    }
    setPin(scanIdx);
    scanIdx++;
}

static void processCommand(const char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (cmd[0] == 's' || cmd[0] == 'S') {
        scanning = true;
        scanIdx = 0;
        SerialUSB.println(F("\n=== STARTING FULL PIN SCAN ==="));
        SerialUSB.println(F("Each pin goes HIGH for 3 seconds. Watch for SSR/relay/buzzer."));
        SerialUSB.println(F("Press any key to skip to next pin.\n"));
        doScan();
    }
    else if (cmd[0] == 'o' || cmd[0] == 'O') {
        scanning = false;
        allOff();
        SerialUSB.println(F("All OFF"));
    }
    else if (cmd[0] == '?') {
        if (activePin >= 0) {
            SerialUSB.print(F("Active: ["));
            SerialUSB.print(activePin);
            SerialUSB.print(F("] "));
            SerialUSB.println(PIN_NAMES[activePin]);
        } else {
            SerialUSB.println(F("No pin active"));
        }
    }
    else if (cmd[0] >= '0' && cmd[0] <= '9') {
        int idx = atoi(cmd);
        if (idx >= 0 && idx < NUM_SAFE) {
            scanning = false;
            setPin(idx);
        } else {
            SerialUSB.print(F("Invalid index. Range: 0-"));
            SerialUSB.println(NUM_SAFE - 1);
        }
    }
    else {
        SerialUSB.println(F("Commands: s=scan  o=allOff  ?=status  0-29=setPin"));
    }
}

void setup() {
    SerialUSB.begin(115200);
    unsigned long start = millis();
    while (!SerialUSB && millis() - start < 5000) delay(10);
    delay(300);

    SerialUSB.println(F("\n============================================="));
    SerialUSB.println(F("  SmartPID CUBE - Full GPIO Pin Scanner"));
    SerialUSB.println(F("=============================================\n"));

    // Init ALL safe pins as OUTPUT LOW
    for (int i = 0; i < NUM_SAFE; i++) {
        pinMode(SAFE_PINS[i], OUTPUT);
        digitalWrite(SAFE_PINS[i], LOW);
    }

    SerialUSB.print(NUM_SAFE);
    SerialUSB.println(F(" GPIO pins configured as OUTPUT.\n"));

    SerialUSB.println(F("Pin index table:"));
    for (int i = 0; i < NUM_SAFE; i++) {
        SerialUSB.print(F("  ["));
        if (i < 10) SerialUSB.print(' ');
        SerialUSB.print(i);
        SerialUSB.print(F("] "));
        SerialUSB.println(PIN_NAMES[i]);
    }

    SerialUSB.println(F("\nCommands:"));
    SerialUSB.println(F("  s       Start auto-scan (3s per pin)"));
    SerialUSB.println(F("  0-29    Manually set one pin HIGH"));
    SerialUSB.println(F("  o       All OFF"));
    SerialUSB.println(F("  ?       Show active pin\n"));

    // Auto-start scan
    SerialUSB.println(F("=== AUTO-STARTING SCAN in 2 seconds ===\n"));
    delay(2000);
    scanning = true;
    scanIdx = 0;
    doScan();
}

static unsigned long lastScanTime = 0;

void loop() {
    // Handle serial input
    while (SerialUSB.available()) {
        char c = SerialUSB.read();
        if (scanning) {
            // Any keypress skips to next pin during scan
            doScan();
            lastScanTime = millis();
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (cmdPos > 0) {
                cmdBuf[cmdPos] = '\0';
                processCommand(cmdBuf);
                cmdPos = 0;
            }
        } else if (cmdPos < (int)sizeof(cmdBuf) - 1) {
            cmdBuf[cmdPos++] = c;
        }
    }

    // Auto-advance scan every 3 seconds
    if (scanning && millis() - lastScanTime >= 3000) {
        lastScanTime = millis();
        doScan();
    }
}
