#include "display.h"
#include <WiFi.h>
#include "config.h"
#include <SSD1306Wire.h>

static SSD1306Wire oled(OLED_ADDR, OLED_SDA_PIN, OLED_SCL_PIN);
static DisplayScreen currentScreen = SCREEN_MAIN;
static unsigned long lastRefresh = 0;
static const unsigned long REFRESH_MS = 250; // 4 FPS

// Message overlay
static char msgBuf[40] = {0};
static unsigned long msgExpiry = 0;

// Temperature history for mini graph (last 60 samples = 60 seconds)
#define GRAPH_SAMPLES 60
static float graphCh0[GRAPH_SAMPLES] = {0};
static float graphCh1[GRAPH_SAMPLES] = {0};
static int graphIdx = 0;
static unsigned long lastGraphSample = 0;

void display_init() {
    oled.init();
    oled.flipScreenVertically();
    oled.setFont(ArialMT_Plain_10);
    oled.clear();
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.drawString(64, 10, "SmartPID CUBE");
    oled.drawString(64, 26, "Still Controller");
    oled.drawString(64, 46, "v2.0");
    oled.display();
    Serial.println("[OLED] Display initialized");
}

static void drawBar(int x, int y, int w, int h, float pct) {
    oled.drawRect(x, y, w, h);
    int fill = (int)(pct / 100.0 * (w - 2));
    if (fill > 0) oled.fillRect(x + 1, y + 1, fill, h - 2);
}

static void drawMiniGraph(int x, int y, int w, int h, float* data, int count, float minVal, float maxVal) {
    // Draw axes
    oled.drawLine(x, y, x, y + h);
    oled.drawLine(x, y + h, x + w, y + h);

    if (maxVal <= minVal) maxVal = minVal + 1;
    float range = maxVal - minVal;

    for (int i = 1; i < count && i < w; i++) {
        int idx0 = (graphIdx - count + i - 1 + GRAPH_SAMPLES) % GRAPH_SAMPLES;
        int idx1 = (graphIdx - count + i + GRAPH_SAMPLES) % GRAPH_SAMPLES;
        int y0 = y + h - (int)((data[idx0] - minVal) / range * h);
        int y1 = y + h - (int)((data[idx1] - minVal) / range * h);
        y0 = constrain(y0, y, y + h);
        y1 = constrain(y1, y, y + h);
        oled.drawLine(x + i - 1, y0, x + i, y1);
    }
}

static void drawMainScreen() {
    // ─── Channel 0 (left half) ───
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
    oled.setFont(ArialMT_Plain_10);

    const char* mode0 = (gState.ch[0].mode == MODE_PID) ? "PID" : "ON/OFF";
    const char* state0 = gState.ch[0].autotuning ? "TUNE" :
                          gState.ch[0].enabled ? "ON" : "OFF";
    char line0[20];
    snprintf(line0, sizeof(line0), "B:%s %s", mode0, state0);
    oled.drawString(0, 0, line0);

    // Temperature big font
    oled.setFont(ArialMT_Plain_16);
    if (!isnan(gState.ch[0].temperature)) {
        char temp0[10];
        snprintf(temp0, sizeof(temp0), "%.1f", gState.ch[0].temperature);
        oled.drawString(0, 12, temp0);
    } else {
        oled.drawString(0, 12, "ERR");
    }

    // Setpoint small
    oled.setFont(ArialMT_Plain_10);
    char sp0[12];
    snprintf(sp0, sizeof(sp0), "SP:%.0f", gState.ch[0].setpoint);
    oled.drawString(0, 30, sp0);

    // Output bar
    float pct0 = gState.ch[0].pidOutput / PID_OUTPUT_MAX * 100.0;
    drawBar(0, 42, 60, 6, pct0);

    // Mini graph (bottom-left)
    drawMiniGraph(0, 50, 60, 13, graphCh0, GRAPH_SAMPLES, 
                  gState.ch[0].setpoint - 10, gState.ch[0].setpoint + 10);

    // ─── Channel 1 (right half) ───
    const char* mode1 = (gState.ch[1].mode == MODE_PID) ? "PID" : "ON/OFF";
    const char* state1 = gState.ch[1].autotuning ? "TUNE" :
                          gState.ch[1].enabled ? "ON" : "OFF";
    char line1[20];
    snprintf(line1, sizeof(line1), "C:%s %s", mode1, state1);
    oled.drawString(66, 0, line1);

    oled.setFont(ArialMT_Plain_16);
    if (!isnan(gState.ch[1].temperature)) {
        char temp1[10];
        snprintf(temp1, sizeof(temp1), "%.1f", gState.ch[1].temperature);
        oled.drawString(66, 12, temp1);
    } else {
        oled.drawString(66, 12, "ERR");
    }

    oled.setFont(ArialMT_Plain_10);
    char sp1[12];
    snprintf(sp1, sizeof(sp1), "SP:%.0f", gState.ch[1].setpoint);
    oled.drawString(66, 30, sp1);

    float pct1 = gState.ch[1].pidOutput / PID_OUTPUT_MAX * 100.0;
    drawBar(66, 42, 60, 6, pct1);

    drawMiniGraph(66, 50, 60, 13, graphCh1, GRAPH_SAMPLES,
                  gState.ch[1].setpoint - 10, gState.ch[1].setpoint + 10);

    // Divider
    oled.drawLine(63, 0, 63, 63);
}

