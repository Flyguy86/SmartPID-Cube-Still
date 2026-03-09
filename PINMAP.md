# SmartPID CUBE — Pin Map (Reverse-Engineered)

Extracted from factory firmware `SPC1000-biab-v1.3-010.bin` (162,468 bytes, BIAB v1.3)
via Capstone ARM Thumb disassembly of flash dump from live device.

## Architecture

| Component | Chip | Role |
|-----------|------|------|
| Main Controller | **ATSAMD21G18** (ARM Cortex-M0+, 48 MHz, 256KB flash, 32KB RAM) | All I/O, PID, display, buttons, outputs, sensors |
| WiFi Co-Processor | **ESP-WROOM-02** (ESP8266) | WiFi, MQTT, HTTP web UI via AT commands |
| Communication | UART serial bridge (SERCOM5, pins 30/31) | SAMD21 ↔ ESP8266 |

## Bootloader

- **Type**: MSD (Mass Storage Device) — custom Arzaman "SPC1000 MSD" bootloader
- **Size**: 16 KB (0x0000–0x3FFF)
- **Application start**: 0x4000
- **Power**: Requires external 12V — USB alone does NOT power the device
- **Flashing**: Must use raw `dd` writes to the USB block device — see [README.md](README.md) for the full procedure and explanation

## Pin Descriptor Table

The firmware uses the **standard Arduino Zero** pin mapping (40 entries × 24 bytes each).
Located at flash address 0x29EA8 (binary offset 0x25EA8).

---

## CONFIRMED Pin Assignments

### 5 Main Outputs (from board variant init at 0x154AC)

The firmware supports **two hardware revisions**. A board variant flag at RAM 0x20003D4C
selects the pin configuration. Output structs live at RAM 0x20002AA0 (5 × 24-byte structs,
pin number at byte 0 of each).

**Variant 0 — Standard CUBE (WiFi-capable, your board):**

| Output | Index | Arduino Pin | SAMD21 Port | Zero Name | Init Code |
|--------|-------|-------------|-------------|-----------|-----------|
| **SSR** | 0 | **22** | **PA12** | MISO | `movs r3, #0x16; strb r3, [r4]` at 0x154F4 |
| **DC1** | 1 | **26** | **PA27** | TX_LED | `movs r3, #0x1a; strb r3, [r4, #0x18]` at 0x154E8 |
| **DC2** | 2 | **27** | **PA28** | USB_HE | `movs r2, #0x1b; strb r2, [r3]` at 0x154F0 (offset +0x30) |
| **RL1** | 3 | **6** | **PA20** | D6 | `movs r2, #6; strb r2, [r3]` at 0x154DC (offset +0x48) |
| **RL2** | 4 | **7** | **PA21** | D7 | `movs r2, #7; strb r2, [r3]` at 0x154E4 (offset +0x60) |

**Variant 1 — Alternate revision (no WiFi, repurposes UART pins):**

| Output | Index | Arduino Pin | SAMD21 Port | Zero Name |
|--------|-------|-------------|-------------|-----------|
| SSR | 0 | 24 | PB11 | SCK |
| DC1 | 1 | 26 | PA27 | TX_LED |
| DC2 | 2 | 23 | PB10 | MOSI |
| RL1 | 3 | 30 | PB22 | TX5 |
| RL2 | 4 | 31 | PB23 | RX5 |

After pin assignment, the init loop (0x154F8–0x15524) iterates 5 times (stride 0x18),
calling setup and `digitalWrite(pin, 0)` to initialize all outputs LOW.

### Timer/PWM Output (from 0x100F4 → 0x140E2)

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **10** | **PA18/D10** | **TC3 PWM output** (PID-controlled SSR) | `outputSetup(0x20003440, 0x20003430, 10)` at 0x10146. TC3 base (0x40000800) loaded at 0x1011A. Sets NVIC interrupt, then `pinMode(10, OUTPUT)` at 0x140F4 |

