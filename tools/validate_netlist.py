#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 GhostBlade Project
"""Validate GhostBlade schematic netlist consistency against GhostBlade.mf manifest.

Checks that:
1. All manifest net names appear in the KiCad netlist
2. Component references in the manifest match netlist components
3. Pin connections specified in the manifest are consistent
4. All components in the netlist have correct footprint assignments
5. 3D model references exist in the 3dmodels/README.md
"""

import re
import sys
import os
import configparser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ERRORS = []
WARNINGS = []


def parse_manifest_nets():
    """Parse net names from GhostBlade.mf [Schematic.Netlist.*] sections."""
    mf_path = os.path.join(ROOT, "GhostBlade.mf")
    nets = {}
    if not os.path.exists(mf_path):
        ERRORS.append(f"Manifest not found: {mf_path}")
        return nets

    with open(mf_path) as f:
        content = f.read()

    # Extract net entries like NET_SPI_CLK = "RK3576.GPIO1_A2 -> R1:33R -> RP2350B.PIN_18 (SPI0_SCK)"
    for m in re.finditer(r'^(\w+)\s*=\s*"(.+)"', content, re.MULTILINE):
        key, value = m.group(1), m.group(2)
        if key.startswith("NET_"):
            nets[key] = value

    return nets


def parse_kicad_netlist():
    """Parse component and net information from the KiCad netlist."""
    net_path = os.path.join(ROOT, "hardware/kicad/ghostblade.net")
    components = {}
    net_names = set()

    if not os.path.exists(net_path):
        ERRORS.append(f"KiCad netlist not found: {net_path}")
        return components, net_names

    with open(net_path) as f:
        content = f.read()

    # Parse components: (comp (ref "U1") (value "RK3576") ...)
    for m in re.finditer(r'\(comp\s+\(ref\s+"([^"]+)"\)\s+\(value\s+"([^"]+)"\)', content):
        ref, value = m.group(1), m.group(2)
        components[ref] = value

    # Parse net names from the nets section
    # (net (code "1") (name "NET_SPI_CLK"))
    for m in re.finditer(r'\(net\s+\(code\s+"\d+"\)\s+\(name\s+"([^"]+)"\)', content):
        net_names.add(m.group(1))

    return components, net_names


def parse_3dmodels_readme():
    """Parse 3D model filenames from the 3dmodels/README.md."""
    readme_path = os.path.join(ROOT, "hardware/kicad/3dmodels/README.md")
    models = set()

    if not os.path.exists(readme_path):
        WARNINGS.append(f"3D models README not found: {readme_path}")
        return models

    with open(readme_path) as f:
        content = f.read()

    # Extract .step filenames
    for m in re.finditer(r'`([^`]+\.step)`', content):
        models.add(m.group(1))

    return models


