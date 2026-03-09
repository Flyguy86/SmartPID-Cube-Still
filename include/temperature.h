#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <OneWire.h>
#include <DallasTemperature.h>

/// Initialize both DS18B20 sensors
void temperature_init();

/// Read both sensors (call at SENSOR_READ_INTERVAL_MS)
void temperature_update();

/// Get current temperature for channel (0 or 1), returns NAN on error
double temperature_read(int channel);

/// Get sensor address string for channel
String temperature_getAddress(int channel);

/// Check if a sensor is connected on channel
bool temperature_isConnected(int channel);

#endif // TEMPERATURE_H