This pin is independently configured with Timer/Counter 3 for hardware PWM, separate
from the 5 on/off outputs. Used for time-proportional PID control of the SSR heater.

### Buzzer (from 0x0DEB4)

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **2** | **PA14/D2** | **Buzzer** | `movs r0, #2; movs r1, #1; bl pinMode` at 0x0DEBE–0x0DEC2 |

### 4 Buttons (from button init at 0x1B5F4 → 0xC7C0)

All buttons configured with `INPUT_PULLUP` (mode 2). The init function at 0xC7C0 sets
debounce parameters: 150ms (0x96) short press, 1000ms (0xFA<<2) long press.

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **14** | **PA02/A0** | **Button 1** (UP/LEFT) | `movs r1, #0xe` at 0x1B610, callback struct at [r4+0] |
| **17** | **PA04/A3** | **Button 2** (DOWN/RIGHT) | `movs r1, #0x11` at 0x1B61E, callback struct at [r4+4] |
| **8** | **PA06/D8** | **Button 3** (SELECT/ENTER) | `movs r1, #8` at 0x1B62C, callback struct at [r4+8] |
| **9** | **PA07/D9** | **Button 4** (START/STOP) | `movs r1, #9` at 0x1B63A, callback struct at [r4+12] |

Note: Buttons use analog-capable pins (A0, A3) in digital mode. This is valid for SAMD21
since all GPIO pins support digital input regardless of analog capability.

### ESP8266 UART (SERCOM5)

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **30** | **PB22** | **ESP8266 UART TX** | SERCOM5 init; `Serial5.begin()` |
| **31** | **PB23** | **ESP8266 UART RX** | `pinMode(31, OUTPUT)` called 4x in serial init at 0x0734A–0x073BA |

### I2C Bus (SERCOM3) — OLED Display

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **20** | **PA22** | **I2C SDA** | Pin descriptor table: COM attribute; SERCOM3 |
| **21** | **PA23** | **I2C SCL** | Pin descriptor table: COM attribute; SERCOM3 |

OLED display: SSD1306 128×64, I2C address typically 0x3C.

### Temperature Sensors

Two probe types supported (user-selectable per channel):
- **DS18B20** — 1-Wire digital temperature sensor
- **NTC** — Analog thermistor (requires ADC + "NTC Beta" calibration value)

Two temperature channels: **Mash Probe** and **HLT Probe**

#### DS18B20 OneWire (Confirmed by physical probing)

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **3** | **PA09** | **DS18B20 Lower sensor input** | ✅ Physically confirmed — OneWire scan found sensor |
| **4** | **PA08** | **DS18B20 Upper sensor input** | ✅ Physically confirmed — OneWire scan found sensor |

#### NTC Analog

| Arduino Pin | SAMD21 Port | Function | Evidence |
|-------------|-------------|----------|----------|
| **16** | **PB09/A2** | **Primary analog sensor (NTC)** | ✅ `analogRead(16)` 5× in firmware, ADC=1018 (3.284V) confirmed |
| **18** | **PA05/A4** | **Secondary analog input** | Partial — 1 analogRead call at 0x0C546 |

### Startup Init Sequence (setup() at 0x042D0)

```
1. delay(1)                     ; 0x042D4
2. init1()                      ; 0x042D8 → 0x1B36C
3. init2()                      ; 0x042DC → 0x1B3D4
4. init3_buzzerArea()           ; 0x042E0 → 0x0DDD2
5. outputInit(5ch, variant)     ; 0x042E4 → 0x154AC  ← 5 main outputs
6. buzzerInit(0)                ; 0x042EA → 0x0DEB4  ← pin 2 OUTPUT
7. init4()                      ; 0x042EE → 0x1AB18
8. init5() → returns mode       ; 0x042F2 → 0xF890
9. if mode != 3: mainLoop()     ; 0x042FA → 0x04120
   else: configMode()           ; 0x04312 → 0x1498E
```

---

## Complete Pin Usage Summary (Variant 0)

