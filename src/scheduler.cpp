// ═══════════════════════════════════════════════════════════════════════════════
//  Cooperative Task Scheduler — Priority-based, non-preemptive
//  Designed for single-core SAMD21 with blocking ESP8266 AT commands
// ═══════════════════════════════════════════════════════════════════════════════

#include "scheduler.h"

// ─── Task Table ─────────────────────────────────────────────────────────────

static Task tasks[MAX_TASKS];
static int  taskCount = 0;

// Guard against re-entrant yieldCritical() calls
static volatile bool inYield = false;

// ─── Initialization ─────────────────────────────────────────────────────────

void schedulerInit() {
    taskCount = 0;
    inYield   = false;
    memset(tasks, 0, sizeof(tasks));
}

// ─── Add Task ───────────────────────────────────────────────────────────────

int schedulerAddTask(
    TaskFunc     func,
    TaskPriority priority,
    uint16_t     intervalMs,
    uint16_t     maxRuntimeMs,
    const char*  name
) {
    if (taskCount >= MAX_TASKS || !func) return -1;

    int id = taskCount++;
    Task& t    = tasks[id];
    t.func         = func;
    t.priority     = priority;
    t.intervalMs   = intervalMs;
    t.maxRuntimeMs = maxRuntimeMs;
    t.lastRunMs    = 0;
    t.lastDuration = 0;
    t.enabled      = true;
    t.name         = name;

    return id;
}

// ─── Check if a task is due to run ──────────────────────────────────────────

static inline bool taskIsDue(const Task& t, unsigned long now) {
    if (!t.enabled || !t.func) return false;
    if (t.intervalMs == 0) return true;  // Run every cycle
    return (now - t.lastRunMs) >= t.intervalMs;
}

// ─── Run a single task, measure duration ────────────────────────────────────

static void runTask(Task& t) {
    unsigned long startUs = micros();
    t.func();
    t.lastDuration = micros() - startUs;
    t.lastRunMs    = millis();
}

// ─── Main Scheduler Cycle ───────────────────────────────────────────────────
//
// Runs ALL due tasks in priority order (critical first).
// Within the same priority, tasks run in registration order.

void schedulerRun() {
    unsigned long now = millis();

    for (uint8_t pri = 0; pri < NUM_PRIORITIES; pri++) {
        for (int i = 0; i < taskCount; i++) {
            Task& t = tasks[i];
            if (t.priority == pri && taskIsDue(t, now)) {
                runTask(t);
                // Refresh 'now' after each task in case it took a while
                now = millis();
            }
        }
    }
}

// ─── Yield to Critical Tasks ────────────────────────────────────────────────
//
// Called from inside blocking WiFi code to ensure sensors + PID don't starve.
// Only runs PRIORITY_CRITICAL tasks that are due. Safe against re-entrance.

bool yieldCritical() {
    if (inYield) return false;  // Prevent recursion
    inYield = true;

    bool didWork = false;
    unsigned long now = millis();

    for (int i = 0; i < taskCount; i++) {
        Task& t = tasks[i];
        if (t.priority == PRIORITY_CRITICAL && taskIsDue(t, now)) {
            runTask(t);
            didWork = true;
            now = millis();
        }
    }

    inYield = false;
    return didWork;
}

// ─── Task Control ───────────────────────────────────────────────────────────

void schedulerEnableTask(int taskId, bool enabled) {
    if (taskId >= 0 && taskId < taskCount) {
        tasks[taskId].enabled = enabled;
    }
}

unsigned long schedulerGetLastDuration(int taskId) {
    if (taskId >= 0 && taskId < taskCount) {
        return tasks[taskId].lastDuration;
    }
    return 0;
}

// ─── Diagnostics ────────────────────────────────────────────────────────────

void schedulerPrintStats() {
    SerialUSB.println(F("[sched] Task Stats:"));
    for (int i = 0; i < taskCount; i++) {
        Task& t = tasks[i];
        SerialUSB.print(F("  P"));
        SerialUSB.print(t.priority);
        SerialUSB.print(F(" "));
        SerialUSB.print(t.name ? t.name : "?");
        SerialUSB.print(F(": "));
        if (t.lastDuration > 1000) {
            SerialUSB.print(t.lastDuration / 1000);
            SerialUSB.print(F("ms"));
        } else {
            SerialUSB.print(t.lastDuration);
            SerialUSB.print(F("us"));
        }
        SerialUSB.print(t.enabled ? F(" [on]") : F(" [off]"));
        SerialUSB.println();
    }
}
