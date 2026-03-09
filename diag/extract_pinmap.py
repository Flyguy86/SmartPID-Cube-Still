#!/usr/bin/env python3
"""
SmartPID CUBE — Firmware Pin Map Extractor
Analyze SAMD21G18 binary to find GPIO pin assignments, SERCOM config,
ADC channels, and other hardware configuration.
"""

import struct
import sys
import re
from collections import defaultdict

FIRMWARE = "/home/brian/SmartPID-Still/SPC1000-biab-v1.3-010.bin"

# SAMD21 memory-mapped peripheral base addresses
PORT_BASE    = 0x41004400  # GPIO PORT
PORTA_BASE   = 0x41004400
PORTB_BASE   = 0x41004480

# PORT register offsets
PORT_REGS = {
    0x00: "DIR",      # Direction (1=output)
    0x04: "DIRCLR",   # Direction clear
    0x08: "DIRSET",   # Direction set (make output)
    0x0C: "DIRTGL",   # Direction toggle
    0x10: "OUT",      # Output value
    0x14: "OUTCLR",   # Output clear (set LOW)
    0x18: "OUTSET",   # Output set (set HIGH)
    0x1C: "OUTTGL",   # Output toggle
    0x20: "IN",       # Input value
    0x24: "CTRL",     # Control (input sampling)
    0x28: "WRCONFIG", # Write configuration
    0x30: "PMUX0",    # Peripheral mux (pins 0-1)
    # PMUX continues every byte up to 0x3F
    0x40: "PINCFG0",  # Pin config (pin 0)
    # PINCFG continues every byte up to 0x5F
}

# SERCOM base addresses (for UART/I2C/SPI detection)
SERCOM_BASES = {
    0x42000800: "SERCOM0",
    0x42000C00: "SERCOM1",
    0x42001000: "SERCOM2",
    0x42001400: "SERCOM3",
    0x42001800: "SERCOM4",
    0x42001C00: "SERCOM5",
}

# SERCOM register offsets
SERCOM_REGS = {
    0x00: "CTRLA",
    0x04: "CTRLB",
    0x0C: "BAUD",
    0x18: "DATA",
    0x1A: "STATUS",
}

# ADC base address
ADC_BASE = 0x42004000
# TC (Timer/Counter) bases
TC_BASES = {
    0x42002000: "TC3",
    0x42002400: "TC4",
    0x42002800: "TC5",
}
TCC_BASES = {
    0x42002000: "TCC0",
    0x42002400: "TCC1",
    0x42002800: "TCC2",
}

# SAMD21 Pin mux functions
PMUX_FUNCS = {
    0: "A (EIC)",
    1: "B (Analog/REF)",
    2: "C (SERCOM)",
    3: "D (SERCOM-ALT)",
    4: "E (TC/TCC)",
    5: "F (TCC)",
    6: "G (COM)",
    7: "H (AC/GCLK)",
}

# Arduino Zero pin mapping (SAMD21G18 48-pin)
ARDUINO_PINS = {
    (0, 11): "D0 (Serial1 RX)",
    (0, 10): "D1 (Serial1 TX)",
    (0, 14): "D2",
    (0,  9): "D3 (PWM)",
    (0,  8): "D4",
    (0, 15): "D5 (PWM)",
    (0, 20): "D6 (PWM)",
    (0, 21): "D7",
    (0,  6): "D8 (PWM)",
    (0,  7): "D9 (PWM)",
    (0, 18): "D10 (SS, PWM)",
    (0, 16): "D11 (MOSI, PWM)",
    (0, 19): "D12 (MISO)",
    (0, 17): "D13 (SCK, LED)",
    (0,  2): "A0 (DAC)",
    (1,  8): "A1",
    (1,  9): "A2",
    (0,  4): "A3",
    (0,  5): "A4",
    (1,  2): "A5",
    (0, 22): "SDA",
    (0, 23): "SCL",
    (1, 10): "MOSI (SPI1)",
    (1, 11): "SCK (SPI1)",
    (0, 12): "MISO (SPI1)",
    (0, 13): "SS (SPI1)",
}


def load_binary(path):
    with open(path, "rb") as f:
        return f.read()


