<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade ERC / DRC Rules

This directory contains the custom KiCad electrical and layout rule sets used for GhostBlade.

## Files

| File | Purpose |
|------|---------|
| `ghostblade-erc-rules.kicad_erc` | Schematic electrical rule checks |
| `ghostblade-drc-rules.kicad_drc` | PCB design rule checks |

## Intended Use

- use the ERC rules before exporting updated netlists
- use the DRC rules before generating fabrication outputs
- re-run checks after changing RF routing, high-speed buses, regulators, or connector pinouts

## Design Intent Covered

- IPC Class 3 board assumptions
- RF-sensitive clearances
- MIPI / PCIe / DSI differential-pair constraints
- power-rail and decoupling sanity checks
- manufacturing guardrails for the 6-layer handheld board

## Related Docs

- `../kicad/library-manifest.md`
- `../../docs/hardware-contributor-guide.md`
- `../../docs/power-tree.md`
