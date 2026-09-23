<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade Documentation Index

This page provides a central index of all GhostBlade (Project NullSpectre) documentation.

## Quick Start

| Document | Description |
|----------|-------------|
| [Board Quick-Start](board-quickstart.md) | TL;DR — from unpowered to operational in 10 minutes |
| [Getting Started](getting-started.md) | Dev environment setup, toolchain installation, first build |
| [Development Environment](development-environment.md) | Quick-setup guide — one-line install and build |
| [Getting Started Guide (detailed)](getting-started-guide.md) | Comprehensive build, flash, and test guide with pyapex examples |
| [Build Instructions](build-instructions.md) | Detailed build steps for firmware, driver, libapex, Python bindings |
| [Reproducible Builds](reproducible-builds.md) | Deterministic build environment, toolchain-file usage, and staging guidance |
| [Flashing Guide](flashing-guide.md) | Firmware flashing via OpenOCD/picotool/USB, driver loading, recovery |
| [Changelog](../CHANGELOG.md) | Project changelog and version history |

## Architecture & Reference

| Document | Description |
|----------|-------------|
| [System Architecture](architecture.md) | Block diagrams, processor architecture, bus map, data flows, security model |
| [Memory Map & Registers](memory-map.md) | Register reference, SPI bridge protocol, DMA, ioctl interfaces |
| [Sysfs Attributes](sysfs-attributes.md) | Driver telemetry attributes under `/sys/class/apex/apex_bridge0/` |
| [SPI Protocol & Timing](spi-protocol-timing.md) | Bridge protocol frame format, timing diagrams, CRC spec |
| [Audio Subsystem](audio-subsystem.md) | ES8388 codec driver, PTT flow, SPI audio commands, unit tests |
| [Power Tree](power-tree.md) | Power domain diagram, rail assignments, sequencing chart |
| [Power Tree Diagram](power-tree-diagram.md) | Detailed power tree with current budgets, ESD, test points |
| [Power Sequencing Overview](power-sequencing.md) | Multi-rail power architecture, rail dependencies, and sequencing constraints |
| [Power Sequencing Timing](power-sequencing-timing.md) | Cold boot, warm reset, sleep/wake, and shutdown timing charts |
| [Reset Circuit Design](reset-circuit-design.md) | Reset circuits for all processors and peripherals |
| [Timing Diagrams](timing-diagrams.md) | Mermaid sequence diagrams for boot, power, SPI, SDR, NFC, CC1101, watchdog, brownout |
| [FAQ & Troubleshooting](faq-troubleshooting.md) | Common issues, error codes, and solutions |
| [Pin Assignments](pin-assignments.md) | Cross-reference: schematic, DTS, and firmware pin mappings |
| [GPIO Cross-Reference](gpio-cross-reference.md) | RK3576 DTS ↔ RP2350B board_pins.h ↔ schematic net name mapping |
| [Glossary](glossary.md) | Project-specific terms, acronyms, and abbreviations |

## Hardware

| Document | Description |
|----------|-------------|
| [Hardware Bring-Up Checklist](hardware-bringup-checklist.md) | Step-by-step board bring-up with expected values and pass/fail criteria |
| [Hardware Test Procedures](hardware-test-procedures.md) | 17-section manufacturing test plan with pass/fail criteria |
| [Hardware Contributor Guide](hardware-contributor-guide.md) | Schematic/PCB design rules, DRC constraints, review checklist |
| [ESD Protection, Reset Circuits & Test Points](hardware-protection-and-testpoints.md) | TVS protection, reset timing, test point map, decoupling requirements |
| [KiCad Library Manifest](../hardware/kicad/library-manifest.md) | Symbol ↔ footprint ↔ 3D model cross-reference for the custom CAD libraries |
| [Reset Circuit Design](reset-circuit-design.md) | Detailed reset circuit schematics for all processors and peripherals |
| [Pin Assignments](pin-assignments.md) | Cross-reference: schematic net, DTS GPIO, and firmware pin mappings |

## Device Tree Overlays