def find_32bit_constants(data):
    """Find all 32-bit values in the binary that look like SAMD21 peripheral addresses."""
    results = defaultdict(list)
    for i in range(0, len(data) - 3, 2):  # Thumb = 2-byte aligned
        val = struct.unpack_from("<I", data, i)[0]
        
        # Check PORT registers
        if PORTA_BASE <= val < PORTA_BASE + 0x60:
            offset = val - PORTA_BASE
            reg_name = PORT_REGS.get(offset, f"offset 0x{offset:02X}")
            results[f"PORTA.{reg_name}"].append((i, val))
        elif PORTB_BASE <= val < PORTB_BASE + 0x60:
            offset = val - PORTB_BASE
            reg_name = PORT_REGS.get(offset, f"offset 0x{offset:02X}")
            results[f"PORTB.{reg_name}"].append((i, val))
        
        # Check SERCOM addresses
        for base, name in SERCOM_BASES.items():
            if base <= val < base + 0x20:
                offset = val - base
                reg_name = SERCOM_REGS.get(offset, f"offset 0x{offset:02X}")
                results[f"{name}.{reg_name}"].append((i, val))
        
        # Check ADC
        if ADC_BASE <= val < ADC_BASE + 0x30:
            offset = val - ADC_BASE
            results[f"ADC.offset_0x{offset:02X}"].append((i, val))
    
    return results


def find_pin_bitmasks(data):
    """Look for common GPIO bitmask patterns near PORT register references."""
    results = []
    
    # Search for sequences that load a PORT address then a bitmask
    # In Cortex-M0+ Thumb, this is typically:
    #   LDR Rn, [PC, #offset]  -> loads PORT address
    #   LDR Rm, [PC, #offset]  -> loads bitmask
    #   STR Rm, [Rn, #0]       -> writes to register
    
    # Alternative: search for literal pool entries near PORT addresses
    for i in range(0, len(data) - 7, 4):
        val = struct.unpack_from("<I", data, i)[0]
        
        # Is this a PORT DIRSET, OUTSET, or OUTCLR address?
        for port_base, port_name in [(PORTA_BASE, "PA"), (PORTB_BASE, "PB")]:
            for reg_off, reg_name in [(0x08, "DIRSET"), (0x18, "OUTSET"), (0x14, "OUTCLR"), (0x00, "DIR")]:
                if val == port_base + reg_off:
                    # Look for bitmask values nearby (within ±64 bytes in literal pool)
                    for j in range(max(0, i - 64), min(len(data) - 3, i + 64), 4):
                        if j == i:
                            continue
                        mask = struct.unpack_from("<I", data, j)[0]
                        # Valid bitmask: single bit set, or reasonable multi-bit mask
                        if mask == 0 or mask > 0xFFFFFFFF:
                            continue
                        # Check if it's a power of 2 (single pin)
                        if mask & (mask - 1) == 0 and 1 <= mask <= (1 << 31):
                            bit = mask.bit_length() - 1
                            pin_name = f"{port_name}{bit:02d}"
                            arduino = ARDUINO_PINS.get((0 if port_name == "PA" else 1, bit), "")
                            results.append({
                                "addr": i,
                                "port": port_name,
                                "reg": reg_name,
                                "bit": bit,
                                "pin": pin_name,
                                "arduino": arduino,
                                "mask_addr": j,
                            })
    
    return results


def find_pmux_writes(data):
    """Find PMUX register writes to identify peripheral pin assignments."""
    results = []
    
    for i in range(0, len(data) - 3, 4):
        val = struct.unpack_from("<I", data, i)[0]
        
        for port_base, port_name in [(PORTA_BASE, "PA"), (PORTB_BASE, "PB")]:
            # PMUX registers at offset 0x30-0x3F (one byte per pair of pins)
            if port_base + 0x30 <= val <= port_base + 0x3F:
                pmux_idx = val - port_base - 0x30
                pin_lo = pmux_idx * 2
                pin_hi = pmux_idx * 2 + 1
                results.append({
                    "addr": i,
                    "port": port_name,
                    "pins": f"{port_name}{pin_lo:02d}/{port_name}{pin_hi:02d}",
                    "pmux_reg": f"PMUX[{pmux_idx}]",
                })
            
            # PINCFG registers at offset 0x40-0x5F (one byte per pin)
            if port_base + 0x40 <= val <= port_base + 0x5F:
                pin = val - port_base - 0x40
                results.append({
                    "addr": i,
                    "port": port_name,
                    "pin": f"{port_name}{pin:02d}",
                    "pincfg_reg": f"PINCFG[{pin}]",
                })
    
    return results


def extract_strings_with_context(data):
    """Extract strings and their addresses for cross-referencing."""
    results = []
    current = b""
    start = 0
    for i, b in enumerate(data):
        if 32 <= b < 127:
            if not current:
                start = i
            current += bytes([b])
        else:
            if len(current) >= 4:
                results.append((start, current.decode("ascii", errors="replace")))
            current = b""
    return results


