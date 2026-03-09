#include "temperature.h"
#include "config.h"

// Two independent OneWire buses
static OneWire ow1(TEMP_SENSOR_1_PIN);
static OneWire ow2(TEMP_SENSOR_2_PIN);
static DallasTemperature sensors1(&ow1);
static DallasTemperature sensors2(&ow2);

static DeviceAddress addr1, addr2;
static bool connected1 = false, connected2 = false;
static double temp1 = NAN, temp2 = NAN;
static unsigned long lastReadTime = 0;

void temperature_init() {
    sensors1.begin();
    sensors2.begin();

    // Set 12-bit resolution for accuracy
    sensors1.setResolution(12);
    sensors2.setResolution(12);

    // Request non-blocking reads
    sensors1.setWaitForConversion(false);
    sensors2.setWaitForConversion(false);

    // Detect sensors
    connected1 = sensors1.getDeviceCount() > 0;
    connected2 = sensors2.getDeviceCount() > 0;

    if (connected1) {
        sensors1.getAddress(addr1, 0);
        Serial.printf("[TEMP] Sensor 1 found: ");
        for (int i = 0; i < 8; i++) Serial.printf("%02X", addr1[i]);
        Serial.println();
    } else {
        Serial.println("[TEMP] WARNING: Sensor 1 not found on pin " + String(TEMP_SENSOR_1_PIN));
    }

    if (connected2) {
        sensors2.getAddress(addr2, 0);
        Serial.printf("[TEMP] Sensor 2 found: ");
        for (int i = 0; i < 8; i++) Serial.printf("%02X", addr2[i]);
        Serial.println();
    } else {
        Serial.println("[TEMP] WARNING: Sensor 2 not found on pin " + String(TEMP_SENSOR_2_PIN));
    }

    // Initial request
    if (connected1) sensors1.requestTemperatures();
    if (connected2) sensors2.requestTemperatures();
    lastReadTime = millis();
}

void temperature_update() {
    unsigned long now = millis();
    if (now - lastReadTime < SENSOR_READ_INTERVAL_MS) return;
    lastReadTime = now;

    if (connected1) {
        double t = sensors1.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C && t > -50 && t < 200) {
            temp1 = t;
        }
        sensors1.requestTemperatures(); // Start next conversion
    }

    if (connected2) {
        double t = sensors2.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C && t > -50 && t < 200) {
            temp2 = t;
        }
        sensors2.requestTemperatures();
    }
}

double temperature_read(int channel) {
    return (channel == 0) ? temp1 : temp2;
}

String temperature_getAddress(int channel) {
    DeviceAddress* addr = (channel == 0) ? &addr1 : &addr2;
    bool conn = (channel == 0) ? connected1 : connected2;
    if (!conn) return "not connected";
    String s = "";
    for (int i = 0; i < 8; i++) {
        if ((*addr)[i] < 0x10) s += "0";
        s += String((*addr)[i], HEX);
    }
    return s;
}

bool temperature_isConnected(int channel) {
    return (channel == 0) ? connected1 : connected2;
}
