// ═══════════════════════════════════════════════════════════════════════════════
//  SmartPID CUBE — Pin Scanner Diagnostic Tool
//  Flash to SAMD21G18 via USB to reverse-engineer the board layout.
//
//  Connect USB, open serial monitor at 115200 baud. Type 'help' for commands.
//
//  SAFETY: This toggles GPIO pins one at a time. Have a multimeter ready
//  to probe relay coils, SSR traces, LED, buzzer, etc.
// ═══════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <stdarg.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ─── printf helper (SAMD21 SerialUSB lacks printf) ──────────────────────────
static char _pf_buf[256];
static void spr(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(_pf_buf, sizeof(_pf_buf), fmt, args);
    va_end(args);
    SerialUSB.print(_pf_buf);
}

// ─── SAMD21G18 48-pin: All available GPIOs ──────────────────────────────────
// PA24/PA25 = USB, PA30/PA31 = SWD — excluded from scanning

struct PinDef {
    int      arduinoPin;  // Arduino digital pin number (-1 if unmapped)
    uint8_t  port;        // 0 = PORTA, 1 = PORTB
    uint8_t  bit;         // Bit within port
    const char* name;     // Human-readable name
    bool     safe;        // Safe to toggle as output?
};

// Map ALL SAMD21G18 48-pin GPIOs to Arduino pin numbers
// Arduino pin mapping varies by board variant; we try the Zero/MKRZero mapping
// Arduino Zero variant pin mapping (must match zeroUSB board target)
static const PinDef ALL_PINS[] = {
    // Port A — Arduino Zero pin numbers
    { -1, 0,  0,  "PA00", true  },           // Not mapped on Zero
    { -1, 0,  1,  "PA01", true  },           // Not mapped on Zero
    { 14, 0,  2,  "PA02 (A0/DAC)", true },   // A0 = pin 14
    { -1, 0,  3,  "PA03 (AREF)", false },    // Voltage reference — don't drive
    { 17, 0,  4,  "PA04 (A3/BTN2)", true },  // A3 = pin 17, BUTTON 2 (DOWN)
    { 18, 0,  5,  "PA05 (A4)", true },       // A4 = pin 18
    {  8, 0,  6,  "PA06 (D8/BTN3)", true },  // D8, BUTTON 3 (SELECT)
    {  9, 0,  7,  "PA07 (D9/BTN4)", true },  // D9, BUTTON 4 (START/STOP)
    {  4, 0,  8,  "PA08 (D4)", true  },      // D4
    {  3, 0,  9,  "PA09 (D3)", true  },      // D3
    {  1, 0, 10,  "PA10 (D1/TX1)", true },   // D1 = pin 1
    {  0, 0, 11,  "PA11 (D0/RX1)", true },   // D0 = pin 0
    { 22, 0, 12,  "PA12 (SSR)", true  },     // MISO = pin 22, **SSR OUTPUT**
    { -1, 0, 13,  "PA13", true  },           // Not mapped on Zero
    {  2, 0, 14,  "PA14 (D2/BUZZ)", true },  // D2 = pin 2, **BUZZER**
    {  5, 0, 15,  "PA15 (D5)", true  },      // D5
    { 11, 0, 16,  "PA16 (D11)", true },      // D11
    { 13, 0, 17,  "PA17 (D13)", true },      // D13
    { 10, 0, 18,  "PA18 (D10/PWM)", true },  // D10 = pin 10, **TC3 PWM SSR**
    { 12, 0, 19,  "PA19 (D12)", true },      // D12
    {  6, 0, 20,  "PA20 (D6/RL1)", true },   // D6 = pin 6, **RELAY 1**
    {  7, 0, 21,  "PA21 (D7/RL2)", true },   // D7 = pin 7, **RELAY 2**
    { 20, 0, 22,  "PA22 (SDA)", true },      // SDA = pin 20
    { 21, 0, 23,  "PA23 (SCL)", true },      // SCL = pin 21
    { -1, 0, 24,  "PA24 (USB D-)", false },  // USB — NEVER toggle
    { -1, 0, 25,  "PA25 (USB D+)", false },  // USB — NEVER toggle
    // PA26 doesn't exist on 48-pin
    { 26, 0, 27,  "PA27 (DC1)", true  },     // pin 26, **DC1 OUTPUT**
    { 27, 0, 28,  "PA28 (DC2)", true  },     // pin 27, **DC2 OUTPUT**
    // PA29 doesn't exist
    { -1, 0, 30,  "PA30 (SWCLK)", false },   // SWD — skip
    { -1, 0, 31,  "PA31 (SWDIO)", false },   // SWD — skip

    // Port B — Arduino Zero pin numbers
    { -1, 1,  0,  "PB00", true  },           // Not mapped on Zero
    { -1, 1,  1,  "PB01", true  },           // Not mapped on Zero
    { 19, 1,  2,  "PB02 (A5)", true  },      // A5 = pin 19
    { 25, 1,  3,  "PB03 (RX_LED)", true },   // RX_LED = pin 25
    { 15, 1,  8,  "PB08 (A1)", true  },      // A1 = pin 15
    { 16, 1,  9,  "PB09 (A2/NTC)", true },   // A2 = pin 16, **NTC SENSOR**
    { 23, 1, 10,  "PB10 (MOSI)", true },     // MOSI = pin 23
    { 24, 1, 11,  "PB11 (SCK)", true  },     // SCK = pin 24
    { 30, 1, 22,  "PB22 (ESP_TX)", true },   // Serial5 TX = pin 30, ESP8266
    { 31, 1, 23,  "PB23 (ESP_RX)", true },   // Serial5 RX = pin 31, ESP8266
};