def find_wrconfig_patterns(data):
    """
    WRCONFIG is a 32-bit write to PORT offset 0x28.
    The value encodes: bit[31]=HWSEL, bit[30]=WRPINCFG, bit[29]=WRPMUX, 
    bit[28]=PMUXE, bits[27:24]=PMUX_VALUE, bits[21:16]=PORTMUX, bits[15:0]=PINMASK
    This is the most reliable way the Arduino core configures pins.
    """
    results = []
    
    for i in range(0, len(data) - 3, 4):
        val = struct.unpack_from("<I", data, i)[0]
        
        for port_base, port_name in [(PORTA_BASE, "PA"), (PORTB_BASE, "PB")]:
            if val == port_base + 0x28:  # WRCONFIG address
                # Search nearby for the config value being written
                for j in range(max(0, i - 128), min(len(data) - 3, i + 128), 4):
                    if j == i:
                        continue
                    cfg = struct.unpack_from("<I", data, j)[0]
                    # Valid WRCONFIG: must have WRPINCFG or WRPMUX set, and a pin mask
                    if cfg & 0x40000000 or cfg & 0x20000000:
                        hwsel = (cfg >> 31) & 1
                        wrpincfg = (cfg >> 30) & 1
                        wrpmux = (cfg >> 29) & 1
                        pmux_en = (cfg >> 28) & 1
                        pmux_val = (cfg >> 24) & 0xF
                        pinmask = cfg & 0xFFFF
                        
                        if pinmask == 0:
                            continue
                        
                        # Decode which pins
                        pins = []
                        for bit in range(16):
                            if pinmask & (1 << bit):
                                pin_num = bit + (16 if hwsel else 0)
                                pins.append(f"{port_name}{pin_num:02d}")
                        
                        func_name = PMUX_FUNCS.get(pmux_val, f"func_{pmux_val}") if wrpmux else "GPIO"
                        
                        results.append({
                            "addr": j,
                            "port": port_name,
                            "pins": pins,
                            "pmux_en": pmux_en,
                            "pmux_func": func_name if pmux_en else "GPIO",
                            "wrpincfg": wrpincfg,
                            "wrpmux": wrpmux,
                            "raw": f"0x{cfg:08X}",
                        })
    
    return results


def analyze_dirset_context(data, disasm_path):
    """Search disassembly for DIRSET writes to find exactly which bits are set as outputs."""
    results = []
    
    # Load disassembly
    try:
        with open(disasm_path, "r") as f:
            disasm = f.read()
    except:
        return results
    
    # Find all references to PORT DIRSET addresses in the binary data
    for port_base, port_name in [(PORTA_BASE, "PA"), (PORTB_BASE, "PB")]:
        dirset_addr = port_base + 0x08
        # Search binary for this 32-bit value
        search_bytes = struct.pack("<I", dirset_addr)
        pos = 0
        while True:
            pos = data.find(search_bytes, pos)
            if pos == -1:
                break
            results.append(f"{port_name} DIRSET ref at binary offset 0x{pos:05X}")
            pos += 1
    
    return results


