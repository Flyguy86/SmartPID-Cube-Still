#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  GPIO Pin Scanner — Shared pin toggle/scan logic for debug
//  Used by: OLED menu, HTTP API, serial commands
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// All safe Arduino pin numbers on the SAMD21 (avoids SWD, USB, crystal)
#define NUM_SCAN_PINS 30
extern const int SCAN_PINS[NUM_SCAN_PINS];

// Port name lookup for display (e.g. "PA12")
const char* pinPortName(int arduinoPin);

// Set a single pin HIGH (turns off previous pin first)
void scanSetPin(int arduinoPin);
void scanSetPinState(int arduinoPin, bool high);

// Turn off all scanned pins
void scanAllOff();

// Get currently active pin (-1 = none)
int  scanGetActivePin();

// Auto-scan: start cycling all pins, 3s each
void scanAutoStart();
void scanAutoStop();
bool scanAutoRunning();
void scanAutoTick();   // Call from loop — advances to next pin when timer expires
int  scanAutoIndex();  // Current index in SCAN_PINS[]
