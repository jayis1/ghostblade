#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 GhostBlade Project
# Author: jayis1
"""Run selected GhostBlade DTS and firmware assignment text checks.

Checks the existing firmware SPI0 definitions, handshake property tuples,
base pinctrl reference names, RAM size, and one NFC GPIO conflict pattern.
The DTS SPI0 check currently recognizes GPIO1_A0-A3 text (including comments),
not per-signal pinmux tuples. Overlay targets are listed, not resolved.
This is not a C/DTS parser, binding validator, or schematic connectivity check.
"""

import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ERRORS = []
WARNINGS = []


def check_dts_spi_pins():
    """Check DTS SPI0 pin-name text and each existing firmware SPI0 literal."""
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    pins_path = os.path.join(ROOT, "firmware/rp2350b/include/board_pins.h")

    with open(dts_path) as f:
        dts = f.read()
    with open(pins_path) as f:
        pins = f.read()

    # DTS defines SPI0 on GPIO1_A0-A3 (pins 0-3 in bank 1)
    dts_spi = sorted(set(re.findall(r'GPIO1_A(\d)', dts)))
    expected = ["0", "1", "2", "3"]  # MOSI, MISO, SCK, CSn
    if dts_spi != sorted(expected):
        ERRORS.append(f"DTS SPI0 pins GPIO1_A{dts_spi} don't match expected A0-A3")
    else:
        print("✓ SPI0 pins in DTS: GPIO1_A0-A3 (correct)")

    # Author: jayis1
    # Preserve the existing per-signal declarations, not merely the pin set.
    # This intentionally accepts only the current decimal-literal header form;
    # it is not a C preprocessor, pinmux check, or schematic endpoint check.
    pins = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                  lambda match: match[0] if match[0].startswith('"') else
                  re.sub(r'[^\n]', ' ', match[0]), pins, flags=re.DOTALL)
    for signal, expected in (("RX", "16"), ("CSN", "17"),
                             ("SCK", "18"), ("TX", "19")):
        name = f"PIN_SPI0_{signal}"
        values = re.findall(r'^[ \t]*#[ \t]*define[ \t]+' + re.escape(name)
                            + r'\b([^\n]*)', pins, flags=re.MULTILINE)
        if len(values) != 1:
            ERRORS.append(f"{name}: expected exactly one definition; found {len(values)}")
        elif values[0].strip() != expected:
            ERRORS.append(f"{name}: {values[0].strip()!r}; expected decimal {expected}")
        else:
            print(f"✓ {name}: {expected} (documented assignment)")


# Author: jayis1
def check_bridge_gpios():
    """Check each documented handshake tuple, not merely the set of pins.

    This is a text sanity check of existing assignments, not a DTS parser,
    binding validation, or evidence of schematic/physical connectivity.
    """
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    with open(dts_path) as f:
        dts = f.read()

    # Preserve quoted strings while removing comments; comment examples must not
    # satisfy a required property. Replace comments with whitespace, not nothing.
    dts = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                 lambda match: match[0] if match[0].startswith('"') else ' ',
                 dts, flags=re.DOTALL)
    for signal, pin in (("int-req", "8"), ("host-rdy", "9"), ("mcu-reset", "10")):
        name = f"apex,{signal}-gpio"
        values = re.findall(r'(?<![\w,-])' + re.escape(name) + r'\s*=\s*([^;]*);', dts)
        expected = f"<&gpio1 {pin} GPIO_ACTIVE_LOW>"
        if not values:
            ERRORS.append(f"Missing bridge GPIO property {name}")
            continue
        for value in values:
            # Check every occurrence: a correct copy cannot hide a conflicting
            # declaration elsewhere. Current source repeats these in two nodes.
            if not re.fullmatch(r'<\s*&gpio1\s+' + pin + r'\s+GPIO_ACTIVE_LOW\s*>',
                                value.strip()):
                ERRORS.append(f"{name}: {' '.join(value.split())!r}; expected {expected}")
            else:
                print(f"✓ {name}: {expected} (documented assignment)")