def main():
    print("=" * 70)
    print("  SmartPID CUBE — Firmware Pin Map Extractor")
    print(f"  Analyzing: {FIRMWARE}")
    print("=" * 70)
    
    data = load_binary(FIRMWARE)
    print(f"\nBinary size: {len(data)} bytes ({len(data)/1024:.1f} KB)")
    
    # Vector table
    sp = struct.unpack_from("<I", data, 0)[0]
    reset = struct.unpack_from("<I", data, 4)[0]
    print(f"Initial SP: 0x{sp:08X}")
    print(f"Reset vector: 0x{reset:08X}")
    
    # ─── Find PORT register references ────────────────────────────────────
    print("\n" + "─" * 70)
    print("  PORT REGISTER REFERENCES")
    print("─" * 70)
    
    constants = find_32bit_constants(data)
    for name, refs in sorted(constants.items()):
        if "PORT" in name:
            print(f"  {name}: {len(refs)} reference(s)")
            for addr, val in refs[:5]:
                print(f"    at binary offset 0x{addr:05X} -> 0x{val:08X}")
    
    # ─── Find pin bitmasks ────────────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  GPIO PIN BITMASKS (near PORT register references)")
    print("─" * 70)
    
    pins = find_pin_bitmasks(data)
    # Deduplicate by pin
    seen_pins = {}
    for p in pins:
        key = (p["pin"], p["reg"])
        if key not in seen_pins:
            seen_pins[key] = p
    
    for key in sorted(seen_pins.keys()):
        p = seen_pins[key]
        arduino = f" ({p['arduino']})" if p['arduino'] else ""
        print(f"  {p['pin']}{arduino}: {p['reg']} at 0x{p['addr']:05X}")
    
    # ─── Find WRCONFIG patterns ───────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  WRCONFIG PIN MUX CONFIGURATIONS")
    print("─" * 70)
    
    wrconfigs = find_wrconfig_patterns(data)
    seen_wrc = set()
    for wc in wrconfigs:
        key = wc["raw"]
        if key in seen_wrc:
            continue
        seen_wrc.add(key)
        pins_str = ", ".join(wc["pins"])
        print(f"  {wc['raw']}: pins=[{pins_str}] func={wc['pmux_func']}")
    
    # ─── Find SERCOM references ───────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  SERCOM REFERENCES (UART/I2C/SPI)")
    print("─" * 70)
    
    for name, refs in sorted(constants.items()):
        if "SERCOM" in name:
            print(f"  {name}: {len(refs)} reference(s)")
    
    # ─── Find ADC references ─────────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  ADC REFERENCES")
    print("─" * 70)
    
    for name, refs in sorted(constants.items()):
        if "ADC" in name:
            print(f"  {name}: {len(refs)} reference(s)")
    
    # ─── Find PMUX/PINCFG writes ─────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  PMUX / PINCFG REGISTER REFERENCES")
    print("─" * 70)
    
    pmux = find_pmux_writes(data)
    seen_pmux = set()
    for p in pmux:
        key = p.get("pin", p.get("pins", ""))
        if key in seen_pmux:
            continue
        seen_pmux.add(key)
        if "pin" in p:
            print(f"  {p['pincfg_reg']} -> {p['pin']}")
        else:
            print(f"  {p['pmux_reg']} -> {p['pins']}")
    
    # ─── DIRSET analysis ─────────────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  DIRSET REFERENCES (Output pins)")
    print("─" * 70)
    
    dirset_refs = analyze_dirset_context(data, "/home/brian/SmartPID-Still/factory_disasm.txt")
    for r in dirset_refs:
        print(f"  {r}")
    
    # ─── Interesting strings ──────────────────────────────────────────────
    print("\n" + "─" * 70)
    print("  HARDWARE-RELATED STRINGS")
    print("─" * 70)
    
    all_strings = extract_strings_with_context(data)
    keywords = ["relay", "ssr", "pump", "buzzer", "heater", "output", "input",
                "button", "temp", "sensor", "serial", "uart", "spi", "i2c",
                "wire", "oled", "display", "pwm", "adc", "wifi", "esp",
                "tone", "alarm", "cooling", "boil", "mash", "pid",
                "mqtt", "baud"]
    
    for addr, s in all_strings:
        s_lower = s.lower()
        if any(k in s_lower for k in keywords):
            if len(s) > 3 and len(s) < 200:
                print(f"  0x{addr:05X}: \"{s}\"")
    
    # ─── Direct binary search for known GPIO patterns ────────────────────
    print("\n" + "─" * 70)
    print("  DIRECT BINARY SEARCH — PORT BASE ADDRESSES")
    print("─" * 70)
    
    for port_base, port_name in [(PORTA_BASE, "PA"), (PORTB_BASE, "PB")]:
        for reg_name, reg_off in [("DIR", 0x00), ("DIRSET", 0x08), ("DIRCLR", 0x04),
                                   ("OUTSET", 0x18), ("OUTCLR", 0x14), ("OUTTGL", 0x1C),
                                   ("IN", 0x20)]:
            addr_bytes = struct.pack("<I", port_base + reg_off)
            count = 0
            pos = 0
            locations = []
            while True:
                pos = data.find(addr_bytes, pos)
                if pos == -1:
                    break
                count += 1
                locations.append(pos)
                pos += 1
            if count > 0:
                locs_str = ", ".join(f"0x{l:05X}" for l in locations[:8])
                if count > 8:
                    locs_str += f" ... (+{count-8} more)"
                print(f"  {port_name} {reg_name} (0x{port_base+reg_off:08X}): {count} refs at [{locs_str}]")
    
    print("\n" + "=" * 70)
    print("  Analysis complete")
    print("=" * 70)


if __name__ == "__main__":
    main()
