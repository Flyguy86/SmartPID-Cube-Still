# SmartPID Still Controller

Custom firmware for the **SmartPID CUBE** hardware, repurposed as a beer/spirits distillation controller with web-based monitoring and control.

## Hardware

| Component | Chip | Role |
|-----------|------|------|
| Main Controller | ATSAMD21G18 (ARM Cortex-M0+, 48 MHz, 256KB flash, 32KB RAM) | Sensors, PID, outputs, scheduling |
| WiFi Co-Processor | ESP-WROOM-02 (ESP8266) | HTTP server via AT commands over UART |
| Communication | Serial1 @ 115200 baud | SAMD21 ↔ ESP8266 bridge |

### I/O

- **2× DS18B20** temperature probes (lower + upper, OneWire, 12-bit async)
- **SSR** with hardware PWM (TC3) for PID-controlled heating
- **2× Relay** outputs (on/off)
- **2× DC** outputs
- **Piezo buzzer** for status tones
- **4 buttons** (Up/Down/Select/Start-Stop)
- **OLED display** (I2C, 0x3C) — not yet implemented in this firmware

### Bootloader & Flashing

The SmartPID CUBE uses a **custom Arzaman "SPC1000 MSD" bootloader** (16KB, 0x0000–0x3FFF). Application code starts at **0x4000**, giving a 240KB (245,760 byte) app region.

When activated, the bootloader exposes a tiny **FAT12 virtual USB drive** (484 blocks × 512 bytes = ~242KB). This drive contains a single file `FLASH.BIN` which represents the raw contents of the application flash region. The disk layout is:

| Block(s) | Contents |
|----------|----------|
| 0 | FAT12 boot sector (filesystem metadata) |
| 1 | FAT #1 (file allocation table) |
| 2 | FAT #2 (copy) |
| 3 | Root directory (contains the `FLASH.BIN` entry) |
| **4–483** | **Data area — 480 blocks = 245,760 bytes = the app flash region** |

The bootloader intercepts raw USB block writes to the data area (blocks 4–483) and programs them directly into the SAMD21's internal flash starting at 0x4000. This means:

- **You cannot simply copy a `.bin` file to the mounted drive.** Although the drive mounts and appears to accept files, the bootloader programs flash based on *which physical disk blocks are written*, not the file contents. A filesystem-level file copy writes data to whatever blocks the FAT driver allocates, which may not start at block 4 or be contiguous. The result is firmware bytes landing at wrong flash addresses, producing a non-bootable image.
- **You must use `dd` to write the firmware binary directly to the raw block device** at `seek=4` (the start of the data area), bypassing the filesystem entirely. This ensures byte 0 of the firmware lands at flash address 0x4000, byte 1 at 0x4001, etc.
- **The binary must be padded to exactly 245,760 bytes** (with `0xFF`, the erased-flash state) to fill the entire app region. If the image is shorter than 245,760 bytes, the remaining flash may retain stale data from a previous firmware, which can cause unpredictable behavior.

#### Flashing Procedure

**Prerequisites:** The board requires **external 12V power** — USB alone does not power the SAMD21 or peripherals.

**Step 1 — Build the firmware**

```bash
pio run -e samd21_still
```

The output binary is at `.pio/build/samd21_still/firmware.bin`.

**Step 2 — Enter bootloader mode**

Hold the **Start/Stop (S/S) button** while connecting the USB cable (with 12V power already applied). The device will enumerate as a USB mass storage drive named `SmartPID` (typically `/dev/sdb` on Linux).

**Step 3 — Unmount the virtual drive**

The OS will auto-mount the FAT12 volume. You must unmount it so `dd` can write to the raw block device:

```bash
sudo umount /media/$USER/SmartPID
```

**Step 4 — Pad the binary to the full app region size (245,760 bytes)**

The compiled firmware is typically much smaller than the full app region. Pad it with `0xFF` bytes (the erased-flash state for NOR flash):

```bash
FW=.pio/build/samd21_still/firmware.bin
PADDED=/tmp/firmware_padded.bin
APP_SIZE=245760

cp "$FW" "$PADDED"
FWSIZE=$(stat -c%s "$FW")
dd if=/dev/zero bs=1 count=$((APP_SIZE - FWSIZE)) 2>/dev/null \
  | tr '\0' '\377' \
  | dd of="$PADDED" bs=1 seek=$FWSIZE conv=notrunc 2>/dev/null

# Verify: must be exactly 245760 bytes
ls -la "$PADDED"
```

**Step 5 — Write to the raw block device at the data area offset**

Write the padded image starting at **block 4** (the FAT12 data area, where `FLASH.BIN` content maps to app flash):

```bash
sudo dd if=/tmp/firmware_padded.bin of=/dev/sdb bs=512 seek=4 conv=notrunc
sync
```

You should see `480+0 records in / 480+0 records out` (480 × 512 = 245,760 bytes).