static const int NUM_PINS = sizeof(ALL_PINS) / sizeof(ALL_PINS[0]);

// ─── Pin raw port access (for pins without Arduino mapping) ─────────────────
static void rawSetOutput(uint8_t port, uint8_t bit) {
    if (port == 0)      PORT->Group[0].DIRSET.reg = (1ul << bit);
    else if (port == 1) PORT->Group[1].DIRSET.reg = (1ul << bit);
}

static void rawSetInput(uint8_t port, uint8_t bit) {
    if (port == 0)      PORT->Group[0].DIRCLR.reg = (1ul << bit);
    else if (port == 1) PORT->Group[1].DIRCLR.reg = (1ul << bit);
}

static void rawWrite(uint8_t port, uint8_t bit, bool val) {
    if (val) {
        PORT->Group[port].OUTSET.reg = (1ul << bit);
    } else {
        PORT->Group[port].OUTCLR.reg = (1ul << bit);
    }
}

static bool rawRead(uint8_t port, uint8_t bit) {
    return (PORT->Group[port].IN.reg >> bit) & 1;
}

// ─── Serial command buffer ──────────────────────────────────────────────────
static char cmdBuf[128];
static int cmdPos = 0;

// ─── Find pin by name (e.g., "PA06", "PB10") ───────────────────────────────
static int findPin(const char* name) {
    for (int i = 0; i < NUM_PINS; i++) {
        if (strcasecmp(ALL_PINS[i].name, name) == 0) return i;
        // Also match just "PA06" prefix from longer names like "PA06 (something)"
        int len = strlen(name);
        if (strncasecmp(ALL_PINS[i].name, name, len) == 0 &&
            (ALL_PINS[i].name[len] == '\0' || ALL_PINS[i].name[len] == ' ')) {
            return i;
        }
    }
    return -1;
}

// ─── Commands ───────────────────────────────────────────────────────────────

