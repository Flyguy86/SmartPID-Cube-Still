#ifndef DISPLAY_H
#define DISPLAY_H

/// Initialize the 1.3" OLED display via I2C
void display_init();

/// Update the display (call from loop, handles its own refresh rate)
void display_update();

/// Show a temporary message overlay (e.g. "Saved!", "Autotune started")
void display_showMessage(const char* msg, unsigned long durationMs = 2000);

/// Show a specific screen
enum DisplayScreen {
    SCREEN_MAIN = 0,    // Both channels: temp, SP, output bars
    SCREEN_CH0_DETAIL,  // Ch0 detail: PID params, mode, output
    SCREEN_CH1_DETAIL,  // Ch1 detail
    SCREEN_CONFIG,      // System config: WiFi, SD, uptime
    SCREEN_COUNT
};

void display_setScreen(DisplayScreen screen);
DisplayScreen display_getScreen();

#endif // DISPLAY_H
