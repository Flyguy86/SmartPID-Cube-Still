#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  SmartPID CUBE — Beer/Spirits Distillation Controller
//  Hardware: ATSAMD21G18 + ESP-WROOM-02 (ESP8266)
//  Pin assignments confirmed by reverse engineering + physical probing
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

// ─── Temperature Sensors ────────────────────────────────────────────────────
#define PIN_DS18B20_LOWER   3   // PA09 — Lower probe connector (OneWire)
#define PIN_DS18B20_UPPER   4   // PA08 — Upper probe connector (OneWire)

// ─── Outputs ────────────────────────────────────────────────────────────────
#define PIN_SSR            22   // PA12 — Solid State Relay (digital on/off)
#define PIN_SSR_PWM        10   // PA18 — SSR PWM output (TC3 hardware PWM)
#define PIN_RELAY1          6   // PA20 — Relay 1
#define PIN_RELAY2          7   // PA21 — Relay 2
#define PIN_DC1            26   // PA27 — DC output 1
#define PIN_DC2            27   // PA28 — DC output 2

// ─── Buzzer ─────────────────────────────────────────────────────────────────
#define PIN_BUZZER          2   // PA14

// ─── Buttons ────────────────────────────────────────────────────────────────
#define PIN_BTN_UP         14   // PA02/A0
#define PIN_BTN_DOWN       17   // PA04/A3
#define PIN_BTN_SELECT      8   // PA06
#define PIN_BTN_SS          9   // PA07 — Start/Stop

// ─── I2C (OLED Display) ────────────────────────────────────────────────────
#define OLED_I2C_ADDR    0x3C   // SDA=PA22(D20), SCL=PA23(D21)

// ─── ESP8266 UART ───────────────────────────────────────────────────────────
#define ESP_BAUD        115200  // Upgraded from 57600 for 2x throughput

// ─── Timing ─────────────────────────────────────────────────────────────────
#define SENSOR_READ_MS    1000  // Read sensors every 1s
#define PID_COMPUTE_MS    1000  // PID update every 1s

// ─── Output Index Constants ─────────────────────────────────────────────────
#define NUM_OUTPUTS  5
#define OUT_SSR      0
#define OUT_RELAY1   1
#define OUT_RELAY2   2
#define OUT_DC1      3
#define OUT_DC2      4

static const int OUTPUT_PINS[NUM_OUTPUTS] = {
    PIN_SSR, PIN_RELAY1, PIN_RELAY2, PIN_DC1, PIN_DC2
};

static const char* const OUTPUT_NAMES[NUM_OUTPUTS] = {
    "SSR", "Relay1", "Relay2", "DC1", "DC2"
};

// ─── Run Profile States ─────────────────────────────────────────────────────
enum RunState : uint8_t {
    RUN_IDLE    = 0,
    RUN_HEATING = 1,
    RUN_HOLDING = 2,
    RUN_DONE    = 3
};

// ─── Persistent Settings ────────────────────────────────────────────────────
#define SETTINGS_MAGIC  0xD15711F2  // Changed: multi-profile + multi-assignment
#define MAX_ASSIGNMENTS_PER_STEP 2  // Max sensors per step (we have 2 probes)
#define MAX_PROFILE_STEPS 10
#define MAX_PROFILES 3

struct PIDParams {
    float Kp;
    float Ki;
    float Kd;
};

struct SensorConfig {
    PIDParams pid;
    uint8_t   outputIndex;  // 0=SSR, 1=Relay1, 2=Relay2, 3=DC1, 4=DC2
};

struct WiFiConfig {
    char ssid[33];
    char password[65];
    bool configured;
};

struct SensorAssignment {
    uint8_t  sensorIndex;   // 0 = lower DS18B20, 1 = upper DS18B20
    uint8_t  outputIndex;   // 0=SSR, 1=Relay1, 2=Relay2, 3=DC1, 4=DC2
    uint8_t  maxPWM;        // Maximum duty cycle (0-255)
    uint8_t  _pad;
    float    targetTemp;    // Target temperature in °F
};

struct ProfileStep {
    uint8_t  numAssignments; // Number of sensor assignments (0-2)
    bool     coolMode;       // false = heat (all >= target), true = cool (all <= target)
    uint16_t holdMinutes;    // Minutes to hold after ALL sensors reach target
    SensorAssignment assignments[MAX_ASSIGNMENTS_PER_STEP];
};

struct RunProfile {
    char     name[16];       // Profile name
    uint8_t  numSteps;       // Number of steps (0 to MAX_PROFILE_STEPS)
    uint8_t  _pad[3];
    ProfileStep steps[MAX_PROFILE_STEPS];
};

struct Settings {
    uint32_t     magic;
    WiFiConfig   wifi;
    SensorConfig sensorCfg[2]; // [0] = lower probe, [1] = upper probe
    uint8_t      activeProfile;  // Index of active profile (0-2)
    uint8_t      _settingsPad[3];
    RunProfile   profiles[MAX_PROFILES];
};