def check_memory_size():
    """Verify the 64-bit DTS RAM size agrees with the hardware manifest."""
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    manifest_path = os.path.join(ROOT, "GhostBlade.mf")
    with open(dts_path) as f:
        dts = f.read()
    with open(manifest_path) as f:
        manifest = f.read()

    memory = re.search(
        r'memory@0\s*\{.*?reg\s*=\s*<\s*0x[0-9a-fA-F]+\s+'
        r'0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>;',
        dts, re.DOTALL)
    if not memory:
        ERRORS.append("DTS memory@0 node has no parseable 64-bit reg property")
        return

    size = (int(memory.group(1), 16) << 32) | int(memory.group(2), 16)
    expected = 8 * 1024 * 1024 * 1024
    if "RAM               = \"8GB LPDDR5" not in manifest:
        ERRORS.append("Manifest no longer declares 8GB LPDDR5; update memory validation")
    elif size != expected:
        ERRORS.append(
            f"DTS memory size is {size:#x} ({size // (1024 * 1024)} MiB), "
            "but manifest requires 8 GiB")
    else:
        print("✓ DTS memory@0 size is 8 GiB and matches the manifest")


def check_pinctrl_completeness():
    """Check that all pinctrl references have corresponding definitions."""
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    with open(dts_path) as f:
        dts = f.read()

    # Find all pinctrl-0 references
    pinctrl_refs = re.findall(r'pinctrl-0\s*=\s*<([^>]+)>', dts)
    all_refs = set()
    for ref_group in pinctrl_refs:
        for ref in re.findall(r'&(\w+)', ref_group):
            all_refs.add(ref)

    # Find all pinctrl group definitions
    pinctrl_defs = re.findall(r'(\w+):\s+[\w-]+-pins?\s*\{', dts)
    pinctrl_defs += re.findall(r'(\w+):\s+[\w-]+-pin\s*\{', dts)

    defined = set(pinctrl_defs)
    for ref in sorted(all_refs):
        if ref not in defined:
            ERRORS.append(f"Referenced pinctrl group '&{ref}' has no definition")
        else:
            print(f"✓ pinctrl group '&{ref}' is defined")

    # Check sdmmc0 references both sdmmc0_pins and sdmmc0_det_pin
    if "sdmmc0_pins" in dts and "sdmmc0_det_pin" in dts:
        print("✓ SDMMC0 has both pin and detect pinctrl groups")


def check_overlay_targets():
    """Check that overlay fragment targets reference existing nodes/paths."""
    overlay_dir = os.path.join(ROOT, "software/dts")
    for fname in os.listdir(overlay_dir):
        if not fname.endswith('-overlay.dts'):
            continue
        fpath = os.path.join(overlay_dir, fname)
        with open(fpath) as f:
            content = f.read()

        # Check that target references are valid
        targets = re.findall(r'target\s*=\s*<&(\w+)>', content)
        for target in targets:
            print(f"  {fname}: references &{target}")

        # Check compatible string
        compat = re.findall(r'compatible\s*=\s*"([^"]+)"', content)
        if 'ghostblade,nullspectre' not in (compat[0] if compat else ''):
            WARNINGS.append(f"{fname}: compatible string should include 'ghostblade,nullspectre'")


def check_nfc_overlay_gpio():
    """Check that NFC overlay doesn't conflict with MCU_RESET GPIO."""
    nfc_path = os.path.join(ROOT, "software/dts/ghostblade-nfc-overlay.dts")
    with open(nfc_path) as f:
        content = f.read()

    # GPIO1_B2 (gpio1 pin 10) is MCU_RESET in base DTS — NFC field-enable should NOT use it
    if 'gpio1 10' in content:
        ERRORS.append("NFC overlay uses GPIO1 pin 10 (MCU_RESET) — this is a conflict!")
    else:
        print("✓ NFC overlay does not conflict with MCU_RESET GPIO")


def main():
    print("GhostBlade DTS Validation")
    print("=" * 50)

    check_dts_spi_pins()
    check_bridge_gpios()
    check_memory_size()
    check_pinctrl_completeness()
    check_overlay_targets()
    check_nfc_overlay_gpio()

    print()
    print("=" * 50)
    if ERRORS:
        print(f"ERRORS ({len(ERRORS)}):")
        for e in ERRORS:
            print(f"  ✗ {e}")
    if WARNINGS:
        print(f"WARNINGS ({len(WARNINGS)}):")
        for w in WARNINGS:
            print(f"  ⚠ {w}")
    if not ERRORS and not WARNINGS:
        print("All checks passed — no errors or warnings.")
    return 1 if ERRORS else 0


if __name__ == "__main__":
    sys.exit(main())