# GhostBlade (Project NullSpectre) — Getting Started Guide

Welcome to the GhostBlade open-source hardware project! This guide will help you set up your development environment, understand the codebase, and start contributing.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Repository Structure](#repository-structure)
4. [Development Environment Setup](#development-environment-setup)
5. [Building the Firmware](#building-the-firmware)
6. [Building the Linux Driver](#building-the-linux-driver)
7. [Running Tests](#running-tests)
8. [SPI Protocol Overview](#spi-protocol-overview)
9. [Coding Conventions](#coding-conventions)
10. [Submitting Changes](#submitting-changes)

---

## Project Overview

GhostBlade is a dual-processor pentesting device built around:

| Component | Part | Role |
|-----------|------|------|
| Host CPU | **RK3576** | Runs Linux, handles networking, UI, and high-level logic |
| Coprocessor | **RP2350B** | Real-time peripheral control (SDR, sub-GHz, NFC) |
| SDR | **LMS7002M** | Wideband RF transceiver (100 kHz – 3.8 GHz) |
| Sub-GHz | **CC1101** | 433/868/915 MHz transceiver |
| NFC | **ST25R3916** | ISO 14443A/B and ISO 15693 reader/writer |
| Wi-Fi | **MT7922** | Wi-Fi 6E (802.11ax) |

The RK3576 and RP2350B communicate via SPI0 (SPI Mode 0, up to 50 MHz) using a framed CRC-64/CRC-32 protocol.

---

## Hardware Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        RK3576 (Host)                         │
│                    ┌─────────────────────┐                   │
│                    │   Linux (Debian)    │                   │
│                    │   apex_bridge.ko     │                   │
│                    │   libapex.so         │                   │
│                    └─────────┬───────────┘                   │
│                              │ SPI0                          │
│                    ┌─────────┴───────────┐                   │
│                    │  /dev/apex_bridge0   │                   │
│                    └─────────┬───────────┘                   │
└──────────────────────────────┼───────────────────────────────┘
                               │ SPI0 (50 MHz)
┌──────────────────────────────┼───────────────────────────────┐
│                        RP2350B (MCU)                         │
│                    ┌─────────┴───────────┐                   │
│                    │   SPI Protocol       │                   │
│                    │   Handler            │                   │
│                    └───┬─────┬─────┬──────┘                   │
│                        │     │     │                           │
│              ┌─────────┤     │     ├──────────┐              │
│              │ SPI1    │     │     │ SPI2     │              │
│              ▼         │     │     ▼          │              │
│        ┌──────────┐   │     │  ┌──────────┐  │              │
│        │LMS7002M  │   │     │  │ST25R3916 │  │              │
│        │(SDR)     │   │     │  │(NFC)     │  │              │
│        └──────────┘   │     │  └──────────┘  │              │
│                        │     │                 │              │
│              ┌─────────┤     │                 │              │
│              │ SPI1    │     │                 │              │
│              │(shared) │     │                 │              │
│              ▼         │     │                 │              │
│        ┌──────────┐   │     │                 │              │
│        │CC1101    │   │     │                 │              │
│        │(Sub-GHz) │   │     │                 │              │
│        └──────────┘   │     │                 │              │
│                        │     │                 │              │
│              ┌─────────┘     ├─────────────────┘              │
│              │ I2C           │ ADC                            │
│              ▼               ▼                                 │
│         (ST25R3916     Battery Monitor                        │
│          aux control)  & Watchdog                              │
└───────────────────────────────────────────────────────────────┘
```

---

## Repository Structure

```
ghostblade/
├── docs/                          # Documentation
│   ├── spi-protocol-timing.md     # SPI protocol timing diagrams
│   ├── architecture-and-requirements.md
│   ├── component-selection-and-schematics.md
│   └── power-tree.md
├── firmware/
│   └── rp2350b/
│       ├── include/               # Header files
│       │   ├── board_pins.h        # GPIO pin assignments
│       │   ├── spi_protocol.h      # SPI protocol definitions
│       │   ├── sdr_dma.h           # SDR DMA ring buffer API
│       │   ├── cc1101_init.h       # CC1101 radio driver API
│       │   ├── st25r3916_init.h    # NFC controller API
│       │   ├── battery_monitor.h   # Battery & temperature API
│       │   ├── watchdog.h          # Watchdog timer API
│       │   ├── rp2350b_init.h      # Low-level init API
│       │   ├── peripheral_power.h  # Power sequencing API
│       │   ├── adc_calibration.h   # ADC calibration API
│       │   └── lms7002m_driver.h    # SDR transceiver API
│       └── src/                    # Source files
│           ├── main.c              # Firmware entry point
│           ├── spi_protocol.c      # SPI protocol handler
│           ├── sdr_dma.c           # SDR DMA ring buffer
│           ├── cc1101_init.c      # CC1101 initialization
│           ├── st25r3916_init.c    # NFC initialization
│           ├── battery_monitor.c   # Battery monitoring
│           ├── watchdog.c          # Watchdog timer
│           ├── rp2350b_init.c      # Low-level hardware init
│           ├── peripheral_power.c  # Power sequencing
│           ├── adc_calibration.c   # ADC calibration
│           └── lms7002m_driver.c   # LMS7002M SDR driver
├── software/
│   ├── linux-drivers/
│   │   ├── include/
│   │   │   └── apex_bridge_regs.h  # Kernel driver register defs
│   │   └── src/
│   │       └── apex_bridge.c       # Kernel SPI bridge driver
│   ├── dts/
│   │   └── ghostblade-sdr-overlay.dts  # Device tree overlay
│   └── libapex/
│       └── src/
│           └── libapex.c          # Userspace library
└── tests/
    ├── test_spi_protocol.c        # SPI protocol unit tests
    ├── test_apex_bridge.c         # Kernel driver tests
    ├── test_crc_validation.c      # CRC validation tests
    └── hitl_test.sh               # Hardware-in-the-loop tests
```

---

## Development Environment Setup

### Prerequisites

- **Host OS**: Linux (Ubuntu 22.04+ or Debian 12+ recommended)
- **Cross-compiler for RP2350B**: `arm-none-eabi-gcc` (for Cortex-M33)
- **Kernel headers**: For your RK3576 kernel version
- **Python 3.10+**: For test scripts and libapex bindings
- **CMake 3.20+**: Build system
- **Pico SDK**: For RP2350B firmware development

### Install Toolchain

```bash
# ARM cross-compiler for RP2350B firmware
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi

# Build tools
sudo apt-get install build-essential cmake git python3 python3-pip

# Pico SDK (for RP2350B)
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk && git submodule update --init
export PICO_SDK_PATH=~/pico-sdk

# Kernel headers (for apex_bridge driver)
sudo apt-get install linux-headers-$(uname -r)
```

### Clone the Repository

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
```

---

## Building the Firmware

```bash
cd firmware/rp2350b

# Create build directory
mkdir -p build && cd build

# Configure (adjust for your Pico SDK path)
cmake ..

# Build
make -j$(nproc)

# Output files:
#   ghostblade_rp2350b.elf   - Debug ELF with symbols
#   ghostblade_rp2350b.bin   - Binary for flashing
#   ghostblade_rp2350b.uf2   - UF2 for USB drag-and-drop
```

### Flashing the RP2350B

```bash
# Method 1: OpenOCD via SWD
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "program ghostblade_rp2350b.elf verify reset exit"

# Method 2: UF2 USB boot (hold BOOTSEL while plugging in USB)
cp ghostblade_rp2350b.uf2 /media/$USER/RPI-RP2/
```

---

## Building the Linux Driver

```bash
cd software/linux-drivers

# Build the kernel module
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the driver
sudo insmod apex_bridge.ko

# Verify it loaded
dmesg | grep apex_bridge
ls /dev/apex_bridge0
ls /sys/class/apex/apex_bridge0/

# Unload
sudo rmmod apex_bridge
```

### Building libapex

```bash
cd software/libapex
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Install (optional)
sudo make install
sudo ldconfig
```

---

## Running Tests

### Unit Tests (Host-side, no hardware required)

```bash
# Build and run CRC validation tests
cd tests
gcc -Wall -Wextra -O2 -I../firmware/rp2350b/include \
    test_crc_validation.c -o test_crc_validation
./test_crc_validation

# Expected output:
# === GhostBlade SPI Protocol CRC Validation Tests ===
# Test: CRC-64 known vectors
# Test: CRC-32 known vectors
# ...
# === Results ===
# Tests run:    XX
# Tests passed: XX
# Tests failed: 0
```

### SPI Protocol Tests

```bash
# Build and run SPI protocol framing tests
gcc -Wall -Wextra -O2 -I../firmware/rp2350b/include \
    test_spi_protocol.c -o test_spi_protocol
./test_spi_protocol
```

### Hardware-in-the-Loop Tests

```bash
# Requires a GhostBlade board connected
cd tests
./hitl_test.sh --verbose

# Skip specific subsystems
./hitl_test.sh --skip-sdr --skip-nfc

# Custom device path
./hitl_test.sh --device /dev/apex_bridge0
```

---

## SPI Protocol Overview

The RK3576 (host) communicates with the RP2350B (MCU) over SPI0 using a framed protocol with CRC integrity checking.

### Frame Format

```
┌─────────────────────────────────────────────────────┐
│ Sync (1B) │ Cmd (1B) │ Len (2B) │ Rsvd (4B) │ CRC-64 (8B) │
│   0xAA    │  opcode  │ payload  │   0x0000   │  header CRC │
│           │          │ length   │            │             │
├───────────┴──────────┴──────────┴────────────┴─────────────┤
│                     Payload (0–4092 bytes)                  │
├────────────────────────────────────────────────────────────┤
│                  CRC-32 (4 bytes)                          │
└────────────────────────────────────────────────────────────┘
```

### Command Opcodes

| Opcode | Name | Direction | Payload |
|--------|------|-----------|---------|
| 0x01 | SDR_TUNE | Host → MCU | freq_hz(4), bw_khz(2), gain_db_x10(2) |
| 0x02 | SDR_STREAM | Host → MCU | enable(1) |
| 0x03 | ANT_SELECT | Host → MCU | ant_id(1) |
| 0x04 | CC1101_CFG | Host → MCU | reg_addr(1), reg_len(1), data(reg_len) |
| 0x05 | NFC_TRANSACT | Host → MCU | cmd(1), flags(1), data_len(2), data(n) |
| 0x06 | TELEMETRY_REQ | Host → MCU | (none) |
| 0x07 | RESET_MCU | Host → MCU | magic(4) = 0x52534554 |
| 0x81 | TELEMETRY | MCU → Host | rssi(2), temp(2), vbat(2), cc1101_rssi(2), nfc_field(2), flags(2), uptime(4) |
| 0x82 | SDR_IQ_CHUNK | MCU → Host | IQ data(n) |

See `docs/spi-protocol-timing.md` for detailed timing diagrams.

---

## Coding Conventions

### C Code Style

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters max
- **Naming**: `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for macros
- **Comments**: Use `/* ... */` for block comments, `//` for single-line comments
- **Headers**: Every file must have an SPDX license identifier header
- **Kernel code**: Follow the Linux kernel coding style for `apex_bridge.c`

### File Headers

Every source file must begin with:

```c
/*
 * filename.c — Brief description
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Detailed description of the file's purpose and functionality.
 */
```

### Commit Messages

Use descriptive commit messages following this format:

```
subsystem: brief summary of changes

More detailed explanation if needed. Wrap at 72 characters.
Include motivation for the change and any relevant issue numbers.
```

Examples:
- `firmware: add LMS7002M SDR driver with PLL tuning support`
- `driver: add DMA scatter-gather engine for SDR IQ streaming`
- `tests: add CRC-64/CRC-32 validation unit tests`
- `docs: add getting-started guide for contributors`

---

## Submitting Changes

1. **Fork** the repository on GitHub
2. **Create a feature branch**: `git checkout -b feature/my-feature`
3. **Make your changes** following the coding conventions above
4. **Add tests** for any new functionality
5. **Run existing tests** to ensure nothing is broken
6. **Commit** with descriptive commit messages
7. **Push** your branch to your fork
8. **Open a Pull Request** against the `main` branch

### What to Contribute

We welcome contributions in these areas:

- **Firmware**: New peripheral drivers, bug fixes, performance improvements
- **Linux drivers**: Kernel module improvements, sysfs attributes, DMA support
- **Documentation**: Better diagrams, tutorials, API references
- **Testing**: Unit tests, integration tests, hardware-in-the-loop tests
- **Hardware**: Schematic improvements, PCB layout fixes, test point additions
- **Tools**: Python bindings, CLI utilities, signal processing scripts

### Important Notes

- **No CI/CD workflows**: Do not add `.github/workflows/` files
- **No force pushes to main**: Use feature branches and PRs
- **License**: All code must be GPL-2.0-or-later or MIT compatible
- **Binary files**: Do not commit binary files (firmware images, PDFs) — use Git LFS if needed

---

## Useful Resources

- **RP2350B Datasheet**: [Raspberry Pi Documentation](https://www.raspberrypi.com/documentation/microcontrollers/)
- **LMS7002M Documentation**: [Lime Microsystems](https://limemicro.com/)
- **CC1101 Datasheet**: [Texas Instruments SWRU066](https://www.ti.com/product/CC1101)
- **ST25R3916**: [STMicroelectronics DS12290](https://www.st.com/en/nfc/st25r3916.html)
- **RK3576 TRM**: Rockchip Technical Reference Manual (NDA required)

---

*Last updated: July 2026*