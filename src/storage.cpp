#include "storage.h"
#include <FlashStorage.h>

FlashStorage(flash_store, Settings);

static Settings current;

void loadSettings() {
    current = flash_store.read();
    if (current.magic != SETTINGS_MAGIC) {
        // First boot — initialize defaults
        memset(&current, 0, sizeof(current));
        current.magic = SETTINGS_MAGIC;

        // WiFi: unconfigured → AP-only mode
        current.wifi.configured = false;

        // Sensor configs: PID defaults + output assignment
        current.sensorCfg[0] = { {2.0f, 0.05f, 1.0f}, 0 }; // Lower → SSR
        current.sensorCfg[1] = { {2.0f, 0.05f, 1.0f}, 0 }; // Upper → SSR

        // Run profile defaults (1 step)
        current.profile.numSteps = 1;
        current.profile.steps[0].targetTemp  = 175.0f;   // °F (typical distillation)
        current.profile.steps[0].holdMinutes = 60;
        current.profile.steps[0].sensorIndex = 0;         // Lower probe
        current.profile.steps[0].maxPWM      = 255;
        current.profile.steps[0].outputIndex = OUT_SSR;   // SSR output

        saveSettings();
    }
}

void saveSettings() {
    flash_store.write(current);
}

Settings& getSettings() {
    return current;
}
