#!/usr/bin/env python3
"""Interactive pin scanner for SmartPID CUBE.
Walks through untested GPIO pins, asks you what happened after each one.
Supports both Serial (USB) and HTTP (WiFi) modes."""

import sys
import time


# ─── Transport: Serial (USB) ────────────────────────────────────────────────

class SerialTransport:
    def __init__(self, port="/dev/ttyACM0", baud=115200):
        import serial as _ser
        self.port_path = port
        self.baud = baud
        self.ser = None
        self._serial_mod = _ser

    def _open(self):
        if self.ser and self.ser.is_open:
            return True
        try:
            self.ser = self._serial_mod.Serial(self.port_path, self.baud, timeout=3)
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            return True
        except Exception as e:
            print(f"  Serial open failed: {e}")
            return False

    def _send_cmd(self, cmd, timeout=5):
        if not self._open():
            return None
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()
        start = time.time()
        lines = []
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                line = self.ser.readline().decode(errors='replace').strip()
                if line:
                    lines.append(line)
                    if line.startswith("OK"):
                        return line
            else:
                time.sleep(0.05)
        return lines[-1] if lines else None

    def check_online(self, timeout=30):
        print(f"  Checking {self.port_path} ...", end="", flush=True)
        start = time.time()
        while time.time() - start < timeout:
            if self._open():
                resp = self._send_cmd("status", timeout=3)
                if resp and "OK" in resp:
                    print(f" {resp}")
                    return True
            time.sleep(2)
            print(".", end="", flush=True)
        print(" TIMEOUT")
        return False

    def set_pin(self, pin, high=True):
        cmd = f"pin {pin}" if high else f"pinlo {pin}"
        resp = self._send_cmd(cmd, timeout=5)
        return resp is not None and "OK" in str(resp)

    def all_off(self):
        self._send_cmd("off", timeout=5)

    def name(self):
        return "Serial"


# ─── Transport: HTTP (WiFi) ─────────────────────────────────────────────────

class HTTPTransport:
    def __init__(self, host="192.168.8.133"):
        import requests as _req
        self._req = _req
        self.base = f"http://{host}"
        self.api = f"{self.base}/api/pintest"

    def check_online(self, timeout=60):
        print(f"  Checking {self.base} ...", end="", flush=True)
        start = time.time()
        while time.time() - start < timeout:
            try:
                r = self._req.get(f"{self.base}/api/status", timeout=15)
                if r.status_code == 200:
                    print(" OK!")
                    return True
            except Exception:
                pass
            time.sleep(5)
            print(".", end="", flush=True)
        print(" TIMEOUT")
        return False

    def set_pin(self, pin, high=True):
        try:
            params = {"p": pin}
            if not high:
                params["s"] = 0
            r = self._req.get(self.api, params=params, timeout=30)
            return r.status_code == 200
        except Exception:
            return False

    def all_off(self):
        try:
            self._req.get(self.api, params={"p": -1}, timeout=30)
        except Exception:
            pass

    def name(self):
        return "HTTP"


# ─── Pin map & known assignments ────────────────────────────────────────────

PIN_PORT = {
    0: "PA11", 1: "PA10", 2: "PA14", 3: "PA09", 4: "PA08",
    5: "PA15", 6: "PA20", 7: "PA21", 8: "PA06", 9: "PA07",
    10: "PA18", 11: "PA16", 12: "PA19", 13: "PA17",
    14: "PA02", 15: "PB08", 16: "PB09", 17: "PA04", 18: "PA05",
    19: "PB02", 20: "PA22", 21: "PA23", 22: "PA12",
    23: "PB10", 24: "PB11", 25: "PA27", 26: "PA28", 27: "PA13",
    30: "PA24", 31: "PA25",
}

KNOWN = {
    2:  "Buzzer",
    8:  "BTN_SELECT (freeze)",
    9:  "BTN_SS (freeze)",
    14: "BTN_UP (freeze)",
    17: "BTN_DOWN (freeze)",
    20: "I2C SCL (freeze)",
    21: "I2C SDA (freeze)",
    30: "Relay 1",
    31: "Relay 2",
}

