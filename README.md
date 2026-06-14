<div align="center">

# Apex One

### Project Cyber-Swiss

**Advanced Mobile Pentesting Lab**

Dual-processor (RK3576 + RP2350B) SDR-equipped handheld with Wi-Fi 6E, sub-GHz, and NFC

[![License: CERN-OHL-S v2](https://img.shields.io/badge/Hardware-CERN--OHL--S%20v2-blue)](LICENSE)
[![License: GPL-2.0+](https://img.shields.io/badge/Firmware-GPL--2.0--or--later-green)](LICENSE)
[![License: CC-BY-SA 4.0](https://img.shields.io/badge/Docs-CC--BY--SA%204.0-orange)](LICENSE)
[![GitHub commit activity](https://img.shields.io/github/commit-activity/t/jayis1/apex-one)](https://github.com/jayis1/apex-one/commits/main)
[![GitHub last commit](https://img.shields.io/github/last-commit/jayis1/apex-one)](https://github.com/jayis1/apex-one/commits/main)
[![GitHub repo size](https://img.shields.io/github/repo-size/jayis1/apex-one)](https://github.com/jayis1/apex-one)
[![GitHub issues](https://img.shields.io/github/issues/jayis1/apex-one)](https://github.com/jayis1/apex-one/issues)
[![GitHub pull requests](https://img.shields.io/github/issues-pr/jayis1/apex-one)](https://github.com/jayis1/apex-one/pulls)

</div>

---

## What Is This?

Apex One is a pocket-sized penetration testing device that combines a powerful Linux SoC with a real-time coprocessor to deliver wideband SDR, sub-GHz radio, NFC, and Wi-Fi 6E — all in a form factor that fits in your hand.

The RP2350B manages all RF frontends (antenna switching, SDR tuning, NFC polling) while the RK3576 runs a full Linux distribution with pentesting tools.

---

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────────┐
│                    Apex One Board                        │
│                                                          │
│  ┌──────────────┐  SPI0 @ 50 MHz  ┌──────────────┐     │
│  │   RK3576     │◄──────────────►│   RP2350B     │     │
│  │  4xA72+4xA53 │  (framed CRC)  │ 2xM33/Hazard3 │     │
│  │  6 TOPS NPU  │                 │                │     │
│  │              │  INT_REQ ──────►│  RF Manager    │     │
│  │  Linux Host  │  HOST_RDY ◄────│  Real-time     │     │
│  └──────┬───────┘  MCU_RESET ────►└──┬──┬──┬──┬───┘     │
│         │                          │  │  │  │           │
│    MIPI-CSI-2               SPI1   │  │  │  PIO          │
│         │                    ┌──────┘  │  └───┐           │
│  ┌──────▼──────┐            │    ┌────┘      │           │
│  │  LMS7002M   │    ┌───────▼┐  ┌▼──────┐ ┌▼────────┐  │
│  │  SDR 100kHz │    │ CC1101 │  │ST25R  │ │ PE42422  │  │
│  │  – 3.8 GHz  │    │ Sub-GHz│  │ 3916  │ │ Antenna  │  │
│  │  2×2 MIMO   │    │        │  │ NFC   │ │ Switch   │  │
│  └──────┬──────┘    └───┬────┘  └───┬───┘ └────┬─────┘  │
│         │               │           │          │         │
│     [J3 SMA]       [J4 SMA]    [NFC coil]  [4× SMA]     │
│         │               │                      │         │
│  ┌──────▼───────────────▼──────────────────────▼──┐     │
│  │          MT7922 Wi-Fi 6E / BT 5.4               │     │
│  │    [J5 SMA 2.4G]  [J6 SMA 5/6G]  [BT]           │     │
│  └─────────────────────────────────────────────────┘     │
│                                                          │
│  ┌──────┐ 8GB LPDDR5  ┌────────┐ 32GB eMMC  ┌──────┐   │
│  │ DDR5 │◄────────────►│RK3576  │◄──────────►│ M.2  │   │
│  └──────┘              │        │            │NVMe  │   │
│                         └────────┘            └──────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## Repository Structure

```
apex-one/
├── .github/workflows/
│   ├── ci.yml                                    # CI: lint, BOM check, source check
│   └── update-stats.yml                          # Auto-update stats.json for badges
├── docs/
│   ├── phase1-conceptual/
│   │   └── architecture-and-requirements.md       # Power, data flow, boot, security
│   ├── phase2-schematics/
│   │   └── component-selection-and-schematics.md  # Netlists, BOM, decoupling
│   ├── phase3-pcb/
│   │   └── pcb-blueprints-and-layout.md          # Stackup, impedance, thermal, DFM
│   ├── phase4-software/
│   │   └── boot-process-and-mmio.md              # Boot chain, register maps
│   └── hardware-test-procedures.md                # 17-section test procedure
├── hardware/
│   ├── bom/
│   │   ├── apex-one-bom.csv                       # Full BOM (80+ parts, MPN, price)
│   │   └── apex-one-bom-interactive.html          # Interactive HTML BOM
│   └── kicad/
│       ├── apex-one.kicad_pro                     # KiCad project (6-layer stackup)
│       ├── apex-one.net                            # Schematic netlist (150+ nets)
│       ├── symbols/
│       │   └── apex-one-symbols.kicad_sym          # All 9 IC symbols
│       ├── footprints/
│       │   └── apex-one-footprints.pretty/           # All 11 footprints
│       └── 3dmodels/
│           └── README.md                           # STEP model references
├── firmware/
│   └── rp2350b/
│       ├── include/
│       │   └── board_pins.h                        # MCU pin definitions
│       └── src/
│           └── rp2350b_init.c                      # Clocks, GPIO, SPI, PIO, radio init
├── software/
│   ├── linux-drivers/
│   │   ├── include/
│   │   │   └── apex_bridge_regs.h                  # Register defs, ioctl, protocol
│   │   ├── src/
│   │   │   └── apex_bridge.c                       # Kernel SPI driver (char dev)
│   │   └── Makefile                                # Cross-compile Makefile
│   ├── bootloader/                                 # U-Boot SPL and board config
│   └── dts/
│       └── apex-one-rk3576.dts                     # Device tree source
├── tools/
│   └── generate_gerbers.py                         # Gerber/fab-note generation script
├── Apex_One.mf                                     # System Manifest
├── stats.json                                      # Dynamic badge data (auto-updated)
├── CONTRIBUTING.md
├── LICENSE
└── README.md
```

---

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

---

## Repository Stats

| Metric | Value |
|--------|-------|
| ![Lines of C Code](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20C%20Code&query=%24.C&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fapex-one%2Fmain%2Fstats.json&color=blue) | Firmware + kernel driver |
| ![Lines of Headers](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20Headers&query=%24.headers&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fapex-one%2Fmain%2Fstats.json&color=blue) | Register defs + pin maps |
| ![Lines of DTS](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20DTS&query=%24.dts&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fapex-one%2Fmain%2Fstats.json&color=green) | Device tree source |
| ![Lines of Docs](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20Docs&query=%24.docs&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fapex-one%2Fmain%2Fstats.json&color=orange) | Markdown documentation |
| ![BOM Components](https://img.shields.io/badge/dynamic/json?label=BOM%20Components&query=%24.bom_components&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fapex-one%2Fmain%2Fstats.json&color=red) | Unique parts in bill of materials |
| ![Total Files](https://img.shields.io/badge/dynamic/json?label=Total%20Files&query=%24.total_files&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fapex-one%2Fmain%2Fstats.json&color=9cf) | All project files |

---

## Engineering Phases

| Phase | Document | Description |
|-------|----------|-------------|
| 1 | [architecture-and-requirements.md](docs/phase1-conceptual/architecture-and-requirements.md) | Power budgets, thermal profiles, data flow, bus topology, security threat model |
| 2 | [component-selection-and-schematics.md](docs/phase2-schematics/component-selection-and-schematics.md) | BOM, netlists, decoupling networks, matching networks, power sequencing |
| 3 | [pcb-blueprints-and-layout.md](docs/phase3-pcb/pcb-blueprints-and-layout.md) | 6-layer stackup, impedance, fly-by routing, RF isolation, thermal vias, DFM |
| 4 | [boot-process-and-mmio.md](docs/phase4-software/boot-process-and-mmio.md) | Boot chain, register maps, SPI protocol specification |

---

## Hardware Design Files

| File | Description |
|------|-------------|
| [apex-one.kicad_pro](hardware/kicad/apex-one.kicad_pro) | KiCad 8 project file (6-layer stackup, net classes) |
| [apex-one-symbols.kicad_sym](hardware/kicad/symbols/apex-one-symbols.kicad_sym) | Symbol library (RK3576, RP2350B, LMS7002M, CC1101, ST25R3916, PE42422, MT7922, RK817, LPDDR5) |
| [apex-one-footprints.kicad_mod](hardware/kicad/footprints/apex-one-footprints.pretty/apex-one-footprints.kicad_mod) | Footprint library (FCBGA-732, QFN-60, QFN-64, all packages) |
| [apex-one.net](hardware/kicad/apex-one.net) | Schematic netlist (150+ nets, all IC connections) |
| [apex-one-bom.csv](hardware/bom/apex-one-bom.csv) | Full bill of materials (80+ line items, MPN, price) |
| [apex-one-bom-interactive.html](hardware/bom/apex-one-bom-interactive.html) | Interactive HTML BOM (search, filter, sort, cost calc) |
| [3D models README](hardware/kicad/3dmodels/README.md) | STEP model references and parametric generation scripts |

---

## Inter-Processor Bridge Protocol

The RK3576 and RP2350B communicate over SPI0 at up to 50 MHz using a framed protocol:

```
┌────────┬─────┬──────┬──────────┬──────────┬─────────┬──────────┬────────┐
│ SYNC   │ CMD │ LEN  │ RESERVED │ HDR_CRC  │ PAYLOAD │ PAYLOAD  │ PADDING │
│ 0xAA   │ 1B  │ 2B   │ 4B       │ 8B (CRC64)│ 0-4092B │ CRC32    │         │
└────────┴─────┴──────┴──────────┴──────────┴─────────┴──────────┴────────┘
 Byte 0    1     2-3     4-7        8-15       16-n      n+1..n+4
```

See `apex_bridge_regs.h` for full opcode definitions.

---

## Getting Started

### Prerequisites

- Linux host with `arm-none-eabi-gcc` for RP2350B firmware
- `aarch64-linux-gnu-gcc` cross-compiler for RK3576 Linux
- KiCad 8+ for schematic/PCB editing
- Linux kernel 6.6+ headers for driver compilation

### Building the Linux Driver

```bash
cd software/linux-drivers
make -C /path/to/kernel/src M=$(pwd) modules
sudo insmod apex_bridge.ko
```

### Building RP2350B Firmware

```bash
cd firmware/rp2350b
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
make -j$(nproc)
```

### Generating Gerber Files

```bash
python3 tools/generate_gerbers.py --fab-note --zip
```

---

## License

- **Hardware designs** (schematics, PCB layouts, BOM): [CERN-OHL-S v2](LICENSE)
- **Firmware and software**: [GPL-2.0-or-later](LICENSE)
- **Documentation**: [CC-BY-SA 4.0](LICENSE)

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines. In short:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Commit your changes with clear descriptions
4. Push to your fork and open a Pull Request

---

*Apex One — Project Cyber-Swiss. Designed for those who build, test, and secure.*