| Overlay | Description |
|---------|-------------|
| `ghostblade-sdr-overlay.dts` | SDR (LMS7002M) runtime config: frequency, bandwidth, gain, DMA buffers |
| `ghostblade-cc1101-overlay.dts` | Sub-GHz radio (CC1101) runtime config: frequency band, modulation, TX power, GDO pins |
| `ghostblade-nfc-overlay.dts` | NFC (ST25R3916) runtime config: protocol, TX power, polling interval |
| `ghostblade-wifi-overlay.dts` | Wi-Fi 6E (MT7922) runtime config: regulatory domain, TX power, monitor mode, BT |
| `ghostblade-options.dts` | Optional hardware: GPS (u-blox NEO-M10N on UART2), external LNA, Bluetooth |
| `ghostblade-sleep-overlay.dts` | Power management: sleep/wake state transitions, brownout thresholds, thermal scaling |
| `ghostblade-gps-overlay.dts` | Optional GPS (u-blox NEO-M10N on UART2 + I2C2) with 1PPS time sync |

## Build Toolchains

| File | Description |
|------|-------------|
| `../software/toolchains/rk3576-aarch64.cmake` | Generic aarch64/Linux CMake toolchain file for RK3576-side builds |
| `../firmware/rp2350b/toolchain-arm-none-eabi.cmake` | RP2350B bare-metal CMake toolchain file for Pico SDK firmware builds |

## Validation Tools

| Tool | Description |
|------|-------------|
| `tools/validate_dts.py` | Cross-reference DTS GPIOs, RAM capacity, firmware pins, and schematic nets |
| `tools/validate_netlist.py` | Cross-reference manifest, KiCad netlist, DTS, firmware pins, and 3D models |
| `tools/check_links.py` | Check external markdown links in documentation |
| `tools/check_internal_links.py` | Check internal markdown links across the repository |
| `tools/generate_gerbers.py` | Generate Gerber files, drill files, and fabrication notes from KiCad PCB |

## Contributing

| Document | Description |
|----------|-------------|
| [Contributing](contributing.md) | Code, documentation, and hardware contribution workflow |
| [Contributor Onboarding](getting-started-contributors.md) | Step-by-step checklist for new contributors |
| [Security Policy](../SECURITY.md) | Vulnerability reporting and responsible disclosure |

## Sister Device — GhostWisp (Project Little Spectre)

| Document | Description |
|----------|-------------|
| [GhostWisp Overview](../devices/ghostwisp/README.md) | Project Little Spectre — pocketable MCU-first hacker companion |
| [GhostWisp Architecture](../devices/ghostwisp/docs/architecture.md) | Hardware architecture, component selection, power model |
| [GhostBlade ↔ GhostWisp Integration](../devices/ghostwisp/docs/ghostblade-integration.md) | Mothership/edge pairing protocol and USB/UART bridge |
| [GhostWisp Roadmap](../devices/ghostwisp/docs/roadmap.md) | Rev A milestones and component status |

## Engineering Phases (Detailed Design)

| Phase | Document | Description |
|-------|----------|-------------|
| 1 | [Architecture & Requirements](phase1-conceptual/architecture-and-requirements.md) | Power budgets, thermal profiles, data flow, bus topology, security threat model |
| 2 | [Component Selection & Schematics](phase2-schematics/component-selection-and-schematics.md) | BOM, netlists, decoupling networks, matching networks, power sequencing |
| 3 | [PCB Blueprints & Layout](phase3-pcb/pcb-blueprints-and-layout.md) | 6-layer stackup, impedance, fly-by routing, RF isolation, thermal vias, DFM |
| 4 | [Boot Process & MMIO](phase4-software/boot-process-and-mmio.md) | Boot chain, register maps, SPI protocol specification |

## Key Specifications

| Parameter | Value |
|-----------|-------|
| Primary SoC | Rockchip RK3576 (4× A72 + 4× A53, 6 TOPS NPU) |
| Coprocessor | RP2350B (2× Cortex-M33 / Hazard3 RISC-V @ 150 MHz) |
| RAM | 8 GB LPDDR5 @ 3200 MT/s |
| Storage | 32 GB eMMC 5.1 + M.2 2230 NVMe (PCIe Gen3 ×2) |
| SDR | LMS7002M (100 kHz – 3.8 GHz, 2×2 MIMO, 12-bit) |
| Sub-GHz | CC1101 (300–928 MHz, OOK/FSK/GFSK) |
| NFC | ST25R3916 (ISO 14443 A/B, 15693, FeliCa) |
| Wi-Fi/BT | MT7922 (Wi-Fi 6E 2×2, BT 5.4) |
| Battery | 10000 mAh Li-Po (38.5 Wh) |
| Form Factor | 162 × 76 × 18 mm, ~320 g |
| PCB | 6-layer FR-4 (Isola 370HR), 1.6 mm, IPC Class 3 |