#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  SmartPID CUBE — Hardware Pin Assignments
//  *** VERIFY THESE AGAINST YOUR BOARD REVISION ***
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Temperature Sensor Inputs ──────────────────────────────────────────────
#define TEMP_SENSOR_1_PIN   27   // DS18B20 OneWire bus — Boiler probe
#define TEMP_SENSOR_2_PIN   14   // DS18B20 OneWire bus — Column probe
#define NTC_ADC_1_PIN       36   // NTC 10K analog input Ch0 (SVP, input-only)
#define NTC_ADC_2_PIN       39   // NTC 10K analog input Ch1 (SVN, input-only)
#define NTC_BETA_DEFAULT    3950 // Default beta factor for NTC 10K

// ─── 5 Configurable Outputs (SW-assignable to either channel) ───────────────
#define RELAY_1_PIN         16   // Relay 1 — 10A (NO/NC)
#define RELAY_2_PIN         17   // Relay 2 — 10A (NO/NC)
#define SSR_PIN              4   // SSR output (zero-cross / phase-angle)
#define DC_OUT_1_PIN        25   // 12V DC 2A power output 1
#define DC_OUT_2_PIN        26   // 12V DC 2A power output 2
#define NUM_OUTPUTS          5

// ─── OLED Display (I2C, 1.3" SH1106 or SSD1306) ────────────────────────────
#define OLED_SDA_PIN        21   // I2C SDA (shared with expansion port)
#define OLED_SCL_PIN        22   // I2C SCL (shared with expansion port)
#define OLED_ADDR           0x3C // Typical I2C address
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

// ─── 4 Push Buttons ─────────────────────────────────────────────────────────
#define BTN_UP_PIN          32   // Menu Up / Increase
#define BTN_DOWN_PIN        33   // Menu Down / Decrease
#define BTN_SELECT_PIN      34   // Select / Enter (input-only pin)
#define BTN_BACK_PIN        35   // Back / Cancel  (input-only pin)
#define BTN_DEBOUNCE_MS     50

// ─── Buzzer ─────────────────────────────────────────────────────────────────
#define BUZZER_PIN          13   // Piezo buzzer

// ─── SD Card (SPI — CUBE only) ──────────────────────────────────────────────
#define SD_CS_PIN            5   // SD chip select
#define SD_SCK_PIN          18   // SPI clock
#define SD_MISO_PIN         19   // SPI MISO
#define SD_MOSI_PIN         23   // SPI MOSI

// ─── Status LED ─────────────────────────────────────────────────────────────
#define STATUS_LED_PIN       2   // On-board LED

// ─── WiFi Defaults ──────────────────────────────────────────────────────────
#define DEFAULT_AP_SSID     "SmartPID-Still"
#define DEFAULT_AP_PASS     "distill123"

// ─── PID Defaults ───────────────────────────────────────────────────────────
#define PID_DEFAULT_KP      2.0
#define PID_DEFAULT_KI      5.0
#define PID_DEFAULT_KD      1.0

// ─── PID Output Limits ─────────────────────────────────────────────────────
#define PID_OUTPUT_MIN      0.0
#define PID_OUTPUT_MAX      255.0

// ─── SSR PWM Window (ms) ───────────────────────────────────────────────────
#define SSR_WINDOW_SIZE     5000  // 5-second time-proportional window

// ─── Temperature Defaults (°C) ─────────────────────────────────────────────
#define DEFAULT_SETPOINT_1  78.0  // Ethanol boiling point target
#define DEFAULT_SETPOINT_2  20.0  // Condenser target

// ─── ON/OFF Hysteresis Defaults (°C) ────────────────────────────────────────
#define DEFAULT_HYSTERESIS  1.0   // ±1°C band

// ─── Autotune Parameters ───────────────────────────────────────────────────
#define AUTOTUNE_OUTPUT_STEP    127.0
#define AUTOTUNE_NOISE_BAND     0.5
#define AUTOTUNE_LOOKBACK_SEC   30
#define AUTOTUNE_MAX_CYCLES     10

// ─── Sensor Read Interval ──────────────────────────────────────────────────
#define SENSOR_READ_INTERVAL_MS 1000

// ─── SD Logging Interval ───────────────────────────────────────────────────
#define SD_LOG_INTERVAL_MS  5000  // Log to SD every 5 seconds

// ─── NVS Namespace ─────────────────────────────────────────────────────────
#define NVS_NAMESPACE       "still_cfg"

// ═══════════════════════════════════════════════════════════════════════════════
//  Data Structures
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Control Mode ───────────────────────────────────────────────────────────
enum ControlMode {
    MODE_PID   = 0,   // PID-PWM with time-proportional output
    MODE_ONOFF = 1    // ON/OFF with hysteresis band
};

// ─── Output Identification ──────────────────────────────────────────────────
enum OutputID {
    OUT_RELAY_1 = 0,
    OUT_RELAY_2 = 1,
    OUT_SSR     = 2,
    OUT_DC_1    = 3,
    OUT_DC_2    = 4,
    OUT_NONE    = 255
};

struct PIDParams {
    double Kp;
    double Ki;
    double Kd;
};

struct AutotuneConfig {
    double outputStep;
    double noiseBand;
    int    lookbackSec;
    int    maxCycles;
};

struct ChannelState {
    double temperature;           // Current reading (°C)
    double setpoint;              // Target (°C)
    double pidOutput;             // PID output (0–255)

    // PID tuning
    PIDParams params;
    AutotuneConfig atCfg;

    // Control mode
    ControlMode mode;             // PID or ON/OFF
    double hysteresis;            // ±°C band for ON/OFF mode

    // Output assignment
    OutputID assignedOutput;      // Which physical output this channel drives
    unsigned long ssrWindowMs;    // Time-proportional window (ms)

    // State flags
    bool autotuning;
    bool enabled;
};

struct SystemState {
    ChannelState ch[2];           // Two independent control channels
    bool wifiConnected;
    bool sdCardPresent;
    bool sdLogging;
    unsigned long uptime;
};

// ─── Output pin lookup table ────────────────────────────────────────────────
static const int OUTPUT_PINS[NUM_OUTPUTS] = {
    RELAY_1_PIN, RELAY_2_PIN, SSR_PIN, DC_OUT_1_PIN, DC_OUT_2_PIN
};

static const char* OUTPUT_NAMES[NUM_OUTPUTS] = {
    "Relay 1 (10A)", "Relay 2 (10A)", "SSR", "DC Out 1 (12V)", "DC Out 2 (12V)"
};

extern SystemState gState;

#endif // CONFIG_H