static void cmdHelp() {
    SerialUSB.println();
    SerialUSB.println(F("╔══════════════════════════════════════════════════════╗"));
    SerialUSB.println(F("║   SmartPID CUBE — Pin Scanner Diagnostic Tool       ║"));
    SerialUSB.println(F("╠══════════════════════════════════════════════════════╣"));
    SerialUSB.println(F("║ help          Show this help                        ║"));
    SerialUSB.println(F("║ list          List all GPIO pins                    ║"));
    SerialUSB.println(F("║ scan          Toggle SAFE pins one-by-one (2s each) ║"));
    SerialUSB.println(F("║ set PA06      Set pin HIGH (push-pull output)       ║"));
    SerialUSB.println(F("║ clr PA06      Set pin LOW                           ║"));
    SerialUSB.println(F("║ float PA06    Set pin to input (high-Z)             ║"));
    SerialUSB.println(F("║ read PA06     Read digital state of pin             ║"));
    SerialUSB.println(F("║ readall       Read ALL pins (input mode snapshot)   ║"));
    SerialUSB.println(F("║ adc PA02      Read analog value (10-bit ADC)        ║"));
    SerialUSB.println(F("║ pulse PA06    Pulse pin 5 times (1Hz, for buzzer)   ║"));
    SerialUSB.println(F("║ pwm PA06 128  PWM output (0-255 duty cycle)        ║"));
    SerialUSB.println(F("║ alloff        Set all safe pins LOW + input         ║"));
    SerialUSB.println(F("║ uart          Scan for ESP8266 UART connection      ║"));
    SerialUSB.println(F("║ uarttx PA10 PA11  Send AT to ESP on given TX/RX    ║"));
    SerialUSB.println(F("║ i2cscan       Scan I2C bus (PA22=SDA, PA23=SCL)     ║"));
    SerialUSB.println(F("║ onewire       Scan all candidate pins for DS18B20   ║"));
    SerialUSB.println(F("║ owread PA09   Read DS18B20 on specific pin          ║"));
    SerialUSB.println(F("║ espinfo       Query ESP8266 config (version, WiFi)  ║"));
    SerialUSB.println(F("║ esp AT+GMR    Send arbitrary AT command to ESP8266  ║"));
    SerialUSB.println(F("╚══════════════════════════════════════════════════════╝"));
    SerialUSB.println();
    SerialUSB.println(F("SAFETY: Outputs are current-limited by the SAMD21."));
    SerialUSB.println(F("Use a multimeter to trace where each pin goes."));
    SerialUSB.println();
}

static void cmdList() {
    SerialUSB.println(F("\n--- All SAMD21G18 GPIOs (48-pin) ---"));
    for (int i = 0; i < NUM_PINS; i++) {
        spr("  %-22s  Arduino D%-3d  %s\n",
            ALL_PINS[i].name,
            ALL_PINS[i].arduinoPin,
            ALL_PINS[i].safe ? "SAFE" : "** SKIP **");
    }
    SerialUSB.println();
}

static void cmdScan() {
    SerialUSB.println(F("\n=== PIN SCAN: Toggling each safe pin HIGH for 2 seconds ==="));
    SerialUSB.println(F("Watch for relays clicking, LEDs, buzzer, OLED, etc."));
    SerialUSB.println(F("Send any character to abort.\n"));

    for (int i = 0; i < NUM_PINS; i++) {
        if (!ALL_PINS[i].safe) continue;

        spr(">>> %s (D%d) = HIGH ... ", ALL_PINS[i].name, ALL_PINS[i].arduinoPin);

        // Set as output HIGH
        if (ALL_PINS[i].arduinoPin >= 0) {
            pinMode(ALL_PINS[i].arduinoPin, OUTPUT);
            digitalWrite(ALL_PINS[i].arduinoPin, HIGH);
        } else {
            rawSetOutput(ALL_PINS[i].port, ALL_PINS[i].bit);
            rawWrite(ALL_PINS[i].port, ALL_PINS[i].bit, true);
        }

        // Wait 2 seconds (check for abort)
        unsigned long start = millis();
        bool aborted = false;
        while (millis() - start < 2000) {
            if (SerialUSB.available()) {
                while (SerialUSB.available()) SerialUSB.read();
                aborted = true;
                break;
            }
            delay(10);
        }

        // Set LOW and back to input
        if (ALL_PINS[i].arduinoPin >= 0) {
            digitalWrite(ALL_PINS[i].arduinoPin, LOW);
            pinMode(ALL_PINS[i].arduinoPin, INPUT);
        } else {
            rawWrite(ALL_PINS[i].port, ALL_PINS[i].bit, false);
            rawSetInput(ALL_PINS[i].port, ALL_PINS[i].bit);
        }

        SerialUSB.println("LOW");

        if (aborted) {
            SerialUSB.println(F("--- Scan aborted ---"));
            return;
        }
    }
    SerialUSB.println(F("=== Scan complete ===\n"));
}

