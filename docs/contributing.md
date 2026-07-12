<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade Contributor's Guide

Welcome to the GhostBlade (Project NullSpectre) open-source hardware project! This guide will help you get started contributing to the dual-processor pentesting device with RK3576 + RP2350B, LMS7002M SDR, CC1101 sub-GHz, ST25R3916 NFC, and MT7922 Wi-Fi 6E.

## Table of Contents

- [Project Overview](#project-overview)
- [Repository Structure](#repository-structure)
- [Getting the Code](#getting-the-code)
- [Development Environment](#development-environment)
- [Code Style](#code-style)
- [Building](#building)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)
- [Areas That Need Help](#areas-that-need-help)

## Project Overview

GhostBlade is a portable dual-processor pentesting device:

| Component | Purpose |
|-----------|---------|
| **RK3576** | Main ARM processor — runs Linux, handles networking, SDR control |
| **RP2350B** | Co-processor — real-time SPI bridge, sub-GHz radio, NFC |
| **LMS7002M** | Wideband SDR (100 kHz – 3.8 GHz) for signal analysis |
| **CC1101** | Sub-GHz transceiver (300–928 MHz) |
| **ST25R3916** | NFC reader/writer (ISO 14443A/B, ISO 15693) |
| **MT7922** | Wi-Fi 6E adapter |

The RK3576 communicates with the RP2350B over a high-speed SPI bus, which carries command/response frames and SDR IQ data.

For detailed architecture diagrams, data flow paths, and the full bus map, see [System Architecture](architecture.md).

## Quick Setup

For a fast one-command setup on Ubuntu, see [Development Environment](development-environment.md).

## Repository Structure

```
ghostblade/
├── firmware/
│   └── rp2350b/
│       ├── include/           # Public headers (SPI protocol, SPI0 ISR, driver APIs)
│       └── src/               # Source files (SPI protocol, SPI0 ISR, SDR DMA, CC1101, NFC, battery monitor)
├── hardware/
│   ├── kicad/                # KiCad schematic and PCB files
│   ├── bom/                  # Bill of materials (CSV and interactive HTML)
│   └── drc/                  # KiCad DRC rules (IPC Class 3)
├── software/
│   ├── linux-drivers/
│   │   ├── include/          # Kernel driver headers (register defs, ioctl commands)
│   │   └── src/              # Kernel driver source (apex_bridge.c)
│   ├── libapex/
│   │   ├── include/          # libapex public API header
│   │   └── src/              # libapex C library + Python bindings (pyapex.c)
│   └── dts/                  # Device tree sources and overlays
├── docs/                     # Documentation (getting started, build, flashing, FAQ, etc.)
└── tests/                    # Unit and integration tests
```

## Getting the Code

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
```

Create a branch for your work:

```bash
git checkout -b improvement/my-feature-name
```

## Development Environment

### Firmware (RP2350B)

The RP2350B firmware uses the Pico SDK. You'll need:

- **arm-none-eabi-gcc** — ARM Cortex-M cross-compiler
- **Pico SDK 2.x** — Raspberry Pi Pico SDK with RP2350B support
- **CMake 3.20+** — Build system

```bash
# Install ARM toolchain (Ubuntu/Debian)
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi

# Clone Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk && git submodule update --init
export PICO_SDK_PATH=$(pwd)
```

### Linux Driver (RK3576)

The kernel driver builds on the target RK3576 device or cross-compiled:

```bash
# On the target device
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

### Userspace Library (libapex)

libapex builds on any Linux host:

```bash
cd software/libapex
make
```

For Python bindings:

```bash
python3 setup.py build_ext --inplace
```

## Code Style

### C Code (Firmware and Kernel Driver)

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 80 characters max
- **Naming**: `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for macros
- **Comments**: C-style `/* ... */` for block comments, `/* inline */` for single-line
- **Header guards**: `#ifndef MODULE_NAME_H \ #define MODULE_NAME_H ... #endif`
- **Doxygen**: Use `/** ... */` for function documentation blocks
- **Error codes**: Negative integers for errors (0 = success), matching `APEX_ERR_*` defines

### Python Bindings

- Follow PEP 8
- Use CPython API for the extension module
- Docstrings for all public functions

### Documentation

- Write in Markdown
- Keep lines under 80 characters for readability in terminal editors
- Use tables for structured data
- Include timing diagrams in ASCII art when possible

## Building

### Userspace Tests

```bash
cd tests
make          # Build all userspace tests
make check    # Build and run all tests
```

### Individual Tests

```bash
cd tests
make test_spi_protocol && ./test_spi_protocol
make test_libapex && ./test_libapex
```

## Testing

All changes should pass the existing test suite before submission:

```bash
cd tests
make check
```

### Test Categories

| Test File | What It Tests |
|-----------|---------------|
| `test_spi_protocol.c` | SPI protocol framing, CRC, command dispatch |
| `test_battery_monitor.c` | ADC battery voltage → percentage mapping |
| `test_cc1101_config.c` | CC1101 register configuration validation |
| `test_watchdog.c` | Watchdog timer behavior |
| `test_power_states.c` | Power state machine transitions |
| `test_libapex.c` | Userspace library API, error codes, telemetry flags, frequency calculations |
| `test_sdr_dma.c` | SDR DMA ring buffer management, overrun/underrun detection |
| `test_spi0_isr.c` | SPI0 ISR frame assembly state machine, sync detection, CRC validation |
| `test_st25r3916_init.c` | ST25R3916 NFC register map, SPI encoding, init sequence, voltage conversion |

### Adding New Tests

1. Create `test_your_feature.c` in `tests/`
2. Use the simple assertion macros pattern from existing tests (`ASSERT_EQ`, `ASSERT_TRUE`, etc.)
3. Add a target in `tests/Makefile`
4. Add the test to the `USERSPACE_TARGETS` list and `run` target

## Submitting Changes

1. **Create a feature branch** from `main`:
   ```bash
   git checkout main
   git pull origin main
   git checkout -b improvement/YYYY-MM-DD-topic
   ```

2. **Make focused commits** with clear messages:
   ```bash
   git add -A
   git commit -m "firmware: add ST25R3916 field strength measurement function"
   ```

3. **Push your branch**:
   ```bash
   git push origin improvement/YYYY-MM-DD-topic
   ```

4. **Open a Pull Request** against `main`:
   ```bash
   gh pr create --title "firmware: ST25R3916 improvements" --body "Description of changes..."
   ```

### Commit Message Convention

Use the following prefixes:

| Prefix | Scope |
|--------|-------|
| `firmware:` | RP2350B firmware changes |
| `driver:` | Linux kernel driver changes |
| `libapex:` | Userspace library changes |
| `docs:` | Documentation changes |
| `hw:` | Hardware/schematic changes |
| `test:` | Test additions or changes |
| `build:` | Build system changes |

### Code Review

- All PRs require review before merging
- Address review feedback with additional commits (do not force-push)
- Keep PRs focused — one concern per PR

## Areas That Need Help

### High Priority

1. **Kernel driver DMA scatter-gather** — The `apex_bridge.c` driver currently uses a simple read/write approach. Adding scatter-gather DMA support would significantly improve SDR throughput.

2. **Device tree overlay** — The SDR configuration overlay needs runtime parameter support for frequency, bandwidth, and gain changes.

3. **SPI0 ISR integration testing** — The SPI0 slave interrupt handler (`spi0_isr.c`) is implemented but needs hardware-in-the-loop verification of frame assembly, CRC validation, and error recovery under real SPI traffic.

### Medium Priority

4. **CC1101 register initialization** — Complete the CC1101 sub-GHz transceiver initialization with verified register values for 433 MHz, 868 MHz, and 915 MHz bands. ✅ *Multi-band config tables (433/868/915 MHz) and `cc1101_set_band()` API added.*

5. **ST25R3916 register address consistency** — The NFC driver register addresses have been consolidated in `st25r3916_init.h`. Verify against the ST25R3916 datasheet (DS12290) and test on hardware.

6. **Hardware-in-the-loop tests** — Automated test scripts that run on the actual GhostBlade hardware, exercising the SPI bridge, SDR streaming, NFC tag detection, and sub-GHz transmission.

7. **Power sequencing timing charts** — Add detailed timing diagrams for the RK3576 → PMIC → RP2350B power-up sequence.

### Low Priority

8. **Schematic PDF exports** — Generate PDF exports from KiCad for the schematic and board layout.

9. **BOM validation** — Cross-reference the bill of materials against available parts and create an alternate parts list.

## Questions?

Open an issue at https://github.com/jayis1/ghostblade/issues for bugs, feature requests, or questions.

For security vulnerabilities, see [SECURITY.md](../SECURITY.md) — **do not report security issues through public GitHub issues.**