static void drawDetailScreen(int ch) {
    oled.setFont(ArialMT_Plain_10);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);

    char title[24];
    snprintf(title, sizeof(title), "Channel %d: %s", ch, ch == 0 ? "Boiler" : "Column");
    oled.drawString(0, 0, title);
    oled.drawLine(0, 11, 127, 11);

    // Temperature + Setpoint
    char tempLine[32];
    if (!isnan(gState.ch[ch].temperature)) {
        snprintf(tempLine, sizeof(tempLine), "Temp: %.1fC  SP: %.1fC",
                 gState.ch[ch].temperature, gState.ch[ch].setpoint);
    } else {
        snprintf(tempLine, sizeof(tempLine), "Temp: ERR  SP: %.1fC", gState.ch[ch].setpoint);
    }
    oled.drawString(0, 13, tempLine);

    // Mode + State
    const char* modeStr = (gState.ch[ch].mode == MODE_PID) ? "PID-PWM" : "ON/OFF";
    const char* stateStr = gState.ch[ch].autotuning ? "AUTOTUNING" :
                           gState.ch[ch].enabled ? "ENABLED" : "DISABLED";
    char modeLine[32];
    snprintf(modeLine, sizeof(modeLine), "Mode: %s  %s", modeStr, stateStr);
    oled.drawString(0, 25, modeLine);

    // PID params or hysteresis
    char paramLine[40];
    if (gState.ch[ch].mode == MODE_PID) {
        snprintf(paramLine, sizeof(paramLine), "P:%.1f I:%.1f D:%.1f",
                 gState.ch[ch].params.Kp, gState.ch[ch].params.Ki, gState.ch[ch].params.Kd);
    } else {
        snprintf(paramLine, sizeof(paramLine), "Hyst: +/-%.1fC", gState.ch[ch].hysteresis);
    }
    oled.drawString(0, 37, paramLine);

    // Output + assignment
    OutputID oid = gState.ch[ch].assignedOutput;
    const char* outName = (oid < NUM_OUTPUTS) ? OUTPUT_NAMES[oid] : "None";
    char outLine[40];
    snprintf(outLine, sizeof(outLine), "Out: %s  %.0f%%",
             outName, gState.ch[ch].pidOutput / PID_OUTPUT_MAX * 100.0);
    oled.drawString(0, 49, outLine);
}

static void drawConfigScreen() {
    oled.setFont(ArialMT_Plain_10);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
    oled.drawString(0, 0, "System Config");
    oled.drawLine(0, 11, 127, 11);

    char buf[32];
    unsigned long s = gState.uptime;
    snprintf(buf, sizeof(buf), "Uptime: %luh %lum %lus", s / 3600, (s % 3600) / 60, s % 60);
    oled.drawString(0, 14, buf);

    oled.drawString(0, 26, gState.wifiConnected ? "WiFi: Connected" : "WiFi: AP Mode");
    oled.drawString(0, 38, gState.sdCardPresent ? "SD: Mounted" : "SD: Not found");

    snprintf(buf, sizeof(buf), "IP: %s", WiFi.softAPIP().toString().c_str());
    oled.drawString(0, 50, buf);
}

void display_update() {
    unsigned long now = millis();
    if (now - lastRefresh < REFRESH_MS) return;
    lastRefresh = now;

    // Sample for graph (every 1 sec)
    if (now - lastGraphSample >= 1000) {
        lastGraphSample = now;
        graphCh0[graphIdx] = isnan(gState.ch[0].temperature) ? 0 : gState.ch[0].temperature;
        graphCh1[graphIdx] = isnan(gState.ch[1].temperature) ? 0 : gState.ch[1].temperature;
        graphIdx = (graphIdx + 1) % GRAPH_SAMPLES;
    }

    oled.clear();

    // Message overlay takes priority
    if (msgBuf[0] && now < msgExpiry) {
        oled.setFont(ArialMT_Plain_16);
        oled.setTextAlignment(TEXT_ALIGN_CENTER);
        oled.drawString(64, 24, msgBuf);
        oled.display();
        return;
    }
    msgBuf[0] = 0;

    switch (currentScreen) {
        case SCREEN_MAIN:       drawMainScreen(); break;
        case SCREEN_CH0_DETAIL: drawDetailScreen(0); break;
        case SCREEN_CH1_DETAIL: drawDetailScreen(1); break;
        case SCREEN_CONFIG:     drawConfigScreen(); break;
        default:                drawMainScreen(); break;
    }

    oled.display();
}

void display_showMessage(const char* msg, unsigned long durationMs) {
    strncpy(msgBuf, msg, sizeof(msgBuf) - 1);
    msgBuf[sizeof(msgBuf) - 1] = 0;
    msgExpiry = millis() + durationMs;
}

void display_setScreen(DisplayScreen screen) {
    currentScreen = screen;
}

DisplayScreen display_getScreen() {
    return currentScreen;
}