static void cmdSet(const char* pinName) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }
    if (!ALL_PINS[idx].safe) { spr("%s is not safe to toggle!\n", ALL_PINS[idx].name); return; }

    if (ALL_PINS[idx].arduinoPin >= 0) {
        pinMode(ALL_PINS[idx].arduinoPin, OUTPUT);
        digitalWrite(ALL_PINS[idx].arduinoPin, HIGH);
    } else {
        rawSetOutput(ALL_PINS[idx].port, ALL_PINS[idx].bit);
        rawWrite(ALL_PINS[idx].port, ALL_PINS[idx].bit, true);
    }
    spr("%s = HIGH\n", ALL_PINS[idx].name);
}

static void cmdClr(const char* pinName) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }

    if (ALL_PINS[idx].arduinoPin >= 0) {
        pinMode(ALL_PINS[idx].arduinoPin, OUTPUT);
        digitalWrite(ALL_PINS[idx].arduinoPin, LOW);
    } else {
        rawSetOutput(ALL_PINS[idx].port, ALL_PINS[idx].bit);
        rawWrite(ALL_PINS[idx].port, ALL_PINS[idx].bit, false);
    }
    spr("%s = LOW\n", ALL_PINS[idx].name);
}

static void cmdFloat(const char* pinName) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }

    if (ALL_PINS[idx].arduinoPin >= 0) {
        pinMode(ALL_PINS[idx].arduinoPin, INPUT);
    } else {
        rawSetInput(ALL_PINS[idx].port, ALL_PINS[idx].bit);
    }
    spr("%s = INPUT (floating)\n", ALL_PINS[idx].name);
}

static void cmdRead(const char* pinName) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }

    // Read without changing direction
    bool val = rawRead(ALL_PINS[idx].port, ALL_PINS[idx].bit);
    spr("%s reads %s\n", ALL_PINS[idx].name, val ? "HIGH" : "LOW");
}

static void cmdReadAll() {
    SerialUSB.println(F("\n--- Reading ALL pins (current state) ---"));

    // First set all safe pins to input to read external state
    for (int i = 0; i < NUM_PINS; i++) {
        if (ALL_PINS[i].safe && ALL_PINS[i].arduinoPin >= 0) {
            pinMode(ALL_PINS[i].arduinoPin, INPUT_PULLUP);
        }
    }
    delay(10);  // Let pull-ups settle

    for (int i = 0; i < NUM_PINS; i++) {
        bool val = rawRead(ALL_PINS[i].port, ALL_PINS[i].bit);
        spr("  %-22s = %s\n", ALL_PINS[i].name, val ? "HIGH" : "LOW");
    }

    // Restore to floating
    for (int i = 0; i < NUM_PINS; i++) {
        if (ALL_PINS[i].safe && ALL_PINS[i].arduinoPin >= 0) {
            pinMode(ALL_PINS[i].arduinoPin, INPUT);
        }
    }
    SerialUSB.println();
}

static void cmdAdc(const char* pinName) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }
    if (ALL_PINS[idx].arduinoPin < 0) { SerialUSB.println("No Arduino mapping for ADC"); return; }

    pinMode(ALL_PINS[idx].arduinoPin, INPUT);
    int val = analogRead(ALL_PINS[idx].arduinoPin);
    float voltage = val * 3.3 / 1023.0;
    spr("%s ADC = %d (%.3fV)\n", ALL_PINS[idx].name, val, voltage);
}

static void cmdPulse(const char* pinName) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }
    if (!ALL_PINS[idx].safe) { spr("%s not safe!\n", ALL_PINS[idx].name); return; }

    spr("Pulsing %s 5x at 1Hz (listen for buzzer/click)...\n", ALL_PINS[idx].name);
    if (ALL_PINS[idx].arduinoPin >= 0) {
        pinMode(ALL_PINS[idx].arduinoPin, OUTPUT);
        for (int p = 0; p < 5; p++) {
            digitalWrite(ALL_PINS[idx].arduinoPin, HIGH);
            delay(100);
            digitalWrite(ALL_PINS[idx].arduinoPin, LOW);
            delay(400);
        }
        pinMode(ALL_PINS[idx].arduinoPin, INPUT);
    }
    SerialUSB.println("Done");
}

static void cmdPwm(const char* pinName, int duty) {
    int idx = findPin(pinName);
    if (idx < 0) { spr("Unknown pin: %s\n", pinName); return; }
    if (ALL_PINS[idx].arduinoPin < 0) { SerialUSB.println("No Arduino mapping for PWM"); return; }
    if (!ALL_PINS[idx].safe) { spr("%s not safe!\n", ALL_PINS[idx].name); return; }

    pinMode(ALL_PINS[idx].arduinoPin, OUTPUT);
    analogWrite(ALL_PINS[idx].arduinoPin, constrain(duty, 0, 255));
    spr("%s PWM duty = %d/255\n", ALL_PINS[idx].name, duty);
}

