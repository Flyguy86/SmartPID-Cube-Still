#pragma once

void initSensors();
void updateSensors();       // Call every loop — handles async conversion timing
void rescanSensors();       // Periodic hot-plug: re-detect disconnected probes
float getSensorTemp(int index);   // 0=lower, 1=upper  (°F, -999 if disconnected)
bool  isSensorConnected(int index);
