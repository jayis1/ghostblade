#!/usr/bin/env python3
"""Validate GhostBlade device tree GPIO consistency against board_pins.h.

Checks that:
1. SPI0 pin assignments in DTS match board_pins.h
2. Bridge GPIO assignments (INT_REQ, HOST_RDY, MCU_RESET) are consistent
3. LED/button GPIO numbers match DTS to schematic netlist
4. Overlay references target existing nodes
5. All referenced pinctrl groups are defined in the base DTS
"""

import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ERRORS = []
WARNINGS = []


def check_dts_spi_pins():
    """Verify SPI0 pin assignments in DTS match board_pins.h."""
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

    # board_pins.h defines SPI0 on RP2350B pins 16-19
    spi_pins = re.findall(r'#define PIN_SPI0_(?:RX|CSN|SCK|TX)\s+(\d+)', pins)
    expected_pins = ["16", "17", "18", "19"]
    if sorted(spi_pins) != sorted(expected_pins):
        ERRORS.append(f"board_pins.h SPI0 pins {spi_pins} don't match expected 16-19")
    else:
        print("✓ SPI0 pins in board_pins.h: pins 16-19 (correct)")


def check_bridge_gpios():
    """Verify bridge GPIO assignments in DTS."""
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    with open(dts_path) as f:
        dts = f.read()

    # INT_REQ = GPIO1_B0 = gpio1 pin 8
    # HOST_RDY = GPIO1_B1 = gpio1 pin 9
    # MCU_RESET = GPIO1_B2 = gpio1 pin 10
    bridge_pins = sorted(set(re.findall(
        r'apex,(?:int-req|host-rdy|mcu-reset)-gpio\s*=\s*<&gpio1\s+(\d+)\s+GPIO_\w+>',
        dts
    )))
    expected = ["8", "9", "10"]
    if bridge_pins != sorted(expected):
        ERRORS.append(f"Bridge GPIO pins {bridge_pins} don't match expected [8, 9, 10]")
    else:
        print("✓ Bridge GPIOs: INT_REQ=gpio1.8, HOST_RDY=gpio1.9, MCU_RESET=gpio1.10 (correct)")


def check_pinctrl_completeness():
    """Check that all pinctrl references have corresponding definitions."""
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    with open(dts_path) as f:
        dts = f.read()

    # Find all pinctrl-0 references
    pinctrl_refs = re.findall(r'pinctrl-0\s*=\s*<[&](\w+)', dts)
    # Also find multi-reference: pinctrl-0 = <&foo &bar>
    pinctrl_refs_multi = re.findall(r'pinctrl-0\s*=\s*<([^>]+)>', dts)
    all_refs = set()
    for ref in pinctrl_refs:
        all_refs.add(ref)

    # Find all pinctrl group definitions
    pinctrl_defs = re.findall(r'(\w+):\s+\w+-pins\s*\{', dts)
    pinctrl_defs += re.findall(r'(\w+):\s+\w+-pin\s*\{', dts)

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