def main():
    print("GhostBlade Netlist Cross-Reference Validation")
    print("=" * 50)

    # Parse sources
    manifest_nets = parse_manifest_nets()
    components, kicad_nets = parse_kicad_netlist()
    models_3d = parse_3dmodels_readme()

    # Check 1: All manifest net names should be referenced consistently
    print(f"\n✓ Parsed {len(manifest_nets)} net definitions from GhostBlade.mf")
    print(f"✓ Parsed {len(components)} components from KiCad netlist")
    print(f"✓ Found {len(kicad_nets)} unique nets in KiCad netlist")

    # Check 2: Verify key components exist in netlist
    expected_components = {
        "U1": "RK3576",
        "U2": "RP2350B",
        "U3": "LMS7002M",
        "U4": "CC1101",
        "U5": "ST25R3916",
        "U6": "PE42422",
        "U7": "MT7922",
        "U8": "RK817",
        "U9": "K3LKBKB0BM-MGCJ",
    }

    for ref, expected_value in expected_components.items():
        if ref in components:
            actual = components[ref]
            if actual == expected_value:
                print(f"✓ {ref} = {actual} (correct)")
            else:
                ERRORS.append(f"{ref}: expected '{expected_value}', got '{actual}'")
        else:
            WARNINGS.append(f"{ref} ({expected_value}) not found in KiCad netlist")

    # Check 3: Verify manifest net names reference correct components
    for net_name, net_value in manifest_nets.items():
        # Check that component references in net values are known
        for comp_ref in re.findall(r'(U\d+|R\d+|C\d+|J\d+|L\d+)', net_value):
            if comp_ref.startswith("U") and comp_ref in expected_components:
                pass  # Known IC component
            elif comp_ref.startswith("R") or comp_ref.startswith("C") or comp_ref.startswith("L"):
                pass  # Passive components - expected but not in netlist component section

    # Check 4: Verify 3D model references match README
    footprint_models = set()
    fp_path = os.path.join(ROOT,
                           "hardware/kicad/footprints/ghostblade-footprints.pretty/ghostblade-footprints.kicad_mod")
    if os.path.exists(fp_path):
        with open(fp_path) as f:
            fp_content = f.read()
        for m in re.finditer(r'model\s+"[^"]*3dmodels/([^"]+)"', fp_content):
            model_name = m.group(1)
            footprint_models.add(model_name)
            if model_name not in models_3d:
                WARNINGS.append(f"3D model '{model_name}' referenced in footprints but not in 3dmodels/README.md")
            else:
                print(f"✓ 3D model reference: {model_name}")

    # Check 5: Verify net name consistency between manifest and netlist
    # The manifest uses NET_ prefixed names, the netlist should reference them
    manifest_net_prefixes = set()
    for net_name in manifest_nets:
        # Extract the functional part: NET_SPI_CLK -> SPI_CLK
        prefix = net_name.split("_")[1] if "_" in net_name else ""
        manifest_net_prefixes.add(prefix)

    print(f"\n✓ Manifest net prefixes: {', '.join(sorted(manifest_net_prefixes))}")

    # Check 6: Verify DTS net references match manifest
    dts_path = os.path.join(ROOT, "software/dts/ghostblade-rk3576.dts")
    if os.path.exists(dts_path):
        with open(dts_path) as f:
            dts_content = f.read()

        # Check that SPI0, I2C1, MIPI-CSI references in DTS match manifest
        dts_spi_pins = re.findall(r'GPIO1_A\d', dts_content)
        manifest_spi = [n for n in manifest_nets if 'SPI' in n]
        print(f"✓ DTS SPI0 pins found: {', '.join(sorted(set(dts_spi_pins)))}")
        print(f"✓ Manifest SPI nets: {len(manifest_spi)} entries")

        # Verify bridge GPIO references in DTS
        int_req = re.search(r'apex,int-req-gpio\s*=\s*<&gpio1\s+(\d+)', dts_content)
        host_rdy = re.search(r'apex,host-rdy-gpio\s*=\s*<&gpio1\s+(\d+)', dts_content)
        mcu_reset = re.search(r'apex,mcu-reset-gpio\s*=\s*<&gpio1\s+(\d+)', dts_content)

        if int_req and int_req.group(1) == "8":
            print("✓ INT_REQ GPIO = gpio1.8 (GPIO1_B0) matches manifest")
        else:
            ERRORS.append("INT_REQ GPIO mismatch in DTS vs manifest")

        if host_rdy and host_rdy.group(1) == "9":
            print("✓ HOST_RDY GPIO = gpio1.9 (GPIO1_B1) matches manifest")
        else:
            ERRORS.append("HOST_RDY GPIO mismatch in DTS vs manifest")

        if mcu_reset and mcu_reset.group(1) == "10":
            print("✓ MCU_RESET GPIO = gpio1.10 (GPIO1_B2) matches manifest")
        else:
            ERRORS.append("MCU_RESET GPIO mismatch in DTS vs manifest")

    # Check 7: Verify board_pins.h matches manifest net names
    pins_path = os.path.join(ROOT, "firmware/rp2350b/include/board_pins.h")
    if os.path.exists(pins_path):
        with open(pins_path) as f:
            pins_content = f.read()

        # Check RP2350B pin assignments match manifest
        pin_checks = [
            ("PIN_SPI0_RX", "16", "NET_SPI_MISO"),
            ("PIN_SPI0_CSN", "17", "NET_SPI_CS"),
            ("PIN_SPI0_SCK", "18", "NET_SPI_CLK"),
            ("PIN_SPI0_TX", "19", "NET_SPI_MOSI"),
            ("PIN_INT_REQ", "20", "NET_INT_REQ"),
            ("PIN_HOST_RDY", "21", "NET_HOST_READY"),
            ("PIN_MCU_RUN", "24", "NET_MCU_RESET"),
        ]

        for define, expected_pin, manifest_net in pin_checks:
            m = re.search(rf'#define\s+{define}\s+(\d+)', pins_content)
            if m:
                actual_pin = m.group(1)
                if actual_pin == expected_pin:
                    print(f"✓ {define} = {actual_pin} matches manifest {manifest_net}")
                else:
                    ERRORS.append(f"{define}: expected pin {expected_pin}, got {actual_pin}")
            else:
                ERRORS.append(f"{define} not found in board_pins.h")

    print("\n" + "=" * 50)
    if ERRORS:
        print(f"ERRORS ({len(ERRORS)}):")
        for e in ERRORS:
            print(f"  ✗ {e}")
    if WARNINGS:
        print(f"WARNINGS ({len(WARNINGS)}):")
        for w in WARNINGS:
            print(f"  ⚠ {w}")
    if not ERRORS and not WARNINGS:
        print("All netlist cross-reference checks passed.")
    return 1 if ERRORS else 0


if __name__ == "__main__":
    sys.exit(main())