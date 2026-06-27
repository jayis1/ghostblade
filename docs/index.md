# GhostBlade Documentation Index

This page provides a central index of all GhostBlade (Project NullSpectre) documentation.

## Quick Start

| Document | Description |
|----------|-------------|
| [Getting Started](getting-started.md) | Dev environment setup, toolchain installation, first build |
| [Build Instructions](build-instructions.md) | Detailed build steps for firmware, driver, libapex, Python bindings |
| [Flashing Guide](flashing-guide.md) | Firmware flashing via OpenOCD/picotool/USB, driver loading, recovery |
| [Changelog](../CHANGELOG.md) | Project changelog and version history |

## Architecture & Reference

| Document | Description |
|----------|-------------|
| [System Architecture](architecture.md) | Block diagrams, processor architecture, bus map, data flows, security model |
| [Memory Map & Registers](memory-map.md) | Register reference, SPI bridge protocol, DMA, ioctl interfaces |
| [Sysfs Attributes](sysfs-attributes.md) | Driver telemetry attributes under `/sys/class/apex/apex_bridge0/` |
| [SPI Protocol & Timing](spi-protocol-timing.md) | Bridge protocol frame format, timing diagrams, CRC spec |
| [Power Tree](power-tree.md) | Power domain diagram, rail assignments, sequencing chart |
| [Power Tree Diagram](power-tree-diagram.md) | Detailed power tree with current budgets, ESD, test points |
| [Power Sequencing Timing](power-sequencing-timing.md) | Cold boot, warm reset, sleep/wake, and shutdown timing charts |
| [Reset Circuit Design](reset-circuit-design.md) | Reset circuits for all processors and peripherals |
| [FAQ & Troubleshooting](faq-troubleshooting.md) | Common issues, error codes, and solutions |
| [Pin Assignments](pin-assignments.md) | Cross-reference: schematic, DTS, and firmware pin mappings |

## Hardware

| Document | Description |
|----------|-------------|
| [Hardware Bring-Up Checklist](hardware-bringup-checklist.md) | Step-by-step board bring-up with expected values and pass/fail criteria |
| [Hardware Test Procedures](hardware-test-procedures.md) | 17-section manufacturing test plan with pass/fail criteria |
| [Hardware Contributor Guide](hardware-contributor-guide.md) | Schematic/PCB design rules, DRC constraints, review checklist |
| [ESD Protection, Reset Circuits & Test Points](hardware-protection-and-testpoints.md) | TVS protection, reset timing, test point map, decoupling requirements |
| [Reset Circuit Design](reset-circuit-design.md) | Detailed reset circuit schematics for all processors and peripherals |
| [Pin Assignments](pin-assignments.md) | Cross-reference: schematic net, DTS GPIO, and firmware pin mappings |

## Contributing

| Document | Description |
|----------|-------------|
| [Contributing](contributing.md) | Code, documentation, and hardware contribution workflow |
| [Contributor Onboarding](getting-started-contributors.md) | Step-by-step checklist for new contributors |
| [Security Policy](../SECURITY.md) | Vulnerability reporting and responsible disclosure |

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
| Battery | 5000 mAh Li-Po (19.25 Wh) |
| Form Factor | 162 × 76 × 18 mm, ~320 g |
| PCB | 6-layer FR-4 (Isola 370HR), 1.6 mm, IPC Class 3 |