| Pin | Port | Function | Direction | Confirmed |
|-----|------|----------|-----------|-----------|
| **0** | **PA11** | **Serial1 RX (ESP8266)** | INPUT | ✅ ESP AT OK at 57600 |
| **1** | **PA10** | **Serial1 TX (ESP8266)** | OUTPUT | ✅ ESP AT OK at 57600 |
| **2** | **PA14** | **Buzzer** | OUTPUT | ✅ Hardcoded |
| **3** | **PA09** | **DS18B20 Lower sensor input** | OneWire INPUT | ✅ Physical probe ||
| **4** | **PA08** | **DS18B20 Upper sensor input** | OneWire INPUT | ✅ Physical probe |
| 5 | PA15 | *Unknown* | — | ❌ |
| **6** | **PA20** | **RL1 (Relay 1)** | OUTPUT | ✅ Variant 0 |
| **7** | **PA21** | **RL2 (Relay 2)** | OUTPUT | ✅ Variant 0 |
| **8** | **PA06** | **Button 3 (SELECT)** | INPUT_PULLUP | ✅ Init at 0x1B62C |
| **9** | **PA07** | **Button 4 (START/STOP)** | INPUT_PULLUP | ✅ Init at 0x1B63A |
| **10** | **PA18** | **TC3 PWM (PID SSR)** | OUTPUT/PWM | ✅ TC3 + pinMode |
| 11 | PA16 | *Unknown / OneWire?* | — | ❌ |
| 12 | PA19 | *Unknown* | — | ❌ |
| 13 | PA17 | *Unknown* | — | ❌ |
| **14** | **PA02** | **Button 1 (UP)** | INPUT_PULLUP | ✅ Init at 0x1B610 |
| 15 | PB08 | *ADC/Sensor (possible)* | ANALOG | Partial |
| **16** | **PB09** | **NTC Analog Sensor** | ANALOG IN | ✅ 5× analogRead |
| **17** | **PA04** | **Button 2 (DOWN)** | INPUT_PULLUP | ✅ Init at 0x1B61E |
| 18 | PA05 | *Secondary ADC input* | ANALOG IN | Partial |
| 19 | PB02 | *Unknown* | — | ❌ |
| **20** | **PA22** | **I2C SDA (OLED)** | I2C | ✅ SERCOM3 |
| **21** | **PA23** | **I2C SCL (OLED)** | I2C | ✅ SERCOM3 |
| **22** | **PA12** | **SSR Output** | OUTPUT | ✅ Variant 0 |
| 23 | PB10 | *Unused (Variant 0)* | — | — |
| 24 | PB11 | *Unused (Variant 0)* | — | — |
| 25 | PB03 | *RX_LED* | — | ❌ |
| **26** | **PA27** | **DC1 Output** | OUTPUT | ✅ Variant 0 |
| **27** | **PA28** | **DC2 Output** | OUTPUT | ✅ Variant 0 |
| **30** | **PB22** | **ESP8266 UART TX** | UART | ✅ SERCOM5 |
| **31** | **PB23** | **ESP8266 UART RX** | UART | ✅ SERCOM5 |

---

## Key RAM Structures

| Address | Size | Description |
|---------|------|-------------|
| 0x20002AA0 | 5 × 24 bytes | Output channel structs (byte 0 = pin, byte 1 = enabled, etc.) |
| 0x20003430 | ~16 bytes | TC3/PWM config struct |
| 0x20003440 | ~8 bytes | TC3/PWM output struct (pin 10 + config ptr) |
| 0x20003D4C | 1 byte | Board variant flag (0 = standard, 1 = alt) |

## Key Function Addresses (flash)

