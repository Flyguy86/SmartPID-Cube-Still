// ═══════════════════════════════════════════════════════════════════════════════
//  Button Input — Debounced polling, short + long press
//  Active LOW (INPUT_PULLUP), 50ms debounce, 1.5s long press
// ═══════════════════════════════════════════════════════════════════════════════

#include "buttons.h"
#include "config.h"

#define NUM_BUTTONS   4
#define DEBOUNCE_MS   50
#define LONG_PRESS_MS 1500

static const int btnPins[NUM_BUTTONS] = {
    PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_SELECT, PIN_BTN_SS
};

static bool     lastStable[NUM_BUTTONS];
static bool     lastReading[NUM_BUTTONS];
static unsigned long debounceTime[NUM_BUTTONS];
static unsigned long pressStart[NUM_BUTTONS];
static bool     longFired[NUM_BUTTONS];

void initButtons() {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(btnPins[i], INPUT_PULLUP);
        lastStable[i]  = true;   // Pull-up: idle HIGH
        lastReading[i] = true;
        debounceTime[i] = 0;
        pressStart[i]   = 0;
        longFired[i]    = false;
    }
    SerialUSB.println(F("[btn] Buttons initialized"));
}

ButtonEvent pollButtons() {
    unsigned long now = millis();

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool reading = digitalRead(btnPins[i]);

        // Reset debounce timer on change
        if (reading != lastReading[i]) {
            debounceTime[i] = now;
        }
        lastReading[i] = reading;

        // Only act after stable for DEBOUNCE_MS
        if ((now - debounceTime[i]) < DEBOUNCE_MS) continue;

        bool pressed = !reading;  // Active LOW

        // Detect press edge
        if (pressed && lastStable[i]) {
            // Just pressed (stable was HIGH, now LOW)
            pressStart[i] = now;
            longFired[i]  = false;
        }

        // Detect long press while held
        if (pressed && !longFired[i] && (now - pressStart[i] >= LONG_PRESS_MS)) {
            longFired[i] = true;
            lastStable[i] = !reading;
            // Long press events
            if (i == 3) return BTN_SS_LONG;       // S/S long = emergency stop
            if (i == 2) return BTN_SELECT_LONG;    // Select long
        }

        // Detect release (short press — only if long didn't fire)
        if (!pressed && !lastStable[i] && !longFired[i]) {
            lastStable[i] = true;
            switch (i) {
                case 0: return BTN_UP_PRESS;
                case 1: return BTN_DOWN_PRESS;
                case 2: return BTN_SELECT_PRESS;
                case 3: return BTN_SS_PRESS;
            }
        }

        lastStable[i] = !pressed;
    }

    return BTN_NONE;
}
