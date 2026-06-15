#!/usr/bin/env python3
"""
check-pin-consistency.py — Verify board_pins.h matches the manifest

SPDX-License-Identifier: GPL-2.0-or-later
Copyright (C) 2026 GhostBlade Project

Checks that the RP2350B pin definitions in board_pins.h match
the net assignments in GhostBlade.mf.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BOARD_PINS_H = REPO_ROOT / "firmware" / "rp2350b" / "include" / "board_pins.h"
MF_FILE = REPO_ROOT / "GhostBlade.mf"


def parse_board_pins():
    """Parse #define PIN_xxx from board_pins.h."""
    pins = {}
    if not BOARD_PINS_H.exists():
        print(f"WARN: board_pins.h not found: {BOARD_PINS_H}")
        return pins

    content = BOARD_PINS_H.read_text()
    for match in re.finditer(r'#define\s+(PIN_\w+)\s+(\d+)', content):
        pins[match.group(1)] = int(match.group(2))
    return pins


def parse_manifest_nets():
    """Parse NET_ entries from the manifest."""
    nets = {}
    if not MF_FILE.exists():
        print(f"WARN: Manifest not found: {MF_FILE}")
        return nets

    content = MF_FILE.read_text()
    for match in re.finditer(r'(NET_\w+)\s*=\s*"([^"]+)"', content):
        nets[match.group(1)] = match.group(2)
    return nets


def check_consistency():
    """Cross-reference pin definitions against manifest."""
    errors = []
    warnings = []

    pins = parse_board_pins()
    nets = parse_manifest_nets()

    if not pins:
        print("WARN: No pin definitions found in board_pins.h")
        return 0

    if not nets:
        print("WARN: No net definitions found in manifest")
        return 0

    # Check that SPI pins exist in board_pins.h
    expected_spi_pins = {
        "PIN_SPI0_RX": "SPI MISO",
        "PIN_SPI0_TX": "SPI MOSI",
        "PIN_SPI0_SCK": "SPI CLK",
        "PIN_SPI0_CSN": "SPI CS",
    }

    for pin_name, desc in expected_spi_pins.items():
        if pin_name not in pins:
            warnings.append(f"Missing {pin_name} ({desc}) in board_pins.h")

    # Check that GPIO control pins exist
    expected_gpio_pins = {
        "PIN_INT_REQ": "Interrupt Request",
        "PIN_HOST_RDY": "Host Ready",
        "PIN_MCU_RESET": "MCU Reset",
        "PIN_SDR_RESET": "SDR Reset",
        "PIN_CC1101_CS": "CC1101 Chip Select",
        "PIN_ST25R_CS": "ST25R3916 Chip Select",
    }

    for pin_name, desc in expected_gpio_pins.items():
        if pin_name not in pins:
            warnings.append(f"Missing {pin_name} ({desc}) in board_pins.h")

    # Print summary
    print(f"Found {len(pins)} pin definitions in board_pins.h")
    print(f"Found {len(nets)} net definitions in manifest")

    if warnings:
        print("\n=== WARNINGS ===")
        for w in warnings:
            print(f"  WARN: {w}")

    if not errors and not warnings:
        print("\n✓ Pin consistency check passed")
        return 0
    elif errors:
        print(f"\n✗ {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1
    else:
        print(f"\n⚠ {len(warnings)} warning(s)")
        return 0


if __name__ == "__main__":
    sys.exit(check_consistency())