#include "runlog.h"
#include "sensors.h"
#include "outputs.h"
#include <FlashStorage.h>

// ─── Flash storage for last completed run ───────────────────────────────────
FlashStorage(flash_lastRun, StoredRunLog);

// ─── Active run (RAM) ───────────────────────────────────────────────────────
static RunLogHeader  activeHeader;
static LogEntry      activeEntries[MAX_LOG_ENTRIES];
static bool          logging = false;
static float         lastLoggedTemp[2] = { -999.0f, -999.0f };

// ─── Cached last run (loaded from flash on boot) ───────────────────────────
static StoredRunLog  lastRun;
static bool          lastRunValid = false;

// ─── Helpers ────────────────────────────────────────────────────────────────

static uint16_t secSinceStart() {
    uint32_t elapsed = millis() - activeHeader.startMillis;
    uint32_t sec = elapsed / 1000UL;
    return (sec > 65535) ? 65535 : (uint16_t)sec;
}

static void addEntry(LogEventType type, uint8_t extra, float value) {
    if (!logging) return;
    if (activeHeader.entryCount >= MAX_LOG_ENTRIES) return;

    LogEntry& e = activeEntries[activeHeader.entryCount];
    e.secOffset = secSinceStart();
    e.type      = type;
    e.extra     = extra;
    e.value     = value;
    activeHeader.entryCount++;
}

// ─── Public API ─────────────────────────────────────────────────────────────

void initRunLog() {
    // Load last run from flash
    flash_lastRun.read(&lastRun);
    lastRunValid = (lastRun.header.magic == RUN_LOG_MAGIC);

    if (lastRunValid) {
        SerialUSB.print(F("[log] Last run loaded: "));
        SerialUSB.print(lastRun.header.entryCount);
        SerialUSB.print(F(" entries, "));
        SerialUSB.print(lastRun.header.durationSec);
        SerialUSB.println(F("s duration"));
    } else {
        SerialUSB.println(F("[log] No previous run found"));
    }

    logging = false;
    memset(&activeHeader, 0, sizeof(activeHeader));
}

void logRunStart(uint8_t numSteps) {
    memset(&activeHeader, 0, sizeof(activeHeader));
    activeHeader.magic      = RUN_LOG_MAGIC;
    activeHeader.startMillis = millis();
    activeHeader.numSteps   = numSteps;
    activeHeader.entryCount = 0;
    logging = true;
    lastLoggedTemp[0] = -999.0f;
    lastLoggedTemp[1] = -999.0f;

    addEntry(LOG_RUN_START, 0, (float)numSteps);
    SerialUSB.print(F("[log] Run started, "));
    SerialUSB.print(numSteps);
    SerialUSB.println(F(" steps"));
}

void logRunStop() {
    addEntry(LOG_RUN_STOP, 0, 0);
    activeHeader.durationSec = secSinceStart();
    saveRunLog();
    logging = false;
    SerialUSB.println(F("[log] Run stopped, saved"));
}

void logRunDone() {
    addEntry(LOG_RUN_DONE, 0, 0);
    activeHeader.durationSec = secSinceStart();
    saveRunLog();
    logging = false;
    SerialUSB.println(F("[log] Run complete, saved"));
}

void logStepStart(uint8_t stepIndex, float targetTemp) {
    addEntry(LOG_STEP_START, stepIndex, targetTemp);
}

void logTargetHit(uint8_t stepIndex, float actualTemp) {
    addEntry(LOG_TARGET_HIT, stepIndex, actualTemp);
}

void logHoldDone(uint8_t stepIndex, float actualTemp) {
    addEntry(LOG_HOLD_DONE, stepIndex, actualTemp);
}

void logTempChange(uint8_t sensorIndex, float temp) {
    addEntry(LOG_TEMP_CHANGE, sensorIndex, temp);
    if (sensorIndex < 2) lastLoggedTemp[sensorIndex] = temp;
}

void logEStop() {
    addEntry(LOG_ESTOP, 0, 0);
    activeHeader.durationSec = secSinceStart();
    saveRunLog();
    logging = false;
    SerialUSB.println(F("[log] ESTOP logged, saved"));
}

// Called periodically by scheduler — check for 0.5°F changes
void checkTempLog() {
    if (!logging) return;

    for (int i = 0; i < 2; i++) {
        if (!isSensorConnected(i)) continue;
        float temp = getSensorTemp(i);
        float diff = temp - lastLoggedTemp[i];
        if (diff < 0) diff = -diff;
        if (diff >= LOG_TEMP_THRESHOLD) {
            logTempChange(i, temp);
        }
    }
}

void saveRunLog() {
    if (activeHeader.entryCount == 0) return;

    // Copy active run into stored format
    memcpy(&lastRun.header, &activeHeader, sizeof(RunLogHeader));
    memcpy(lastRun.entries, activeEntries,
           activeHeader.entryCount * sizeof(LogEntry));

    // Clear any remaining entries
    if (activeHeader.entryCount < MAX_LOG_ENTRIES) {
        memset(&lastRun.entries[activeHeader.entryCount], 0,
               (MAX_LOG_ENTRIES - activeHeader.entryCount) * sizeof(LogEntry));
    }

    flash_lastRun.write(lastRun);
    lastRunValid = true;

    SerialUSB.print(F("[log] Saved to flash: "));
    SerialUSB.print(activeHeader.entryCount);
    SerialUSB.println(F(" entries"));
}

// ─── Accessors ──────────────────────────────────────────────────────────────

bool isLogging() {
    return logging;
}

bool hasLastRun() {
    return lastRunValid;
}

const RunLogHeader& getLastRunHeader() {
    return lastRun.header;
}

const LogEntry* getLastRunEntries(uint16_t& count) {
    if (!lastRunValid) { count = 0; return nullptr; }
    count = lastRun.header.entryCount;
    return lastRun.entries;
}

const LogEntry* getActiveEntries(uint16_t& count) {
    count = activeHeader.entryCount;
    return activeEntries;
}

const RunLogHeader& getActiveRunHeader() {
    return activeHeader;
}
