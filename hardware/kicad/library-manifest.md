<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade KiCad Library Manifest

This file is the human-readable cross-reference for the custom GhostBlade symbol, footprint, and 3D model libraries.

## Core IC Coverage

| Ref | Device | Symbol | Footprint | 3D model |
|-----|--------|--------|-----------|----------|
| U1 | RK3576 | `RK3576` | `ghostblade-footprints:FCBGA-732_0.65mm` | `RK3576.step` |
| U2 | RP2350B | `RP2350B` | `ghostblade-footprints:QFN-60-0.4mm_7x7mm` | `RP2350B.step` |
| U3 | LMS7002M | `LMS7002M` | `ghostblade-footprints:QFN-64-0.5mm_9x9mm` | `LMS7002M.step` |
| U4 | CC1101 | `CC1101` | `ghostblade-footprints:QFN-20-0.5mm_4x4mm` | `CC1101.step` |
| U5 | ST25R3916 | `ST25R3916` | `ghostblade-footprints:QFN-32-0.5mm_5x5mm` | `ST25R3916.step` |
| U6 | PE42422 | `PE42422` | `ghostblade-footprints:QFN-16-0.5mm_3x3mm` | `PE42422.step` |
| U7 | MT7922 | `MT7922` | `ghostblade-footprints:LGA-145_14.5x13.5mm` | `MT7922.step` |
| U8 | RK817 | `RK817` | `ghostblade-footprints:QFN-48-0.5mm_6x6mm` | `RK817.step` |
| U9 | K3LKBKB0BM-MGCJ | `K3LKBKB0BM-MGCJ` | `ghostblade-footprints:FBGA-200_12x12mm` | `LPDDR5.step` |

## Board-Level Connectors and Support Parts

The custom footprint library also carries package definitions used repeatedly across the board:

- USB-C receptacle
- M.2 2230 socket
- SMA edge-mount connectors
- eMMC BGA package
- SPI NOR flash package
- common regulator packages (SOT-23-5/6, VQFN-14)
- crystals, inductors, FPC connectors, tactile switches, LEDs, and TVS devices

## Review Checklist

When editing KiCad assets, verify:

1. symbol `Footprint` property matches the library footprint name
2. footprint `model` path points at an entry documented in `3dmodels/README.md`
3. net names remain aligned with `GhostBlade.mf`
4. validation passes with `python3 tools/validate_netlist.py`

## Related Files

- `hardware/kicad/symbols/ghostblade-symbols.kicad_sym`
- `hardware/kicad/footprints/ghostblade-footprints.pretty/ghostblade-footprints.kicad_mod`
- `hardware/kicad/3dmodels/README.md`
- `hardware/kicad/ghostblade.net`
