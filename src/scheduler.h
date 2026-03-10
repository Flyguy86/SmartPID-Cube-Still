#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  Cooperative Task Scheduler — Priority-based with time budgets
//  Single-core SAMD21: no preemption, tasks must yield cooperatively
//
//  Priority levels:
//    CRITICAL (0) — Temperature sensing + PID/SSR control (safety)
//    HIGH     (1) — WiFi HTTP serving (user experience)
//    NORMAL   (2) — Auto-tune, buzzer feedback
//    LOW      (3) — Debug serial output, housekeeping
//
//  Key feature: yieldCritical() can be called from inside blocking WiFi
//  loops to run safety-critical tasks without fully returning from the
//  current function. This prevents PID/sensor starvation during long
//  AT command sequences.
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// ─── Priority Levels ────────────────────────────────────────────────────────

enum TaskPriority : uint8_t {
    PRIORITY_CRITICAL = 0,   // Temp sensors + PID — MUST run on time
    PRIORITY_HIGH     = 1,   // WiFi poll / HTTP response
    PRIORITY_NORMAL   = 2,   // Auto-tune, buzzer
    PRIORITY_LOW      = 3,   // Debug prints, housekeeping
    NUM_PRIORITIES    = 4
};

// ─── Task Definition ────────────────────────────────────────────────────────

typedef void (*TaskFunc)();

#define MAX_TASKS 16

struct Task {
    TaskFunc     func;          // Function to call
    TaskPriority priority;      // Scheduling priority
    uint16_t     intervalMs;    // Minimum interval between runs (0 = every loop)
    uint16_t     maxRuntimeMs;  // Time budget per invocation (0 = unlimited)
    unsigned long lastRunMs;    // Timestamp of last run
    unsigned long lastDuration; // Actual runtime of last invocation (us)
    bool          enabled;      // Can be disabled at runtime
    const char*   name;         // For debug/diagnostics
};

// ─── Scheduler API ──────────────────────────────────────────────────────────

void schedulerInit();

// Register a task. Returns task ID (index) or -1 on failure.
int schedulerAddTask(
    TaskFunc     func,
    TaskPriority priority,
    uint16_t     intervalMs,
    uint16_t     maxRuntimeMs,
    const char*  name
);

// Run one scheduler cycle — call from loop()
void schedulerRun();

// Yield to critical tasks (call from inside blocking code like WiFi AT waits)
// Returns true if any critical task actually ran
bool yieldCritical();

// Enable/disable a task by ID
void schedulerEnableTask(int taskId, bool enabled);

// Get last measured duration of a task (microseconds)
unsigned long schedulerGetLastDuration(int taskId);

// Print scheduler stats to SerialUSB
void schedulerPrintStats();