TESTED_NO_EFFECT = {3, 4, 5, 6, 7, 10, 12, 13, 22}

UNTESTED = sorted(set(PIN_PORT.keys()) - set(KNOWN.keys()) - TESTED_NO_EFFECT)

RESPONSES = {
    '1': 'nothing',
    '2': 'SSR clicked/activated',
    '3': 'buzzer sounded',
    '4': 'relay clicked',
    '5': 'LED/light changed',
    '6': 'device froze',
    '7': 'other',
    's': 'skip',
    'q': 'quit',
}


def main():
    results = {}

    print("=" * 60)
    print("SmartPID Pin Scanner — Interactive Test")
    print("=" * 60)
    print()

    # Choose transport
    print("Connection mode:")
    print("  [S] Serial USB (/dev/ttyACM0)")
    print("  [H] HTTP WiFi (192.168.8.133)")
    tmode = input("Choose [S/H] (default S): ").strip().lower()

    if tmode == 'h':
        transport = HTTPTransport()
    else:
        port = input("Serial port (default /dev/ttyACM0): ").strip()
        if not port:
            port = "/dev/ttyACM0"
        transport = SerialTransport(port)

    print()
    print(f"Using {transport.name()} transport")
    print()

    # Known pins
    print("Known pins:")
    for pin in sorted(KNOWN):
        pname = PIN_PORT.get(pin, "???")
        print(f"  D{pin:2d} ({pname:5s}) = {KNOWN[pin]}")
    print()
    print(f"Already tested (no effect): {sorted(TESTED_NO_EFFECT)}")
    print()
    print(f"Pins to test: {UNTESTED}")
    print()

    # Check device
    if not transport.check_online():
        print("\n✗ Device not reachable.")
        print("  Make sure it's powered on and connected.")
        sys.exit(1)

    print()
    mode = input("Test mode — [H]IGH only, [L]OW only, or [B]oth? (default H): ").strip().lower()
    if mode not in ('l', 'b'):
        mode = 'h'

    print()
    print("For each pin, I'll set it and ask what happened.")
    print("Responses: 1=nothing 2=SSR 3=buzzer 4=relay 5=LED 6=froze 7=other s=skip q=quit")
    print("-" * 60)

    for pin in UNTESTED:
        pname = PIN_PORT.get(pin, "???")
        states_to_test = []
        if mode in ('h', 'b'):
            states_to_test.append(('HIGH', True))
        if mode in ('l', 'b'):
            states_to_test.append(('LOW', False))

        for state_name, high in states_to_test:
            print(f"\n>>> D{pin} ({pname}) — setting {state_name}...")

            ok = transport.set_pin(pin, high=high)

            if not ok:
                print("  ⚠ No response from device!")
                print("  Device may have frozen. Power cycle and press Enter...")
                input()
                transport.check_online(timeout=120)
                results[f"D{pin}_{state_name}"] = "freeze/no-response"
                continue

            resp = input(f"  What happened? [1-7/s/q]: ").strip().lower()

            if resp == 'q':
                transport.all_off()
                break

            result = RESPONSES.get(resp, f'custom: {resp}')
            results[f"D{pin}_{state_name}"] = result
            print(f"  → {result}")

            if resp == '7':
                note = input("  Describe: ").strip()
                results[f"D{pin}_{state_name}"] = f"other: {note}"

            # Turn off before next pin
            transport.all_off()
            time.sleep(0.3)

            if resp == '6':
                print("  Device froze — power cycle and press Enter...")
                input()
                transport.check_online(timeout=120)
        else:
            continue
        break  # quit

    # Summary
    print()
    print("=" * 60)
    print("RESULTS SUMMARY")
    print("=" * 60)
    for key, val in results.items():
        print(f"  {key:15s} = {val}")

    hits = {k: v for k, v in results.items() if v not in ('nothing', 'skip')}
    if hits:
        print()
        print("*** INTERESTING PINS ***")
        for key, val in hits.items():
            print(f"  {key:15s} = {val}")

    print()
    print("Done!")


if __name__ == "__main__":
    main()