static void cmdAllOff() {
    SerialUSB.println(F("Setting all safe pins LOW + input..."));
    for (int i = 0; i < NUM_PINS; i++) {
        if (!ALL_PINS[i].safe) continue;
        if (ALL_PINS[i].arduinoPin >= 0) {
            digitalWrite(ALL_PINS[i].arduinoPin, LOW);
            pinMode(ALL_PINS[i].arduinoPin, INPUT);
        } else {
            rawWrite(ALL_PINS[i].port, ALL_PINS[i].bit, false);
            rawSetInput(ALL_PINS[i].port, ALL_PINS[i].bit);
        }
    }
    SerialUSB.println(F("All outputs OFF"));
}

// ─── UART scanner: try to find ESP8266 ──────────────────────────────────────
// The ESP8266 responds to "AT\r\n" with "OK" at 115200 baud (default)
// We try each pair of SERCOM-capable pins

static void cmdUartProbe(int txIdx, int rxIdx) {
    // Use Serial1 if available, otherwise bit-bang
    // For now, use the Arduino Serial1 which is typically PA10(TX)/PA11(RX)
    spr("Trying UART: TX=%s  RX=%s at 115200...\n",
        ALL_PINS[txIdx].name, ALL_PINS[rxIdx].name);

    // We can only easily use Serial1 (SERCOM0) which is on specific pins
    // For arbitrary pins, we'd need SoftwareSerial or manual SERCOM config
    // Start simple: just test Serial1
    Serial1.begin(115200);
    delay(100);

    // Flush
    while (Serial1.available()) Serial1.read();

    // Send AT command
    Serial1.print("AT\r\n");
    delay(500);

    // Read response
    String resp = "";
    while (Serial1.available()) {
        resp += (char)Serial1.read();
    }

    if (resp.length() > 0) {
        spr("  Response (%d bytes): ", resp.length());
        SerialUSB.println(resp);
        if (resp.indexOf("OK") >= 0) {
            SerialUSB.println(F("  *** ESP8266 FOUND! ***"));
        }
    } else {
        SerialUSB.println(F("  No response"));
    }
    Serial1.end();
}

static void cmdUartScan() {
    SerialUSB.println(F("\n=== ESP8266 UART Scanner ==="));
    SerialUSB.println(F("Testing Serial1 (default pins) with AT command..."));
    SerialUSB.println(F("If no response, use 'uarttx PAxx PAyy' to try specific pins.\n"));

    // Try Serial1 default pins
    Serial1.begin(115200);
    delay(200);
    while (Serial1.available()) Serial1.read();

    // Try different baud rates
    long bauds[] = {115200, 9600, 74880, 57600, 38400, 19200};
    for (int b = 0; b < 6; b++) {
        Serial1.end();
        Serial1.begin(bauds[b]);
        delay(100);
        while (Serial1.available()) Serial1.read();

        Serial1.print("AT\r\n");
        delay(500);

        String resp = "";
        unsigned long start = millis();
        while (millis() - start < 500) {
            if (Serial1.available()) resp += (char)Serial1.read();
        }

        spr("  %ld baud: ", bauds[b]);
        if (resp.length() > 0) {
            spr("Got %d bytes: ", resp.length());
            // Print printable chars, hex for others
            for (unsigned int c = 0; c < resp.length() && c < 60; c++) {
                if (resp[c] >= 32 && resp[c] < 127) SerialUSB.print(resp[c]);
                else spr("[%02X]", (uint8_t)resp[c]);
            }
            SerialUSB.println();
            if (resp.indexOf("OK") >= 0) {
                spr("  *** ESP8266 FOUND at %ld baud! ***\n", bauds[b]);
                Serial1.end();
                return;
            }
        } else {
            SerialUSB.println(F("no response"));
        }
    }
    Serial1.end();
    SerialUSB.println(F("\nNo ESP8266 found on Serial1 default pins."));
    SerialUSB.println(F("Try 'uarttx PAxx PAyy' with different pin pairs."));
    SerialUSB.println();
}

