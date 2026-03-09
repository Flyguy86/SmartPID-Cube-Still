// ═══════════════════════════════════════════════════════════════════════════════
//  OLED Display — SSD1306 128×64 I2C
//  Dashboard, WiFi status, menu navigation, emergency stop overlay
//  Uses Adafruit SSD1306 + GFX libraries
// ═══════════════════════════════════════════════════════════════════════════════

#include "display.h"
#include "config.h"
#include "sensors.h"
#include "outputs.h"
#include "storage.h"
#include "wifi_server.h"
#include "runlog.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_W 128
#define SCREEN_H  64
#define REFRESH_MS 250   // 4 FPS max

static Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);

static Screen currentScreen = SCREEN_SPLASH;
static unsigned long lastRefresh = 0;
static unsigned long splashEnd   = 0;
static bool eStopped = false;

// ─── Menu State ─────────────────────────────────────────────────────────────

enum MenuItem : uint8_t {
    MENU_BACK = 0,
    MENU_START_STOP,
    MENU_WIFI_INFO,
    MENU_AUTOTUNE_LOWER,
    MENU_AUTOTUNE_UPPER,
    MENU_OUTPUTS,
    MENU_COUNT
};

static const char* const menuLabels[MENU_COUNT] = {
    "<< Dashboard",
    "Start / Stop",
    "WiFi Info",
    "AutoTune Lower",
    "AutoTune Upper",
    "All Outputs Off"
};

static int menuSel = 0;
static int menuScroll = 0;
#define MENU_VISIBLE 5   // Lines visible on screen

// ─── Splash Screen ─────────────────────────────────────────────────────────

static void drawSplash() {
    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(16, 4);
    oled.print(F("SmartPID"));
    oled.setTextSize(1);
    oled.setCursor(20, 26);
    oled.print(F("Still Controller"));
    oled.setCursor(46, 42);
    oled.print(F("v1.0"));
    oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);
}

// ─── Dashboard Screen ───────────────────────────────────────────────────────

