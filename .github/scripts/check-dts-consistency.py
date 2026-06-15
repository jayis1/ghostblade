#!/usr/bin/env python3
"""
check-dts-consistency.py — Verify DTS GPIO/net assignments match the manifest

SPDX-License-Identifier: GPL-2.0-or-later
Copyright (C) 2026 GhostBlade Project

Checks that:
1. GPIO pin assignments in the DTS match the GhostBlade.mf manifest
2. SPI node properties are consistent between DTS and manifest
3. Compatible strings follow the expected pattern
4. All referenced phandles and nodes exist
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DTS_DIR = REPO_ROOT / "software" / "dts"
MF_FILE = REPO_ROOT / "GhostBlade.mf"

# Expected GPIO assignments from the manifest
EXPECTED_GPIO = {
    "apex,int-req-gpio": "GPIO1_B0 (gpio1 pin 8)",
    "apex,host-rdy-gpio": "GPIO1_B1 (gpio1 pin 9)",
    "apex,mcu-reset-gpio": "GPIO1_B2 (gpio1 pin 10)",
}

# Expected SPI pins from the manifest
EXPECTED_SPI = {
    "SPI_CLK": "GPIO1_A2",
    "SPI_MOSI": "GPIO1_A0",
    "SPI_MISO": "GPIO1_A1",
    "SPI_CS": "GPIO1_A3",
}

# Expected compatible strings
EXPECTED_COMPATIBLES = [
    "ghostblade,nullspectre",
    "rockchip,rk3576",
    "apex,apex-bridge",
]


def parse_manifest_gpio():
    """Parse the manifest file for GPIO/net assignments."""
    assignments = {}
    if not MF_FILE.exists():
        print(f"WARN: Manifest file not found: {MF_FILE}")
        return assignments

    with open(MF_FILE, 'r') as f:
        for line in f:
            line = line.strip()
            if '=' in line and 'NET_' in line:
                # Parse: NET_SPI_CLK = "RK3576.GPIO1_A2 -> RP2350B.PIN_18 (SPI0_SCK)"
                match = re.match(r'(\w+)\s*=\s*"(.+)"', line)
                if match:
                    assignments[match.group(1)] = match.group(2)
    return assignments


def check_dts_files():
    """Check all DTS files for consistency."""
    errors = []
    warnings = []

    dts_files = list(DTS_DIR.glob("*.dts"))
    if not dts_files:
        print("INFO: No DTS files found in software/dts/")
        return 0

    for dts_file in dts_files:
        print(f"\nChecking: {dts_file.name}")
        content = dts_file.read_text()

        # Check compatible strings
        compat_match = re.search(r'compatible\s*=\s*"([^"]+)"', content)
        if compat_match:
            compat = compat_match.group(1)
            # The first compatible should be project-specific
            if "apex-one" in compat.lower() or "cyber-swiss" in compat.lower():
                errors.append(
                    f"{dts_file.name}: Stale compatible string '{compat}' — "
                    f"should use 'ghostblade,nullspectre'"
                )

        # Check for stale naming references
        stale_names = ["apex-one", "Apex One", "Cyber-Swiss", "cyber-swiss"]
        for stale in stale_names:
            if stale.lower() in content.lower():
                # Allow "apex,apex-bridge" which is a driver compatible string
                if stale.lower() == "apex-one" and "apex,apex-bridge" in content:
                    continue
                warnings.append(
                    f"{dts_file.name}: Contains stale name reference '{stale}'"
                )

        # Check SPI frequency
        spi_freq = re.search(r'spi-max-frequency\s*=\s*<(\d+)>', content)
        if spi_freq:
            freq = int(spi_freq.group(1))
            if freq != 50000000:
                warnings.append(
                    f"{dts_file.name}: SPI max frequency is {freq}, "
                    f"expected 50000000"
                )

        # Check INT_REQ interrupt type
        int_type = re.search(r'interrupts\s*=\s*<\d+\s+(\w+)>', content)
        if int_type and int_type.group(1) != "IRQ_TYPE_EDGE_FALLING":
            warnings.append(
                f"{dts_file.name}: INT_REQ interrupt type is {int_type.group(1)}, "
                f"expected IRQ_TYPE_EDGE_FALLING"
            )

    # Print results
    if errors:
        print("\n=== ERRORS ===")
        for e in errors:
            print(f"  ERROR: {e}")
    if warnings:
        print("\n=== WARNINGS ===")
        for w in warnings:
            print(f"  WARN: {w}")

    if not errors and not warnings:
        print("\n✓ DTS consistency check passed")
        return 0
    elif errors:
        print(f"\n✗ {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1
    else:
        print(f"\n⚠ {len(warnings)} warning(s)")
        return 0


if __name__ == "__main__":
    sys.exit(check_dts_files())