| Address | Function |
|---------|----------|
| 0x042D0 | `setup()` — main init sequence |
| 0x05048 | `pinMode(pin, mode)` |
| 0x05130 | Pin control function (possibly `digitalWrite`) |
| 0x05180 | `digitalRead(pin)` |
| 0x0C7C0 | `buttonInit(struct, pin, callback, config)` |
| 0x0DEB4 | `buzzerInit(mode)` — pin 2 as OUTPUT |
| 0x0DF08 | Output write (analogWrite/digitalWrite) |
| 0x0E120 | `delay(ms)` |
| 0x140E2 | `outputSetup(dest_struct, config, pin)` — calls pinMode(OUTPUT) |
| 0x154AC | `outputInit()` — variant-based 5-channel output setup |
| 0x1B5F4 | Main button init — configures 4 buttons |
| 0x1BCC0 | `__gnu_thumb1_case_uqi` — GCC switch helper |

## What Still Needs Physical Probing

The diagnostic firmware (`diag/pin_scanner.cpp`) should verify:

1. ✅ ~~Which 5 pins drive the physical output terminals~~ → **FOUND: D22, D26, D27, D6, D7**
2. ✅ ~~Buzzer pin~~ → **FOUND: D2 (PA14)** — physically confirmed (audible)
3. ✅ ~~Button pins~~ → **FOUND: A0(14), A3(17), D8, D9**
4. ❌ **SD card SPI pins** — likely on SERCOM1 (D11/D12/D13) or absent
5. ✅ ~~DS18B20 OneWire data pins~~ → **FOUND: D3/PA09 (lower), D4/PA08 (upper)**
6. ✅ ~~ESP8266 UART~~ → **Serial1 (PB22 TX / PB23 RX) at 57600 baud**
7. ✅ ~~OLED display~~ → **I2C 0x3C on SDA=PA22 / SCL=PA23**
8. ❌ **Physical verification** — confirm outputs match the correct terminal labels (needs 12V)

### Diagnostic Firmware Probe Plan

Once 12V power is connected:

```
1. Flash pin_scanner.cpp via MSD bootloader
2. Run `i2cscan` — confirm OLED at 0x3C
3. Run `readall` — check button pin states
4. Press each button and `readall` to confirm pin 14/17/8/9 assignments
5. Run `set 22` — should activate SSR terminal
6. Run `set 26` — should activate DC1 terminal
7. Run `set 27` — should activate DC2 terminal
8. Run `set 6` — should activate Relay1 terminal
9. Run `set 7` — should activate Relay2 terminal
10. Run `pwm 10 128` — should produce PWM on SSR PWM channel
11. Run `set 2` then `tone` — confirm buzzer on pin 2
12. Connect DS18B20 probe, try `scan` on D3/D5/D11/D12/D13 to find OneWire
13. Run `adc 16` — check NTC thermistor reading on A2
```

## Firmware String Summary

Key operational strings found in the binary:

- **Brewing**: Mash In/Out, Phytase, Glucanase, Protease, B-Amylase, A-Amylase, Boil, Cooling
- **PID**: Kp/Ki/Kd for Mash and HLT, Auto Tune, OutputStep, NoiseBand, LookBackSec
- **Outputs**: Relay1, Relay2, SSR, DC1, DC2, PI, Pump Cycle/Rest, PWM Period
- **Sensors**: DS18B20, NTC, NTC Beta, Mash Probe, HLT Probe, Temperature Unit
- **Control**: Sound Alarms, Button Beep, Pump Stop Temp, Auto Resume, Hysteresis
- **Connectivity**: MQTT broker/port/user/pwd, WiFi SSID/Password, Serial Baud Rate
- **ESP8266**: AT commands (+UART_DEF), HTTP/1.1, TCP

## EEPROM Layout (from flash dump analysis)

EEPROM emulation uses last 16KB of flash (0x3C000–0x3FFFF). 256 pages × 64 bytes.

| Page | Content |
|------|---------|
| 0–3 | WiFi credentials, temperature setpoints (183.4°F, 170°F, 175.4°F) |
| 5 | IP configuration (192.168.8.50) |
| 8–11 | Channel config with PID params (Kp=7.381, Ki=0.095, setpoint=100°C) |
| 26–30 | Mash temperature profiles (BIAB recipes, step temps 29–80°C) |
| 255 | EEPROM marker: "EEtAMORP.umE" (reversed = EEPROM emulation tag) |
