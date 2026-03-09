#include "sdlog.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>

static bool sdReady = false;
static String logFilename = "";
static unsigned long lastLogTime = 0;

static String makeFilename() {
    // Use boot time (uptime = 0) to create a unique filename
    // Format: /still_YYYYMMDD_HHMMSS.csv  (or just sequential if no RTC)
    unsigned long bootId = millis();
    char buf[32];
    snprintf(buf, sizeof(buf), "/still_%08lX.csv", bootId);
    return String(buf);
}

bool sdlog_init() {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] Card mount failed or not present");
        gState.sdCardPresent = false;
        sdReady = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No SD card detected");
        gState.sdCardPresent = false;
        sdReady = false;
        return false;
    }

    gState.sdCardPresent = true;
    logFilename = makeFilename();

    // Write CSV header
    File f = SD.open(logFilename, FILE_WRITE);
    if (f) {
        f.println("uptime_s,ch0_temp,ch0_sp,ch0_output_pct,ch0_enabled,"
                  "ch1_temp,ch1_sp,ch1_output_pct,ch1_enabled");
        f.close();
        Serial.println("[SD] Logging to: " + logFilename);
        sdReady = true;
        gState.sdLogging = true;
    } else {
        Serial.println("[SD] Failed to create log file");
        sdReady = false;
    }

    Serial.printf("[SD] Card type: %d, size: %lluMB\n", cardType, SD.cardSize() / (1024 * 1024));
    return sdReady;
}

void sdlog_update() {
    if (!sdReady) return;

    unsigned long now = millis();
    if (now - lastLogTime < SD_LOG_INTERVAL_MS) return;
    lastLogTime = now;

    File f = SD.open(logFilename, FILE_APPEND);
    if (!f) {
        Serial.println("[SD] Failed to append to log");
        gState.sdLogging = false;
        return;
    }

    char line[128];
    snprintf(line, sizeof(line), "%lu,%.2f,%.1f,%.1f,%d,%.2f,%.1f,%.1f,%d",
        gState.uptime,
        isnan(gState.ch[0].temperature) ? -999.0 : gState.ch[0].temperature,
        gState.ch[0].setpoint,
        gState.ch[0].pidOutput / PID_OUTPUT_MAX * 100.0,
        gState.ch[0].enabled ? 1 : 0,
        isnan(gState.ch[1].temperature) ? -999.0 : gState.ch[1].temperature,
        gState.ch[1].setpoint,
        gState.ch[1].pidOutput / PID_OUTPUT_MAX * 100.0,
        gState.ch[1].enabled ? 1 : 0
    );

    f.println(line);
    f.close();
    gState.sdLogging = true;
}

bool sdlog_isReady() {
    return sdReady;
}

String sdlog_getFilename() {
    return logFilename;
}
