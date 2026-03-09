#include "sensors.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ─── OneWire buses (one per probe connector) ────────────────────────────────
static OneWire owLower(PIN_DS18B20_LOWER);
static OneWire owUpper(PIN_DS18B20_UPPER);
static DallasTemperature sensLower(&owLower);
static DallasTemperature sensUpper(&owUpper);

// ─── State ──────────────────────────────────────────────────────────────────
static float temps[2]      = { -999.0f, -999.0f };
static bool  connected[2]  = { false, false };

static unsigned long lastReadTime      = 0;
static bool          conversionPending = false;
static unsigned long conversionStart   = 0;

// ─── Public API ─────────────────────────────────────────────────────────────

void initSensors() {
    sensLower.begin();
    sensUpper.begin();
    sensLower.setWaitForConversion(false);  // Non-blocking
    sensUpper.setWaitForConversion(false);
    sensLower.setResolution(12);
    sensUpper.setResolution(12);

    connected[0] = sensLower.getDeviceCount() > 0;
    connected[1] = sensUpper.getDeviceCount() > 0;

    SerialUSB.print(F("Sensors: lower="));
    SerialUSB.print(connected[0] ? "OK" : "N/C");
    SerialUSB.print(F("  upper="));
    SerialUSB.println(connected[1] ? "OK" : "N/C");
}

void updateSensors() {
    unsigned long now = millis();

    // Phase 1: Start async conversion
    if (!conversionPending && (now - lastReadTime >= SENSOR_READ_MS)) {
        if (connected[0]) sensLower.requestTemperatures();
        if (connected[1]) sensUpper.requestTemperatures();
        conversionPending = true;
        conversionStart   = now;
        return;
    }

    // Phase 2: Read results after conversion time (~750 ms for 12-bit)
    if (conversionPending && (now - conversionStart >= 800)) {
        if (connected[0]) {
            float t = sensLower.getTempCByIndex(0);
            if (t > -100.0f) {
                temps[0] = t * 9.0f / 5.0f + 32.0f;  // °C → °F
            } else {
                // Sensor may have been disconnected
                temps[0] = -999.0f;
                // Try to re-detect next cycle
                connected[0] = sensLower.getDeviceCount() > 0;
            }
        }

        if (connected[1]) {
            float t = sensUpper.getTempCByIndex(0);
            if (t > -100.0f) {
                temps[1] = t * 9.0f / 5.0f + 32.0f;
            } else {
                temps[1] = -999.0f;
                connected[1] = sensUpper.getDeviceCount() > 0;
            }
        }

        conversionPending = false;
        lastReadTime      = now;
    }
}

float getSensorTemp(int index) {
    if (index < 0 || index > 1) return -999.0f;
    return temps[index];
}

bool isSensorConnected(int index) {
    if (index < 0 || index > 1) return false;
    return connected[index];
}

void rescanSensors() {
    // Only rescan buses that are currently disconnected — avoid
    // disrupting active reads on connected sensors.
    if (!connected[0]) {
        sensLower.begin();
        sensLower.setWaitForConversion(false);
        sensLower.setResolution(12);
        if (sensLower.getDeviceCount() > 0) {
            connected[0] = true;
            SerialUSB.println(F("[sensor] Lower probe connected"));
        }
    }
    if (!connected[1]) {
        sensUpper.begin();
        sensUpper.setWaitForConversion(false);
        sensUpper.setResolution(12);
        if (sensUpper.getDeviceCount() > 0) {
            connected[1] = true;
            SerialUSB.println(F("[sensor] Upper probe connected"));
        }
    }
}