// ─── DS18B20 OneWire scanner ────────────────────────────────────────────────
// Candidate pins for the OneWire data line (unknown from RE)
static const int OW_CANDIDATES[] = { 3, 4, 5, 11, 12, 13, 15, 18, 19 };
static const char* OW_CANDIDATE_NAMES[] = {
    "D3/PA09", "D4/PA08", "D5/PA15", "D11/PA16", "D12/PA19", "D13/PA17",
    "D15/PB08(A1)", "D18/PA05(A4)", "D19/PB02(A5)"
};
static const int OW_NUM_CANDIDATES = sizeof(OW_CANDIDATES) / sizeof(OW_CANDIDATES[0]);

static void cmdOneWireScan() {
    SerialUSB.println(F("\n=== DS18B20 OneWire Pin Scanner ==="));
    SerialUSB.println(F("Testing each candidate pin for OneWire devices...\n"));

    int totalFound = 0;
    for (int c = 0; c < OW_NUM_CANDIDATES; c++) {
        int pin = OW_CANDIDATES[c];
        spr("  Pin %s (Arduino D%d): ", OW_CANDIDATE_NAMES[c], pin);

        OneWire ow(pin);
        DallasTemperature sensors(&ow);
        sensors.begin();
        int count = sensors.getDeviceCount();

        if (count > 0) {
            spr("*** FOUND %d sensor(s)! ***\n", count);
            totalFound += count;

            // Read addresses and temperatures
            sensors.requestTemperatures();
            delay(800);  // Wait for conversion

            for (int i = 0; i < count && i < 4; i++) {
                DeviceAddress addr;
                if (sensors.getAddress(addr, i)) {
                    spr("    Sensor %d addr: ", i);
                    for (int b = 0; b < 8; b++) spr("%02X", addr[b]);
                    float tempC = sensors.getTempCByIndex(i);
                    spr("  Temp: %.2f C (%.2f F)\n", tempC, tempC * 9.0 / 5.0 + 32.0);
                }
            }
        } else {
            SerialUSB.println(F("no devices"));
        }

        // Reset pin to input
        pinMode(pin, INPUT);
    }

    spr("\nTotal DS18B20 sensors found: %d\n\n", totalFound);
    if (totalFound == 0) {
        SerialUSB.println(F("No DS18B20 found. Check:"));
        SerialUSB.println(F("  - Is a DS18B20 probe connected?"));
        SerialUSB.println(F("  - Does it have a 4.7K pull-up resistor on data line?"));
        SerialUSB.println(F("  - Is 12V power required for the sensor circuit?\n"));
    }
}

static void cmdOneWireRead(const char* pinName) {
    int idx = findPin(pinName);
    int pin = -1;
    if (idx >= 0) pin = ALL_PINS[idx].arduinoPin;
    if (pin < 0) {
        // Try direct number
        pin = atoi(pinName);
        if (pin <= 0 || pin > 31) {
            spr("Invalid pin: %s\n", pinName);
            return;
        }
    }

    spr("Reading OneWire on Arduino pin %d...\n", pin);
    OneWire ow(pin);
    DallasTemperature sensors(&ow);
    sensors.begin();
    int count = sensors.getDeviceCount();
    if (count == 0) {
        SerialUSB.println(F("No DS18B20 found on this pin"));
        pinMode(pin, INPUT);
        return;
    }
    spr("Found %d sensor(s)\n", count);
    sensors.requestTemperatures();
    delay(800);
    for (int i = 0; i < count && i < 8; i++) {
        DeviceAddress addr;
        if (sensors.getAddress(addr, i)) {
            spr("  Sensor %d: ", i);
            for (int b = 0; b < 8; b++) spr("%02X", addr[b]);
            float tempC = sensors.getTempCByIndex(i);
            spr("  %.2f C (%.2f F)\n", tempC, tempC * 9.0 / 5.0 + 32.0);
        }
    }
    pinMode(pin, INPUT);
}

// ─── ESP8266 AT command passthrough ─────────────────────────────────────────

