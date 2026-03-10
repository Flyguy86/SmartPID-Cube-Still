// ═══════════════════════════════════════════════════════════════════════════════
//  GPIO Pin Scanner — Shared implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "pinscan.h"

const int SCAN_PINS[NUM_SCAN_PINS] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,
    14,15,16,17,18,19,20,21,22,23,24,25,26,27,30,31
};

static int  activePin    = -1;
static bool autoRunning  = false;
static int  autoIdx      = 0;
static unsigned long autoTime = 0;

// ─── Port name strings ──────────────────────────────────────────────────────
// Maps Arduino pin number → SAM D21 port pin name for display

const char* pinPortName(int p) {
    switch (p) {
        case  0: return "PA11";
        case  1: return "PA10";
        case  2: return "PA14";
        case  3: return "PA09";
        case  4: return "PA08";
        case  5: return "PA15";
        case  6: return "PA20";
        case  7: return "PA21";
        case  8: return "PA06";
        case  9: return "PA07";
        case 10: return "PA18";
        case 11: return "PA16";
        case 12: return "PA19";
        case 13: return "PA17";
        case 14: return "PA02";
        case 15: return "PB08";
        case 16: return "PB09";
        case 17: return "PA04";
        case 18: return "PA05";
        case 19: return "PB02";
        case 20: return "PA22";
        case 21: return "PA23";
        case 22: return "PA12";
        case 23: return "PB10";
        case 24: return "PB11";
        case 25: return "PA13";
        case 26: return "PA27";
        case 27: return "PA28";
        case 30: return "PA24";
        case 31: return "PA25";
        default: return "???";
    }
}

// ─── Pin control ────────────────────────────────────────────────────────────

void scanSetPin(int arduinoPin) {
    scanSetPinState(arduinoPin, true);
}

void scanSetPinState(int arduinoPin, bool high) {
    // Turn off previous
    if (activePin >= 0) {
        digitalWrite(activePin, LOW);
        if (activePin == 10) analogWrite(10, 0);
    }
    activePin = arduinoPin;
    if (arduinoPin >= 0) {
        pinMode(arduinoPin, OUTPUT);
        digitalWrite(arduinoPin, high ? HIGH : LOW);
        if (arduinoPin == 10) analogWrite(10, high ? 255 : 0);
        SerialUSB.print(F("[scan] Pin D"));
        SerialUSB.print(arduinoPin);
        SerialUSB.print(F(" ("));
        SerialUSB.print(pinPortName(arduinoPin));
        SerialUSB.print(high ? F(") → HIGH") : F(") → LOW"));
        SerialUSB.println();
    }
}

void scanAllOff() {
    if (activePin >= 0) {
        digitalWrite(activePin, LOW);
        if (activePin == 10) analogWrite(10, 0);
    }
    activePin = -1;
    autoRunning = false;
    SerialUSB.println(F("[scan] All off"));
}

int scanGetActivePin() {
    return activePin;
}

// ─── Auto-scan ──────────────────────────────────────────────────────────────

void scanAutoStart() {
    autoRunning = true;
    autoIdx = 0;
    autoTime = millis();
    SerialUSB.println(F("[scan] Auto-scan started, 3s each..."));
    scanSetPin(SCAN_PINS[0]);
}

void scanAutoStop() {
    autoRunning = false;
    scanAllOff();
}

bool scanAutoRunning() {
    return autoRunning;
}

void scanAutoTick() {
    if (!autoRunning) return;
    if (millis() - autoTime < 3000) return;

    autoIdx++;
    if (autoIdx >= NUM_SCAN_PINS) {
        scanAllOff();
        SerialUSB.println(F("[scan] === SCAN COMPLETE ==="));
    } else {
        autoTime = millis();
        scanSetPin(SCAN_PINS[autoIdx]);
    }
}

int scanAutoIndex() {
    return autoIdx;
}
