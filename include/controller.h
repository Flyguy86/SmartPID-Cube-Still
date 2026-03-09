#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "config.h"

/// Initialize PID controllers and all outputs
void controller_init();

/// Run one iteration of both control loops + output drive
void controller_update();

/// Start autotune on channel (0 or 1)
void controller_startAutotune(int channel);

/// Cancel autotune on channel
void controller_cancelAutotune(int channel);

/// Enable/disable channel output
void controller_setEnabled(int channel, bool enabled);

/// Set setpoint for channel
void controller_setSetpoint(int channel, double setpoint);

/// Set PID parameters for channel
void controller_setPIDParams(int channel, double kp, double ki, double kd);

/// Set autotune configuration for channel
void controller_setAutotuneConfig(int channel, double outputStep, double noiseBand, int lookbackSec, int maxCycles);

/// Set SSR window size for channel (ms)
void controller_setSSRWindow(int channel, unsigned long windowMs);

/// Set control mode (MODE_PID or MODE_ONOFF)
void controller_setMode(int channel, ControlMode mode);

/// Set hysteresis band for ON/OFF mode (°C)
void controller_setHysteresis(int channel, double hysteresis);

/// Assign a physical output to a channel
void controller_setOutput(int channel, OutputID output);

/// Save current config to NVS
void controller_saveConfig();

/// Load config from NVS
void controller_loadConfig();

#endif // CONTROLLER_H