static void espSendAT(const char* atCmd, int waitMs = 1000) {
    Serial1.begin(57600);
    delay(100);
    while (Serial1.available()) Serial1.read();  // flush

    spr(">> %s\n", atCmd);
    Serial1.print(atCmd);
    Serial1.print("\r\n");

    unsigned long start = millis();
    String resp = "";
    while (millis() - start < (unsigned long)waitMs) {
        while (Serial1.available()) {
            char c = Serial1.read();
            resp += c;
        }
        delay(1);
    }

    if (resp.length() > 0) {
        // Print response, showing non-printable chars as hex
        for (unsigned int i = 0; i < resp.length(); i++) {
            char c = resp[i];
            if (c == '\r') continue;
            if (c == '\n' || (c >= 32 && c < 127)) SerialUSB.print(c);
            else spr("[%02X]", (uint8_t)c);
        }
        if (resp[resp.length()-1] != '\n') SerialUSB.println();
    } else {
        SerialUSB.println(F("(no response)"));
    }
}

static void cmdEsp(const char* fullCmd) {
    // Send arbitrary AT command to ESP8266
    if (!fullCmd || strlen(fullCmd) == 0) {
        SerialUSB.println(F("Usage: esp AT+GMR  (send any AT command)"));
        return;
    }
    espSendAT(fullCmd, 2000);
    Serial1.end();
}

static void cmdEspInfo() {
    SerialUSB.println(F("\n=== ESP8266 Configuration Info ==="));
    SerialUSB.println(F("(57600 baud on Serial1)\n"));

    SerialUSB.println(F("--- Firmware Version ---"));
    espSendAT("AT+GMR", 1000);

    SerialUSB.println(F("\n--- WiFi Mode ---"));
    espSendAT("AT+CWMODE?", 1000);

    SerialUSB.println(F("\n--- Current AP Connection ---"));
    espSendAT("AT+CWJAP?", 2000);

    SerialUSB.println(F("\n--- IP Address ---"));
    espSendAT("AT+CIFSR", 1000);

    SerialUSB.println(F("\n--- UART Config ---"));
    espSendAT("AT+UART_CUR?", 1000);

    SerialUSB.println(F("\n--- DHCP Status ---"));
    espSendAT("AT+CWDHCP?", 1000);

    SerialUSB.println(F("\n--- Multiplexing Mode ---"));
    espSendAT("AT+CIPMUX?", 1000);

    SerialUSB.println(F("\n--- Available APs (scanning ~5s) ---"));
    espSendAT("AT+CWLAP", 8000);

    Serial1.end();
    SerialUSB.println(F("\n=== Done ==="));
}

// ─── I2C scanner (for OLED, EEPROM) ────────────────────────────────────────
#include <Wire.h>

static void cmdI2CScan() {
    SerialUSB.println(F("\n=== I2C Bus Scan (SDA=PA22, SCL=PA23) ==="));
    Wire.begin();
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            spr("  Found device at 0x%02X", addr);
            if (addr == 0x3C || addr == 0x3D) SerialUSB.print(" (OLED SSD1306/SH1106)");
            if (addr == 0x50) SerialUSB.print(" (EEPROM 24Cxx)");
            if (addr == 0x48) SerialUSB.print(" (Temp sensor / ADC)");
            if (addr == 0x68 || addr == 0x69) SerialUSB.print(" (RTC / IMU)");
            SerialUSB.println();
            found++;
        }
    }
    Wire.end();
    if (found == 0) SerialUSB.println(F("  No I2C devices found"));
    spr("\n%d device(s) found\n\n", found);
}