static void drawDashboard() {
    Settings& s = getSettings();
    RunState rs = getRunState();

    // ── Row 1: WiFi status bar (top 10px) ──
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    if (isWiFiReady()) {
        if (isWiFiSTA()) {
            // Connected to home network as client
            // WiFi icon: 3 arcs
            oled.drawPixel(3, 6, SSD1306_WHITE);
            oled.drawCircle(3, 8, 2, SSD1306_WHITE);
            oled.drawCircle(3, 8, 5, SSD1306_WHITE);
            oled.setCursor(12, 0);
            oled.print(s.wifi.ssid);
        } else {
            // AP mode
            oled.print(F("AP:SmartPID-Still"));
        }
    } else {
        oled.print(F("WiFi: OFF"));
    }
    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // ── Row 2: Temperature readings (big) ──
    oled.setTextSize(1);
    oled.setCursor(0, 12);
    oled.print(F("Lo:"));
    if (isSensorConnected(0)) {
        oled.setTextSize(2);
        oled.setCursor(18, 11);
        oled.print(getSensorTemp(0), 1);
        oled.setTextSize(1);
        oled.print(F("F"));
    } else {
        oled.setTextSize(2);
        oled.setCursor(18, 11);
        oled.print(F("N/C"));
    }

    oled.setTextSize(1);
    oled.setCursor(80, 12);
    oled.print(F("Up:"));
    if (isSensorConnected(1)) {
        oled.setCursor(96, 12);
        oled.print(getSensorTemp(1), 1);
    } else {
        oled.setCursor(96, 12);
        oled.print(F("N/C"));
    }

    oled.drawLine(0, 28, 127, 28, SSD1306_WHITE);

    // ── Row 3: Run state + step info ──
    oled.setTextSize(1);
    oled.setCursor(0, 31);

    if (isAutoTuning()) {
        oled.print(F("AUTOTUNE sensor "));
        oled.print(getAutoTuneSensor());
    } else {
        switch (rs) {
            case RUN_IDLE:
                oled.print(F("IDLE"));
                oled.setCursor(0, 41);
                oled.print(F("[SET]=Menu [S/S]=Go"));
                break;
            case RUN_HEATING: {
                int cs = getCurrentStep();
                RunProfile& prof = s.profiles[s.activeProfile];
                ProfileStep& step = prof.steps[cs];
                oled.print(step.coolMode ? F("COOL ") : F("HEAT "));
                oled.print(F("Step "));
                oled.print(cs + 1);
                oled.print(F("/"));
                oled.print(prof.numSteps);
                oled.setCursor(0, 41);
                if (step.numAssignments > 0) {
                    oled.print(F("Tgt:"));
                    oled.print(step.assignments[0].targetTemp, 1);
                    oled.print(F("F PWM:"));
                    oled.print(getSSRPWM());
                    oled.print(F("/"));
                    oled.print(step.assignments[0].maxPWM);
                }
                break;
            }
            case RUN_HOLDING: {
                int cs = getCurrentStep();
                RunProfile& prof = s.profiles[s.activeProfile];
                ProfileStep& step = prof.steps[cs];
                oled.print(F("HOLD Step "));
                oled.print(cs + 1);
                oled.print(F("/"));
                oled.print(prof.numSteps);
                oled.setCursor(0, 41);
                unsigned long rem = getHoldRemaining();
                unsigned long m = rem / 60;
                unsigned long sec = rem % 60;
                oled.print(F("Rem:"));
                oled.print(m);
                oled.print(F("m"));
                if (sec < 10) oled.print('0');
                oled.print(sec);
                oled.print(F("s PWM:"));
                oled.print(getSSRPWM());
                break;
            }
            case RUN_DONE:
                oled.print(F("DONE - All steps"));
                oled.setCursor(0, 41);
                oled.print(F("complete!"));
                break;
        }
    }

    // ── Row 4: Output status bar (bottom) ──
    oled.drawLine(0, 53, 127, 53, SSD1306_WHITE);
    oled.setCursor(0, 55);
    const char* outShort[] = { "SSR", "RL1", "RL2", "DC1", "DC2" };
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (getOutput(i)) {
            // Draw filled box around active output
            int x = i * 26;
            oled.fillRoundRect(x, 54, 24, 10, 2, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
            oled.setCursor(x + 2, 55);
            oled.print(outShort[i]);
            oled.setTextColor(SSD1306_WHITE);
        } else {
            oled.setCursor(i * 26 + 2, 55);
            oled.print(outShort[i]);
        }
    }
}

// ─── Menu Screen ────────────────────────────────────────────────────────────

static void drawMenu() {
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print(F("=== MENU ==="));
    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // Adjust scroll window
    if (menuSel < menuScroll) menuScroll = menuSel;
    if (menuSel >= menuScroll + MENU_VISIBLE) menuScroll = menuSel - MENU_VISIBLE + 1;

    for (int i = 0; i < MENU_VISIBLE && (menuScroll + i) < MENU_COUNT; i++) {
        int idx = menuScroll + i;
        int y = 12 + i * 10;

        if (idx == menuSel) {
            // Highlight selected item
            oled.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }
        oled.setCursor(4, y);
        oled.print(menuLabels[idx]);
        oled.setTextColor(SSD1306_WHITE);  // Reset
    }

    // Scroll indicators
    if (menuScroll > 0) {
        oled.setCursor(120, 12);
        oled.print(F("^"));
    }
    if (menuScroll + MENU_VISIBLE < MENU_COUNT) {
        oled.setCursor(120, 52);
        oled.print(F("v"));
    }
}

// ─── WiFi Info Screen ───────────────────────────────────────────────────────