> **⚠ WARNING:** Do NOT use `seek=0` or `seek=1` — this would overwrite the FAT12 boot sector or FAT tables, corrupting the virtual drive and placing firmware bytes at the wrong flash addresses. The data area begins at block 4.

**Step 6 — Power cycle**

Disconnect USB, then power cycle the 12V supply (do NOT hold S/S). The bootloader will detect a valid application at 0x4000 and jump to it.

#### One-Liner (for repeat flashing)

```bash
pio run -e samd21_still && \
FW=.pio/build/samd21_still/firmware.bin && \
PADDED=/tmp/firmware_padded.bin && \
cp "$FW" "$PADDED" && \
FWSIZE=$(stat -c%s "$FW") && \
dd if=/dev/zero bs=1 count=$((245760 - FWSIZE)) 2>/dev/null | tr '\0' '\377' | \
dd of="$PADDED" bs=1 seek=$FWSIZE conv=notrunc 2>/dev/null && \
sudo umount /media/$USER/SmartPID 2>/dev/null; \
sudo dd if="$PADDED" of=/dev/sdb bs=512 seek=4 conv=notrunc && \
sync && echo "Flash complete — power cycle now"
```

#### Why Not Other Methods?

| Method | Why it fails |
|--------|--------------|
| **File copy** (`cp firmware.bin /media/.../SmartPID/`) | FAT driver allocates arbitrary disk blocks; firmware bytes land at wrong flash addresses |
| **`pio run -t upload`** | PlatformIO uses `bossac` which speaks SAM-BA protocol; this bootloader is MSD-based, not SAM-BA |
| **`dd seek=0`** | Overwrites the FAT12 boot sector — bootloader can't find the data area on next activation |
| **`dd seek=1`** | Writes to FAT table blocks, not the data area — firmware vector table ends up at the wrong flash offset |

## Features

### Multi-Step Distillation Profiles
- Up to **8 sequential steps**, each with independent target temperature, hold time, sensor selection, and max PWM
- Automatic step advancement with buzzer notifications
- Safety shutoff on sensor disconnect

### PID Temperature Control
- Independent PID parameters (Kp, Ki, Kd) per sensor
- Derivative-on-measurement (avoids setpoint kick)
- Integral anti-windup clamping
- Configurable output routing (any sensor → any output)

### PID Auto-Tune
- Relay-based Ziegler-Nichols oscillation method
- Runs bang-bang control around current temperature
- Detects 4 full oscillation cycles, calculates Ku and Tu
- Applies "no overshoot" ZN tuning variant
- 30-minute timeout safety

### Web Interface
- **Dashboard** (`/`) — live sensor readings, output toggles, SSR PWM slider, run profile status
- **Settings** (`/settings`) — WiFi network config, PID tuning per sensor, profile step editor, auto-tune trigger
- Mobile-first dark theme, minimal HTML for fast transfer
- WiFi scan with network picker

### WiFi Modes
- **AP mode** (default): Creates open network `SmartPID-Still`, accessible at `192.168.4.1`
- **STA+AP mode**: Joins a saved home network while keeping the AP as fallback
- Credentials persisted in flash

### Persistent Storage
- All settings (WiFi, PID params, profile steps) stored in SAMD21 internal flash via FlashStorage library
- Survives power cycles, validated with magic number

## Architecture

### Cooperative Priority Scheduler

The single-core SAMD21 runs a non-preemptive priority scheduler that ensures safety-critical tasks are never starved, even during long blocking WiFi AT command sequences.

| Priority | Level | Tasks | Interval |
|----------|-------|-------|----------|
| 0 — CRITICAL | Safety | `updateSensors`, `updatePID` | Every cycle |
| 1 — HIGH | User experience | `wifiPoll` | Every cycle |
| 2 — NORMAL | Feature | `updateAutoTune` | Every cycle |
| 3 — LOW | Diagnostics | `debugPrint` (5s), `schedulerPrintStats` (30s) | Timed |

**Key mechanism:** All blocking WiFi wait loops (`espCmd`, `espSendChunk`) call `yieldCritical()` when no serial data is available. This runs the sensor and PID tasks inline — the PID loop never misses its 1-second window even during multi-chunk HTML page transfers.

### Optimized WiFi Communication

The ESP8266 AT command interface is inherently slow (each `CIPSEND` requires a multi-step handshake over serial). Several optimizations minimize serial traffic:

| Optimization | Impact |
|-------------|--------|
| **Binary status protocol** — `/api/status` sends a 17-byte packed struct instead of ~150 bytes of JSON | ~9× less data per poll |
| **115200 baud** serial (auto-negotiated from 57600) | 2× throughput |
| **2048-byte chunks** (up from 512) | 4× fewer AT round trips per page |
| **Combined header+body** — JSON/binary responses fit in one `CIPSEND` | Halves AT overhead for API calls |
| **Cache-Control headers** on static HTML pages (5 min) | Eliminates repeat page downloads |
| **Time-budgeted polling** — `wifiPoll` processes max 128 bytes per call | Prevents serial flood from blocking |
| **3-second poll interval** on dashboard | 33% fewer requests vs 2s |

