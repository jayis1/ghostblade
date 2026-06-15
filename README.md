<div align="center">

# GhostBlade

### Project NullSpectre

**Advanced Mobile Pentesting Lab**

Dual-processor (RK3576 + RP2350B) SDR-equipped handheld with Wi-Fi 6E, sub-GHz, and NFC

[![License: CERN-OHL-S v2](https://img.shields.io/badge/Hardware-CERN--OHL--S%20v2-blue)](LICENSE)
[![License: GPL-2.0+](https://img.shields.io/badge/Firmware-GPL--2.0--or--later-green)](LICENSE)
[![License: CC-BY-SA 4.0](https://img.shields.io/badge/Docs-CC--BY--SA%204.0-orange)](LICENSE)
[![GitHub commit activity](https://img.shields.io/github/commit-activity/t/jayis1/ghostblade)](https://github.com/jayis1/ghostblade/commits/main)
[![GitHub last commit](https://img.shields.io/github/last-commit/jayis1/ghostblade)](https://github.com/jayis1/ghostblade/commits/main)
[![GitHub repo size](https://img.shields.io/github/repo-size/jayis1/ghostblade)](https://github.com/jayis1/ghostblade)
[![GitHub issues](https://img.shields.io/github/issues/jayis1/ghostblade)](https://github.com/jayis1/ghostblade/issues)
[![GitHub pull requests](https://img.shields.io/github/issues-pr/jayis1/ghostblade)](https://github.com/jayis1/ghostblade/pulls)

</div>

---

## What Is This?

GhostBlade is a pocket-sized penetration testing device that combines a powerful Linux SoC with a real-time coprocessor to deliver wideband SDR, sub-GHz radio, NFC, and Wi-Fi 6E — all in a form factor that fits in your hand.

The RP2350B manages all RF frontends (antenna switching, SDR tuning, NFC polling) while the RK3576 runs a full Linux distribution with pentesting tools.

---

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────────┐
│                  GhostBlade Board                        │
│                                                          │
│  ┌──────────────┐  SPI0 @ 50 MHz  ┌──────────────┐      │
│  │   RK3576     │◄──────────────►│   RP2350B     │      │
│  │  4xA72+4xA53 │  (framed CRC)  │ 2xM33/Hazard3 │      │
│  │  6 TOPS NPU  │                 │                │      │
│  │              │  INT_REQ ──────►│  RF Manager    │      │
│  │  Linux Host  │  HOST_RDY ◄────│  Real-time     │      │
│  └──────┬───────┘  MCU_RESET ────►└──┬──┬──┬──┬───┘      │
│         │                          │  │  │  │            │
│    MIPI-CSI-2               SPI1   │  │  │  PIO           │
│         │                    ┌──────┘  │  └───┐          │
│  ┌──────▼──────┐            │    ┌────┘      │          │
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
│  │    [J5 SMA 2.4G]  [J6 SMA 5/6G]  [BT]          │     │
│  └─────────────────────────────────────────────────┘     │
│                                                          │
│  ┌──────┐ 8GB LPDDR5  ┌────────┐ 32GB eMMC  ┌──────┐   │
│  │ DDR5 │◄────────────►│RK3576  │◄──────────►│ M.2  │   │
│  └──────┘              │        │            │NVMe  │   │
│                         └────────┘            └──────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## System Block Diagram

```mermaid
graph TB
    RK3576[RK3576 SoC<br/>4×A72 + 4×A53<br/>6 TOPS NPU<br/>Mali G52]
    RP2350B[RP2350B MCU<br/>2×M33 / Hazard3<br/>150 MHz]
    LMS[LMS7002M SDR<br/>100 kHz–3.8 GHz<br/>2×2 MIMO]
    CC[CC1101 Sub-GHz<br/>300–928 MHz]
    NFC[ST25R3916 NFC<br/>ISO 14443/15693]
    MT[MT7922 Wi-Fi 6E<br/>BT 5.4]
    ANT[PE42422 Antenna Switch]
    DDR[8 GB LPDDR5]
    eMMC[32 GB eMMC 5.1]
    NVMe[M.2 2230 NVMe]
    PMIC[RK817 PMIC]
    BAT[5000 mAh Li-Po]

    RK3576 -- SPI0 50 MHz<br/>CRC-64/CRC-32 framed --> RP2350B
    RK3576 -- INT_REQ / HOST_RDY --> RP2350B
    RP2350B -- SPI1 --> LMS
    RP2350B -- SPI1 shared --> CC
    RP2350B -- SPI2 --> NFC
    RP2350B -- I2C0 --> NFC
    RP2350B -- GPIO --> ANT
    RK3576 -- MIPI-CSI-2 --> LMS
    RK3576 -- PCIe Gen3 x2 --> NVMe
    RK3576 -- eMMC 5.1 --> eMMC
    RK3576 -- LPDDR5 3200 --> DDR
    RK3576 -- PCIe + USB --> MT
    PMIC --> RK3576
    PMIC --> RP2350B
    BAT --> PMIC
```

## Power Sequencing Diagram

```mermaid
sequenceDiagram
    participant BAT as Battery (3.7–4.2V)
    participant PMIC as RK817 PMIC
    participant SOC as RK3576
    participant MCU as RP2350B
    participant SDR as LMS7002M
    participant NFC as ST25R3916

    BAT->>PMIC: VBAT_ALWAYS (3.7–4.2V)
    PMIC->>PMIC: Power-on reset (PWRON key)
    PMIC->>SOC: VDD_CORE 0.9V (BUCK1)
    PMIC->>SOC: VDD_LOGIC 1.8V (BUCK2)
    PMIC->>SOC: VDD_DDR 1.1V (BUCK3)
    Note over SOC: Boot ROM → SPL → U-Boot → Linux
    SOC->>MCU: MCU_RESET assert (LOW)
    PMIC->>MCU: VDD_3V3 (BUCK4 + LDO)
    SOC->>MCU: MCU_RESET release (HIGH)
    Note over MCU: RP2350B firmware init
    MCU->>SOC: HOST_RDY assert (LOW)
    SOC->>MCU: SPI0 probe → apex_bridge driver
    MCU->>SDR: SDR_RESET release, SPI1 config
    MCU->>NFC: NFC_SPI_CSN, SPI2 config
    Note over SOC,NFC: System operational
```

## SPI Bridge Protocol

```mermaid
sequenceDiagram
    participant Host as RK3576 (Host)
    participant MCU as RP2350B (MCU)

    Note over Host,MCU: Command Transaction
    Host->>MCU: CSn LOW → TX frame (16B hdr + payload + CRC32)
    Host->>MCU: CSn HIGH
    Note over MCU: MCU validates CRC-64 hdr + CRC-32 payload
    MCU->>Host: INT_REQ LOW (data ready)
    Host->>MCU: CSn LOW → NOP frame → RX response
    Host->>MCU: CSn HIGH
    MCU->>Host: INT_REQ HIGH (deassert)
```

---

## Repository Structure

```
ghostblade/
├── docs/
│   ├── getting-started.md                      # Dev environment setup & build guide
│   ├── flashing-guide.md                      # Firmware flashing & driver loading
│   ├── faq-troubleshooting.md                 # Frequently asked questions
│   ├── power-tree.md                          # Power tree diagram & rails
│   ├── spi-protocol-timing.md                # SPI bridge timing diagrams
│   ├── hardware-test-procedures.md             # 17-section test plan
│   ├── phase1-conceptual/
│   │   └── architecture-and-requirements.md
│   ├── phase2-schematics/
│   │   └── component-selection-and-schematics.md
│   ├── phase3-pcb/
│   │   └── pcb-blueprints-and-layout.md
│   └── phase4-software/
│       └── boot-process-and-mmio.md
├── firmware/
│   └── rp2350b/
│       ├── CMakeLists.txt                      # CMake build (Pico SDK)
│       ├── pico_sdk_import.cmake               # Pico SDK import
│       ├── rp2350b_memmap.ld                   # Linker script (memory map)
│       ├── include/
│       │   └── board_pins.h                    # MCU pin definitions
│       └── src/
│           ├── main.c                          # Entry point & init dispatch
│           ├── rp2350b_init.c                  # Clocks, GPIO, SPI, PIO, ADC init
│           ├── spi_protocol.c                  # SPI bridge protocol handler
│           ├── cc1101_init.c                   # CC1101 sub-GHz radio init
│           ├── st25r3916_init.c                # ST25R3916 NFC controller init
│           ├── sdr_dma.c                       # SDR DMA ring buffer manager
│           ├── battery_monitor.c               # ADC battery/temperature monitor
│           └── watchdog.c                      # Hardware watchdog handler
├── hardware/
│   ├── bom/
│   │   ├── ghostblade-bom.csv                  # Full BOM (80+ parts, MPN, price)
│   │   └── ghostblade-bom-interactive.html     # Interactive HTML BOM
│   └── kicad/
│       ├── ghostblade.kicad_pro                # KiCad 8 project file
│       ├── ghostblade.net                      # Schematic netlist (150+ nets)
│       ├── symbols/
│       │   └── ghostblade-symbols.kicad_sym
│       ├── footprints/
│       │   └── ghostblade-footprints.pretty/
│       │       └── ghostblade-footprints.kicad_mod
│       └── 3dmodels/
│           └── README.md
├── software/
│   ├── linux-drivers/
│   │   ├── include/
│   │   │   └── apex_bridge_regs.h              # Register defs, ioctl, protocol
│   │   ├── src/
│   │   │   └── apex_bridge.c                  # Kernel SPI driver (char dev)
│   │   ├── Kconfig                             # Kernel menuconfig entry
│   │   └── Makefile                            # Cross-compile Makefile
│   ├── libapex/
│   │   ├── include/
│   │   │   └── libapex.h                      # Userspace C API
│   │   ├── src/
│   │   │   ├── libapex.c                      # C library implementation
│   │   │   └── pyapex.c                        # Python bindings
│   │   ├── libapex.pc.in                       # pkg-config template
│   │   ├── Makefile
│   │   ├── setup.py
│   │   └── README.md
│   └── dts/
│       ├── ghostblade-rk3576.dts              # Device tree source
│       └── ghostblade-options.dts              # Optional hardware overlay
├── tests/
│   └── test_spi_protocol.c                    # SPI protocol unit tests
├── tools/
│   └── generate_gerbers.py                    # Gerber/fab-note generation script
├── .clang-format                               # Linux kernel-style formatting config
├── .markdownlint.json                          # Markdown linting rules
├── .codespell.ignore                           # Project-specific spellcheck ignore list
├── GhostBlade.mf                               # System Manifest
├── stats.json                                  # Dynamic badge data (auto-updated)
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
| ![Lines of C Code](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20C%20Code&query=%24.C&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fghostblade%2Fmain%2Fstats.json&color=blue) | Firmware + kernel driver |
| ![Lines of Headers](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20Headers&query=%24.headers&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fghostblade%2Fmain%2Fstats.json&color=blue) | Register defs + pin maps |
| ![Lines of DTS](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20DTS&query=%24.dts&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fghostblade%2Fmain%2Fstats.json&color=green) | Device tree source |
| ![Lines of Docs](https://img.shields.io/badge/dynamic/json?label=Lines%20of%20Docs&query=%24.docs&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fghostblade%2Fmain%2Fstats.json&color=orange) | Markdown documentation |
| ![BOM Components](https://img.shields.io/badge/dynamic/json?label=BOM%20Components&query=%24.bom_components&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fghostblade%2Fmain%2Fstats.json&color=red) | Unique parts in bill of materials |
| ![Total Files](https://img.shields.io/badge/dynamic/json?label=Total%20Files&query=%24.total_files&url=https%3A%2F%2Fraw.githubusercontent.com%2Fjayis1%2Fghostblade%2Fmain%2Fstats.json&color=9cf) | All project files |

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
| [ghostblade.kicad_pro](hardware/kicad/ghostblade.kicad_pro) | KiCad 8 project file (6-layer stackup, net classes) |
| [ghostblade-symbols.kicad_sym](hardware/kicad/symbols/ghostblade-symbols.kicad_sym) | Symbol library (RK3576, RP2350B, LMS7002M, CC1101, ST25R3916, PE42422, MT7922, RK817, LPDDR5) |
| [ghostblade-footprints.kicad_mod](hardware/kicad/footprints/ghostblade-footprints.pretty/ghostblade-footprints.kicad_mod) | Footprint library (FCBGA-732, QFN-60, QFN-64, all packages) |
| [ghostblade.net](hardware/kicad/ghostblade.net) | Schematic netlist (150+ nets, all IC connections) |
| [ghostblade-bom.csv](hardware/bom/ghostblade-bom.csv) | Full bill of materials (80+ line items, MPN, price) |
| [ghostblade-bom-interactive.html](hardware/bom/ghostblade-bom-interactive.html) | Interactive HTML BOM (search, filter, sort, cost calc) |
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

*GhostBlade — Project NullSpectre. Designed for those who build, test, and secure.*