#ifndef SDLOG_H
#define SDLOG_H

#include <Arduino.h>

/// Initialize SD card (returns true if card detected)
bool sdlog_init();

/// Write a data row (timestamp, ch0 temp/sp/out, ch1 temp/sp/out)
void sdlog_update();

/// Check if SD card is present and writable
bool sdlog_isReady();

/// Get current log filename
String sdlog_getFilename();

#endif // SDLOG_H
