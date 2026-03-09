#include "storage.h"
#include <FlashStorage.h>

FlashStorage(flash_store, Settings);

static Settings current;

void loadSettings() {
    current = flash_store.read();
    if (current.magic != SETTINGS_MAGIC) {
        // First boot or struct changed — initialize defaults
        memset(&current, 0, sizeof(current));
        current.magic = SETTINGS_MAGIC;

        // WiFi: unconfigured → AP-only mode
        current.wifi.configured = false;

        // Sensor configs: PID defaults + output assignment
        current.sensorCfg[0] = { {2.0f, 0.05f, 1.0f}, 0 }; // Lower → SSR
        current.sensorCfg[1] = { {2.0f, 0.05f, 1.0f}, 0 }; // Upper → SSR

        // Active profile
        current.activeProfile = 0;

        // Profile 0: "Default" with one step, one sensor assignment
        strncpy(current.profiles[0].name, "Default", sizeof(current.profiles[0].name));
        current.profiles[0].numSteps = 1;
        current.profiles[0].steps[0].numAssignments = 1;
        current.profiles[0].steps[0].coolMode = false;
        current.profiles[0].steps[0].holdMinutes = 60;
        current.profiles[0].steps[0].assignments[0].sensorIndex = 0;     // Lower probe
        current.profiles[0].steps[0].assignments[0].outputIndex = OUT_SSR;
        current.profiles[0].steps[0].assignments[0].maxPWM      = 255;
        current.profiles[0].steps[0].assignments[0].targetTemp   = 175.0f;

        // Profiles 1 and 2: empty
        strncpy(current.profiles[1].name, "Profile 2", sizeof(current.profiles[1].name));
        current.profiles[1].numSteps = 0;
        strncpy(current.profiles[2].name, "Profile 3", sizeof(current.profiles[2].name));
        current.profiles[2].numSteps = 0;

        saveSettings();
    }
}

void saveSettings() {
    flash_store.write(current);
}

Settings& getSettings() {
    return current;
}
