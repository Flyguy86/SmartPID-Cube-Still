#include "buttons.h"
#include "config.h"
#include <Arduino.h>

static const int btnPins[4] = { BTN_UP_PIN, BTN_DOWN_PIN, BTN_SELECT_PIN, BTN_BACK_PIN };
static bool lastState[4] = { true, true, true, true }; // Pull-up: idle HIGH
static unsigned long pressTime[4] = {0};
static unsigned long lastDebounce[4] = {0};
static bool longFired[4] = {false};

#define LONG_PRESS_MS 1000

void buttons_init() {
    for (int i = 0; i < 4; i++) {
        // GPIO 34, 35 are input-only (no internal pull-up) — use external pull-up
        // GPIO 32, 33 support internal pull-up
        if (btnPins[i] == BTN_UP_PIN || btnPins[i] == BTN_DOWN_PIN) {
            pinMode(btnPins[i], INPUT_PULLUP);
        } else {
            pinMode(btnPins[i], INPUT); // 34, 35: external pull-up required
        }
        lastState[i] = digitalRead(btnPins[i]);
    }
    Serial.println("[BTN] Buttons initialized");
}

ButtonEvent buttons_poll() {
    unsigned long now = millis();

    for (int i = 0; i < 4; i++) {
        bool reading = digitalRead(btnPins[i]);

        if (reading != lastState[i]) {
            lastDebounce[i] = now;
        }

        if ((now - lastDebounce[i]) > BTN_DEBOUNCE_MS) {
            bool pressed = !reading; // Active LOW

            if (pressed && lastState[i]) {
                // Just pressed
                pressTime[i] = now;
                longFired[i] = false;
            }

            if (pressed && !longFired[i] && (now - pressTime[i] > LONG_PRESS_MS)) {
                longFired[i] = true;
                if (i == 2) { // Select button long press
                    lastState[i] = reading;
                    return BTN_SELECT_LONG;
                }
            }

            if (!pressed && !lastState[i] && !longFired[i]) {
                // Just released (short press)
                lastState[i] = reading;
                switch (i) {
                    case 0: return BTN_UP_PRESS;
                    case 1: return BTN_DOWN_PRESS;
                    case 2: return BTN_SELECT_PRESS;
                    case 3: return BTN_BACK_PRESS;
                }
            }
        }

        lastState[i] = reading;
    }

    return BTN_NONE;
}