#### Binary Status Packet (17 bytes, little-endian)

```
Offset  Type     Field
0-1     int16    temp0        (°F × 10)
2-3     int16    temp1        (°F × 10)
4       uint8    flags        bit0-1: connected, bit2-6: outputs
5       uint8    ssrPWM       (0-255)
6       uint8    runState     (0=IDLE, 1=HEAT, 2=HOLD, 3=DONE)
7-8     int16    targetTemp   (°F × 10)
9-10    int16    currentTemp  (°F × 10)
11-12   uint16   holdRemSec   (seconds remaining)
13      uint8    maxPWM
14      uint8    currentStep
15      uint8    totalSteps
16      int8     autoTune     (-1 = inactive)
```

### HTTP API

| Endpoint | Method | Format | Description |
|----------|--------|--------|-------------|
| `/` | GET | HTML | Dashboard page (cached 5 min) |
| `/settings` | GET | HTML | Settings page (cached 5 min) |
| `/api/status` | GET | Binary | 17-byte status packet (polled every 3s) |
| `/api/settings` | GET | JSON | Full settings dump |
| `/api/output?id=N&s=0\|1` | GET | JSON | Toggle output N on/off |
| `/api/pwm?v=N` | GET | JSON | Set SSR PWM duty (0-255) |
| `/api/wifi?ssid=X&pass=Y` | GET | JSON | Save WiFi credentials + connect |
| `/api/pid?n=0\|1&kp=X&ki=X&kd=X&out=N` | GET | JSON | Save PID params for sensor N |
| `/api/profile?step=N&target=X&hold=M&sensor=S&maxpwm=P` | GET | JSON | Save profile step N |
| `/api/profile/add` | GET | JSON | Add a profile step |
| `/api/profile/del?step=N` | GET | JSON | Delete profile step N |
| `/api/start` | GET | JSON | Start running profile |
| `/api/stop` | GET | JSON | Stop profile + all outputs off |
| `/api/scan` | GET | JSON | Scan WiFi networks |
| `/api/autotune?n=0\|1` | GET | JSON | Toggle auto-tune for sensor N |

## Project Structure

```
src/
├── main.cpp          Main setup/loop, task registration
├── config.h          Pin definitions, structs, constants
├── scheduler.h/.cpp  Cooperative priority scheduler
├── sensors.h/.cpp    DS18B20 async temperature reading
├── outputs.h/.cpp    Output control, PID controller, auto-tune
├── storage.h/.cpp    Flash-persistent settings
├── wifi_server.h/.cpp  ESP8266 AT driver, HTTP server, API handlers
├── pages.h           HTML pages (dashboard + settings) in flash
diag/
├── pin_scanner.cpp   GPIO diagnostic scanner
├── extract_pinmap.py Pin map extraction from factory firmware
legacy/               Original factory firmware modules (reference)
```

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build production firmware
pio run -e samd21_still

# Build diagnostic pin scanner
pio run -e samd21_diag

# Serial monitor (115200 baud)
pio device monitor
```

For flashing, see the [Bootloader & Flashing](#bootloader--flashing) section above — `pio run -t upload` does not work with this bootloader.

### Build Stats

| | RAM | Flash |
|---|---|---|
| Production firmware | 6,100 / 32,768 bytes (18.6%) | 78,492 / 245,760 bytes (31.9%) |
| Diagnostic scanner | 4,360 / 32,768 bytes (13%) | 46,012 / 245,760 bytes (19%) |

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [OneWire](https://github.com/PaulStoffregen/OneWire) | ^2.3.8 | DS18B20 communication |
| [DallasTemperature](https://github.com/milesburton/DallasTemperature) | ^3.11.0 | DS18B20 high-level API |
| [FlashStorage](https://github.com/cmaglie/FlashStorage) | ^1.0.0 | SAMD21 internal flash persistence |

## Pin Map

See [PINMAP.md](PINMAP.md) for the complete reverse-engineered pin assignment table with disassembly evidence.

| Function | Arduino Pin | SAMD21 |
|----------|-------------|--------|
| DS18B20 Lower | 3 | PA09 |
| DS18B20 Upper | 4 | PA08 |
| SSR (digital) | 22 | PA12 |
| SSR (PWM) | 10 | PA18 |
| Relay 1 | 6 | PA20 |
| Relay 2 | 7 | PA21 |
| DC Out 1 | 26 | PA27 |
| DC Out 2 | 27 | PA28 |
| Buzzer | 2 | PA14 |
| Button Up | 14 | PA02 |
| Button Down | 17 | PA04 |
| Button Select | 8 | PA06 |
| Button S/S | 9 | PA07 |
| ESP8266 TX | 30 | PB22 |
| ESP8266 RX | 31 | PB23 |