static void drawWiFiInfo() {
    Settings& s = getSettings();

    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print(F("=== WiFi Info ==="));
    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    oled.setCursor(0, 13);
    if (!isWiFiReady()) {
        oled.print(F("WiFi: DISABLED"));
        oled.setCursor(0, 25);
        oled.print(F("ESP8266 not responding"));
    } else if (isWiFiSTA()) {
        oled.print(F("Mode: Client (STA)"));
        oled.setCursor(0, 25);
        oled.print(F("Net: "));
        oled.print(s.wifi.ssid);
        oled.setCursor(0, 37);
        oled.print(F("IP: "));
        oled.print(getWiFiIP());
    } else {
        oled.print(F("Mode: Hotspot (AP)"));
        oled.setCursor(0, 25);
        oled.print(F("SSID: SmartPID-Still"));
        oled.setCursor(0, 37);
        oled.print(F("IP: 192.168.4.1"));
        if (isWiFiSTAFailed()) {
            oled.setCursor(0, 49);
            oled.print(F("! Join failed: "));
            oled.print(s.wifi.ssid);
        }
    }

    oled.setCursor(0, 56);
    oled.print(F("[S/S] Back"));
}

// ─── Emergency Stop Screen ─────────────────────────────────────────────────

static void drawEStop() {
    // Flashing border effect
    bool flash = (millis() / 300) % 2;

    if (flash) {
        oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);
        oled.drawRect(1, 1, 126, 62, SSD1306_WHITE);
        oled.drawRect(2, 2, 124, 60, SSD1306_WHITE);
    }

    oled.setTextSize(2);
    oled.setCursor(8, 8);
    oled.print(F("!! ESTOP"));
    oled.setCursor(8, 26);
    oled.print(F("ALL OFF"));

    oled.setTextSize(1);
    oled.setCursor(0, 48);
    oled.print(F("Outputs killed."));
    oled.setCursor(0, 56);
    oled.print(F("[SET] to clear"));
}

// ─── Menu Action Execution ──────────────────────────────────────────────────

static void executeMenuItem() {
    switch ((MenuItem)menuSel) {
        case MENU_BACK:
            currentScreen = SCREEN_DASHBOARD;
            break;
        case MENU_START_STOP:
            if (getRunState() == RUN_IDLE || getRunState() == RUN_DONE) {
                startProfile();
            } else {
                stopProfile();
            }
            currentScreen = SCREEN_DASHBOARD;
            break;
        case MENU_WIFI_INFO:
            currentScreen = SCREEN_WIFI_INFO;
            break;
        case MENU_AUTOTUNE_LOWER:
            if (isAutoTuning()) {
                stopAutoTune();
            } else {
                startAutoTune(0);
            }
            currentScreen = SCREEN_DASHBOARD;
            break;
        case MENU_AUTOTUNE_UPPER:
            if (isAutoTuning()) {
                stopAutoTune();
            } else {
                startAutoTune(1);
            }
            currentScreen = SCREEN_DASHBOARD;
            break;
        case MENU_OUTPUTS:
            allOutputsOff();
            currentScreen = SCREEN_DASHBOARD;
            break;
        default:
            break;
    }
}

// ─── Button Handling ────────────────────────────────────────────────────────

