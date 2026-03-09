#pragma once
#include <Arduino.h>

// ─── Run Log System ─────────────────────────────────────────────────────────
// Records temperature changes (≥0.5°F) and profile events during runs.
// Active run log kept in RAM. Last completed run saved to flash.
// Each log entry is 8 bytes. Max 500 entries per run = 4KB.

#define MAX_LOG_ENTRIES   500
#define LOG_TEMP_THRESHOLD 0.5f   // Log every 0.5°F change

// Log entry types
enum LogEventType : uint8_t {
    LOG_RUN_START     = 0,   // Run started,              value = step count
    LOG_RUN_STOP      = 1,   // Run stopped manually,     value = 0
    LOG_RUN_DONE      = 2,   // Run completed all steps,  value = 0
    LOG_STEP_START    = 3,   // Step began heating,       value = target temp
    LOG_TARGET_HIT    = 4,   // Target temp reached,      value = actual temp
    LOG_HOLD_DONE     = 5,   // Hold period completed,    value = actual temp
    LOG_TEMP_CHANGE   = 6,   // Temperature changed ≥0.5, value = new temp
    LOG_ESTOP         = 7,   // Emergency stop triggered,  value = 0
};

// 8 bytes per entry
struct __attribute__((packed)) LogEntry {
    uint16_t     secOffset;  // Seconds since run start (max ~18 hours)
    LogEventType type;       // Event type
    uint8_t      extra;      // Sensor index (for temp) or step index (for events)
    float        value;      // Temperature or relevant value
};

// Run log header
struct __attribute__((packed)) RunLogHeader {
    uint32_t magic;          // Validity marker
    uint32_t startMillis;    // millis() at run start
    uint32_t durationSec;    // Total run duration in seconds
    uint16_t entryCount;     // Number of log entries
    uint8_t  numSteps;       // Profile steps count
    uint8_t  _pad;
};

#define RUN_LOG_MAGIC 0x524C4F47  // "RLOG"

// Stored run = header + entries (saved to flash)
struct StoredRunLog {
    RunLogHeader header;
    LogEntry     entries[MAX_LOG_ENTRIES];
};

// ─── API ────────────────────────────────────────────────────────────────────

void initRunLog();

// Call from profile start/stop/transitions
void logRunStart(uint8_t numSteps);
void logRunStop();
void logRunDone();
void logStepStart(uint8_t stepIndex, float targetTemp);
void logTargetHit(uint8_t stepIndex, float actualTemp);
void logHoldDone(uint8_t stepIndex, float actualTemp);
void logTempChange(uint8_t sensorIndex, float temp);
void logEStop();

// Periodic check — call from scheduler to detect 0.5°F changes
void checkTempLog();

// Save current run log to flash (called on RUN_DONE or RUN_STOP)
void saveRunLog();

// Accessors for web API
bool hasLastRun();                                   // Is there a saved run in flash?
const RunLogHeader& getLastRunHeader();               // Header of last saved run
const LogEntry*     getLastRunEntries(uint16_t& count); // Entries of last saved run
const LogEntry*     getActiveEntries(uint16_t& count);  // Current run entries
const RunLogHeader& getActiveRunHeader();              // Current run header
bool isLogging();