// ─── Parse and execute command ──────────────────────────────────────────────
static void processCommand(char* cmd) {
    // Trim whitespace
    while (*cmd == ' ') cmd++;
    char* end = cmd + strlen(cmd) - 1;
    while (end > cmd && (*end == ' ' || *end == '\r' || *end == '\n')) { *end = 0; end--; }
    if (strlen(cmd) == 0) return;

    // Tokenize
    char* tok1 = strtok(cmd, " ");
    char* tok2 = strtok(NULL, " ");
    char* tok3 = strtok(NULL, " ");

    if (strcasecmp(tok1, "help") == 0)         cmdHelp();
    else if (strcasecmp(tok1, "list") == 0)     cmdList();
    else if (strcasecmp(tok1, "scan") == 0)     cmdScan();
    else if (strcasecmp(tok1, "readall") == 0)  cmdReadAll();
    else if (strcasecmp(tok1, "alloff") == 0)   cmdAllOff();
    else if (strcasecmp(tok1, "uart") == 0)     cmdUartScan();
    else if (strcasecmp(tok1, "i2cscan") == 0)  cmdI2CScan();
    else if (strcasecmp(tok1, "onewire") == 0)  cmdOneWireScan();
    else if (strcasecmp(tok1, "owread") == 0 && tok2) cmdOneWireRead(tok2);
    else if (strcasecmp(tok1, "espinfo") == 0)  cmdEspInfo();
    else if (strcasecmp(tok1, "esp") == 0 && tok2) {
        // Reconstruct the full AT command from remaining tokens
        char atBuf[128];
        strncpy(atBuf, tok2, sizeof(atBuf)-1);
        atBuf[sizeof(atBuf)-1] = '\0';
        if (tok3) { strncat(atBuf, " ", sizeof(atBuf)-strlen(atBuf)-1); strncat(atBuf, tok3, sizeof(atBuf)-strlen(atBuf)-1); }
        cmdEsp(atBuf);
    }
    else if (strcasecmp(tok1, "set") == 0 && tok2)    cmdSet(tok2);
    else if (strcasecmp(tok1, "clr") == 0 && tok2)    cmdClr(tok2);
    else if (strcasecmp(tok1, "float") == 0 && tok2)  cmdFloat(tok2);
    else if (strcasecmp(tok1, "read") == 0 && tok2)   cmdRead(tok2);
    else if (strcasecmp(tok1, "adc") == 0 && tok2)    cmdAdc(tok2);
    else if (strcasecmp(tok1, "pulse") == 0 && tok2)  cmdPulse(tok2);
    else if (strcasecmp(tok1, "pwm") == 0 && tok2) {
        int duty = tok3 ? atoi(tok3) : 128;
        cmdPwm(tok2, duty);
    }
    else if (strcasecmp(tok1, "uarttx") == 0 && tok2 && tok3) {
        int txIdx = findPin(tok2);
        int rxIdx = findPin(tok3);
        if (txIdx >= 0 && rxIdx >= 0) cmdUartProbe(txIdx, rxIdx);
        else SerialUSB.println(F("Unknown pin(s)"));
    }
    else {
        spr("Unknown command: %s (type 'help')\n", tok1);
    }

    SerialUSB.print(F("diag> "));
}

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup() {
    SerialUSB.begin(115200);

    // Wait for USB serial connection (up to 5 seconds)
    unsigned long start = millis();
    while (!SerialUSB && millis() - start < 5000) {
        delay(10);
    }
    delay(500);

    // Quick LED blink to show we're alive (if LED exists)
    pinMode(LED_BUILTIN, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH); delay(100);
        digitalWrite(LED_BUILTIN, LOW);  delay(100);
    }

    SerialUSB.println();
    SerialUSB.println(F("╔══════════════════════════════════════════════════════╗"));
    SerialUSB.println(F("║   SmartPID CUBE — Pin Scanner v1.0                  ║"));
    SerialUSB.println(F("║   ATSAMD21G18 Reverse Engineering Tool              ║"));
    SerialUSB.println(F("╠══════════════════════════════════════════════════════╣"));
    SerialUSB.println(F("║   Type 'help' for commands                          ║"));
    SerialUSB.println(F("║   Type 'scan' to toggle all pins one by one         ║"));
    SerialUSB.println(F("║   Type 'i2cscan' to find OLED/EEPROM               ║"));
    SerialUSB.println(F("║   Type 'uart' to find ESP8266 serial link           ║"));
    SerialUSB.println(F("╚══════════════════════════════════════════════════════╝"));
    SerialUSB.println();
    SerialUSB.print(F("diag> "));
}

// ─── Main Loop ──────────────────────────────────────────────────────────────
void loop() {
    while (SerialUSB.available()) {
        char c = SerialUSB.read();
        if (c == '\n' || c == '\r') {
            if (cmdPos > 0) {
                SerialUSB.println();
                cmdBuf[cmdPos] = '\0';
                processCommand(cmdBuf);
                cmdPos = 0;
            }
        } else if (cmdPos < (int)sizeof(cmdBuf) - 1) {
            cmdBuf[cmdPos++] = c;
            SerialUSB.print(c);  // Echo
        }
    }
}