void handleButton(ButtonEvent evt) {
    if (evt == BTN_NONE) return;

    // Emergency stop — highest priority, always active
    if (evt == BTN_SS_LONG) {
        triggerEmergencyStop();
        return;
    }

    // If in ESTOP, only SELECT can clear it
    if (eStopped) {
        if (evt == BTN_SELECT_PRESS) {
            clearEmergencyStop();
        }
        return;
    }

    switch (currentScreen) {
        case SCREEN_SPLASH:
            // Any button skips splash
            currentScreen = SCREEN_DASHBOARD;
            break;

        case SCREEN_DASHBOARD:
            switch (evt) {
                case BTN_SELECT_PRESS:
                    currentScreen = SCREEN_MENU;
                    menuSel = 0;
                    menuScroll = 0;
                    break;
                case BTN_SS_PRESS:
                    // Quick start/stop from dashboard
                    if (getRunState() == RUN_IDLE || getRunState() == RUN_DONE) {
                        startProfile();
                    } else {
                        stopProfile();
                    }
                    break;
                default: break;
            }
            break;

        case SCREEN_MENU:
            switch (evt) {
                case BTN_UP_PRESS:
                    if (menuSel > 0) menuSel--;
                    break;
                case BTN_DOWN_PRESS:
                    if (menuSel < MENU_COUNT - 1) menuSel++;
                    break;
                case BTN_SELECT_PRESS:
                    executeMenuItem();
                    break;
                case BTN_SS_PRESS:
                    // Back to dashboard
                    currentScreen = SCREEN_DASHBOARD;
                    break;
                default: break;
            }
            break;

        case SCREEN_WIFI_INFO:
            if (evt == BTN_SS_PRESS || evt == BTN_SELECT_PRESS) {
                currentScreen = SCREEN_DASHBOARD;
            }
            break;

        default:
            break;
    }
}

// ─── Emergency Stop ─────────────────────────────────────────────────────────

void triggerEmergencyStop() {
    eStopped = true;
    currentScreen = SCREEN_ESTOP;

    // Log ESTOP before stopping profile
    if (getRunState() != RUN_IDLE && getRunState() != RUN_DONE) {
        logEStop();
    }

    // Kill everything immediately
    stopProfile();
    allOutputsOff();

    // Alarm beeps
    for (int i = 0; i < 5; i++) {
        tone(PIN_BUZZER, 3000, 200);
        delay(250);
    }

    SerialUSB.println(F("!!! EMERGENCY STOP !!!"));
}

bool isEmergencyStopped() {
    return eStopped;
}

void clearEmergencyStop() {
    eStopped = false;
    currentScreen = SCREEN_DASHBOARD;
    tone(PIN_BUZZER, 2000, 100);
    SerialUSB.println(F("[estop] Cleared"));
}

// ─── Init + Update ──────────────────────────────────────────────────────────

void initDisplay() {
    Wire.begin();   // Must init I2C bus before SSD1306
    delay(10);      // Let bus settle

    // ── I2C bus scan (diagnostic) ────────────────────────────────────────
    SerialUSB.println(F("[i2c] Scanning bus..."));
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            SerialUSB.print(F("[i2c]   Device at 0x"));
            if (addr < 16) SerialUSB.print('0');
            SerialUSB.println(addr, HEX);
            found++;
        }
    }
    if (found == 0) {
        SerialUSB.println(F("[i2c]   No devices found!"));
    }

    // ── SSD1306 init ─────────────────────────────────────────────────────
    SerialUSB.print(F("[oled] Init at 0x"));
    SerialUSB.print(OLED_I2C_ADDR, HEX);
    SerialUSB.println(F("..."));

    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        SerialUSB.println(F("[oled] SSD1306 init FAILED!"));
        return;
    }
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextWrap(false);

    // Show splash
    currentScreen = SCREEN_SPLASH;
    splashEnd = millis() + 2000;
    drawSplash();
    oled.display();

    SerialUSB.println(F("[oled] Display initialized 128x64"));
}

void updateDisplay() {
    unsigned long now = millis();
    if (now - lastRefresh < REFRESH_MS) return;
    lastRefresh = now;

    // Auto-transition from splash
    if (currentScreen == SCREEN_SPLASH && now >= splashEnd) {
        currentScreen = SCREEN_DASHBOARD;
    }

    oled.clearDisplay();

    switch (currentScreen) {
        case SCREEN_SPLASH:     drawSplash();     break;
        case SCREEN_DASHBOARD:  drawDashboard();  break;
        case SCREEN_MENU:       drawMenu();       break;
        case SCREEN_ESTOP:      drawEStop();      break;
        case SCREEN_WIFI_INFO:  drawWiFiInfo();   break;
    }

    oled.display();
}
