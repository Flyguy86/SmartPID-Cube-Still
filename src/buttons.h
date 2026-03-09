#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  Button Input — Debounced polling with short/long press detection
//  4 buttons: UP, DOWN, SELECT, START/STOP
//  All configured INPUT_PULLUP, active LOW
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// Button events
enum ButtonEvent : uint8_t {
    BTN_NONE = 0,
    BTN_UP_PRESS,
    BTN_DOWN_PRESS,
    BTN_SELECT_PRESS,
    BTN_SS_PRESS,          // Start/Stop short press
    BTN_SS_LONG,           // Start/Stop long press (emergency stop)
    BTN_SELECT_LONG        // Select long press
};

void initButtons();
ButtonEvent pollButtons();   // Call from scheduler — returns one event per call
