#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  OLED Display — SSD1306 128×64 over I2C
//  Screens: Dashboard, WiFi status, Menu, Emergency Stop overlay
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "buttons.h"

// Display screens
enum Screen : uint8_t {
    SCREEN_SPLASH = 0,     // Boot splash (shown briefly)
    SCREEN_DASHBOARD,      // Main: temps + step + state
    SCREEN_MENU,           // Menu with Up/Down/Select navigation
    SCREEN_ESTOP,          // Emergency stop overlay
    SCREEN_WIFI_INFO       // WiFi details
};

void initDisplay();
void updateDisplay();       // Refresh at ~4 FPS (self-timed)
void handleButton(ButtonEvent evt);  // Process button input for menu/estop

// Emergency stop — can be triggered by button or safety code
void triggerEmergencyStop();
bool isEmergencyStopped();
void clearEmergencyStop();   // Reset ESTOP state (requires button confirm)
