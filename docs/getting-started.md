# Getting Started — GhostBlade (Project NullSpectre)

Welcome to the GhostBlade open-source hardware project! This guide will
help you set up your development environment, understand the project
structure, and start contributing.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Repository Structure](#repository-structure)
3. [Hardware Architecture](#hardware-architecture)
4. [Development Environment Setup](#development-environment-setup)
5. [Quick Build from Project Root](#quick-build-from-project-root)
6. [Building the Firmware](#building-the-firmware)
7. [Building the Kernel Driver](#building-the-kernel-driver)
8. [Building libapex and Python Bindings](#building-libapex-and-python-bindings)
9. [Running Tests](#running-tests)
10. [Code Style and Conventions](#code-style-and-conventions)
11. [Contributing Workflow](#contributing-workflow)
12. [Hardware-in-the-Loop Testing](#hardware-in-the-loop-testing)
13. [Documentation Guidelines](#documentation-guidelines)

---

## Project Overview

GhostBlade is a dual-processor pentesting device featuring:

- **RK3576** — ARM Cortex-A72 quad-core SoC running Linux
- **RP2350B** — ARM Cortex-M33 dual-core MCU for real-time radio control
- **LMS7002M** — SDR transceiver (100 kHz – 3.8 GHz, 2×2 MIMO)
- **CC1101** — Sub-GHz transceiver (300–928 MHz)
- **ST25R3916** — NFC reader/writer (13.56 MHz, ISO 14443/15693)
- **MT7922** — Wi-Fi 6E / Bluetooth 5.4
- **PE42422** — RF switch matrix (4 antenna paths)

The RP2350B manages the radio control plane via SPI buses, while the
RK3576 handles high-level operations through the `apex_bridge` kernel
driver and `libapex` userspace library.

---

## Repository Structure

```
ghostblade/
├── firmware/
│   └── rp2350b/                 # RP2350B MCU firmware
│       ├── src/                  # C source files
│       ├── include/              # Header files
│       └── CMakeLists.txt        # Pico SDK build config
├── software/
│   ├── linux-drivers/            # Kernel module (apex_bridge)
│   │   ├── src/                  # Driver source
│   │   ├── include/              # Register definitions
│   │   └── Makefile              # Kernel module build
│   ├── libapex/                  # Userspace C library + Python bindings
│   │   ├── src/                  # libapex.c + pyapex.c
│   │   ├── include/              # libapex.h
│   │   ├── Makefile              # C library build
│   │   └── setup.py              # Python extension build
│   └── dts/                      # Device tree sources
│       ├── ghostblade-rk3576.dts
│       ├── ghostblade-options.dts
│       ├── ghostblade-sdr-overlay.dts
│       └── Makefile              # DTS compile & validate targets
├── hardware/
│   ├── kicad/                    # KiCad schematic & PCB files
│   ├── bom/                      # Bill of materials (CSV + interactive HTML)
│   └── drc/                      # Custom DRC rules
├── docs/                         # All documentation
│   ├── getting-started.md
│   ├── build-instructions.md
│   ├── flashing-guide.md
│   ├── faq-troubleshooting.md
│   └── ...                       # See docs/index.md
├── tests/                        # Unit and HIL tests
├── tools/                        # Build utilities (Gerber generation)
├── Makefile                      # Top-level build convenience targets
├── CHANGELOG.md                  # Project changelog
├── CONTRIBUTING.md               # Contribution guidelines
├── LICENSE                       # Triple-license (CERN-OHL-S, GPL-2.0+, CC-BY-SA)
└── README.md                     # Project overview
```

---

## Hardware Architecture

### Processor Roles

| Component   | Role                            | Interface to RK3576  |
|-------------|---------------------------------|----------------------|
| RK3576      | Main SoC, Linux userspace       | —                    |
| RP2350B     | Radio control, real-time I/O    | SPI0 (bridge driver) |
| LMS7002M    | Wideband SDR                    | MIPI CSI-2 (data), SPI1 via MCU (control) |
| CC1101      | Sub-GHz radio                   | SPI1 via MCU         |
| ST25R3916   | NFC controller                  | SPI2 via MCU         |
| MT7922      | Wi-Fi 6E / BT                   | SDIO/USB on RK3576   |
| PE42422     | RF antenna switch               | GPIO via MCU         |

### SPI Bridge Protocol

The RK3576 communicates with the RP2350B over SPI0 using a framed
protocol with CRC-64 header integrity and CRC-32 payload integrity.
See `docs/spi-protocol-timing.md` for detailed timing diagrams.

---

## Development Environment Setup

### Prerequisites

- Ubuntu 22.04+ or equivalent Linux
- Git, GCC, Make, CMake, Ninja
- ARM cross-toolchain (`arm-none-eabi-gcc`)
- Raspberry Pi Pico SDK v2.0+
- Linux kernel headers (for module build)
- Python 3.8+ with development headers

### 1. Clone the Repository

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
```

### 2. Install System Dependencies

```bash
# Build essentials
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build python3 python3-pip \
  gcc-arm-none-eabi libnewlib-arm-none-eabi

# Kernel module build
sudo apt-get install -y linux-headers-$(uname -r) kmod

# Device tree compiler
sudo apt-get install -y device-tree-compiler

# Static analysis tools
sudo apt-get install -y cppcheck sparse clang-format
```

### 3. Set Up Pico SDK (for firmware build)

```bash
git clone --depth 1 --branch 2.0.0 \
  https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
cd /opt/pico-sdk && git submodule update --init --recursive
cd -

export PICO_SDK_PATH=/opt/pico-sdk
```

---

## Quick Build from Project Root

The top-level `Makefile` provides convenience targets for building sub-projects:

```bash
make help        # Show all available targets
make firmware    # Build RP2350B firmware (requires PICO_SDK_PATH)
make driver      # Build Linux kernel driver
make libapex     # Build userspace C library + Python bindings
make tests       # Build and run unit tests
make dtb         # Compile device tree sources
make validate    # Validate DTS syntax
make clean       # Remove all build artifacts
```

See the [Build Instructions](build-instructions.md) document for detailed per-component build steps.

---

## Building the Firmware

```bash
cd firmware/rp2350b
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=$PICO_SDK_PATH -DPICO_PLATFORM=rp2350 -G Ninja
ninja
```

Output: `apex_one.uf2` (flashable via Pico boot mode) and `apex_one.elf` (debug).

### Flashing the Firmware

1. Hold BOOTSEL button on the RP2350B
2. Connect USB
3. Copy `apex_one.uf2` to the mass storage device
4. The MCU will reboot with the new firmware

---

## Building the Kernel Driver

```bash
cd software/linux-drivers
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
sudo insmod apex_bridge.ko
```

Verify:
```bash
dmesg | grep apex
ls /dev/apex_bridge0
```

---

## Building libapex and Python Bindings

### C Library

```bash
cd software/libapex
make all
sudo make install
```

### Python Extension

```bash
cd software/libapex
pip3 install .
```

Or for development:
```bash
python3 setup.py build_ext --inplace
```

### Quick Test

```python
import pyapex
dev = pyapex.open()
telem = dev.get_telemetry()
print(f"Battery: {telem['vbat_mv']} mV")
dev.close()
```

---

## Running Tests

### SPI Protocol Unit Tests

```bash
cd tests
make test_spi_protocol
./test_spi_protocol
```

Expected output:
```
=== SPI Protocol Unit Tests ===
...
=== Results: 158/158 passed, 0 failed ===
```

### Battery Monitor Unit Tests

```bash
make test_battery_monitor
./test_battery_monitor
```

Expected output:
```
=== Battery Monitor Unit Tests ===
...
=== Results: 95/95 passed, 0 failed ===
```

### CC1101 Configuration Unit Tests

```bash
make test_cc1101_config
./test_cc1101_config
```

Expected output:
```
=== CC1101 Configuration Unit Tests ===
...
=== Results: 37/37 passed, 0 failed ===
```

### Watchdog Timer Unit Tests

```bash
make test_watchdog
./test_watchdog
```

Expected output:
```
=== Watchdog Unit Tests ===
...
=== Results: 72/72 passed, 0 failed ===
```

Tests the watchdog timer configuration, brownout detection magic values,
load value calculations, bark/reset timing, kick interval safety, and
boot sequence reset reason handling.

### Power State Machine Unit Tests

```bash
make test_power_states
./test_power_states
```

Expected output:
```
=== Power State Machine Unit Tests ===
...
=== Results: 57/57 passed, 0 failed ===
```

Tests the battery monitor power state machine transitions (ACTIVE → IDLE →
SLEEP → SHUTDOWN), voltage threshold hysteresis, brownout detection,
overtemperature protection, and boundary conditions.

### SDR DMA Ring Buffer Unit Tests

```bash
make test_sdr_dma
./test_sdr_dma
```

Expected output:
```
=== SDR DMA Ring Buffer Unit Tests ===
...
=== Results: 52/52 passed, 0 failed ===
```

Tests the SDR DMA ring buffer manager: block produce/consume cycles, overrun
and underrun detection, wrap-around integrity, sustained streaming patterns,
and rapid stress testing.

### SPI0 ISR Frame Assembly Unit Tests

```bash
make test_spi0_isr
./test_spi0_isr
```

Expected output:
```
=== SPI0 ISR Frame Assembly Unit Tests ===
...
=== Results: 42/42 passed, 0 failed ===
```

Tests the SPI0 slave interrupt handler's frame assembly state machine: sync
byte detection, header and payload CRC validation, error recovery, resync,
back-to-back frame processing, and statistics tracking.

### ST25R3916 NFC Controller Unit Tests

```bash
make test_st25r3916_init
./test_st25r3916_init
```

Expected output:
```
=== ST25R3916 NFC Controller Init Unit Tests ===
...
=== Results: XX/XX passed, 0 failed ===
```

Tests the ST25R3916 NFC controller initialization: register address and value
validation, SPI protocol encoding (read/write/burst/direct command), Space B
gateway mechanism, IRQ status bit definitions, initialization sequence ordering,
and voltage measurement conversion.

### Run All Userspace Tests

```bash
make all
make run
```

### With cmocka (if installed)

```bash
sudo apt-get install libcmocka-dev
# Rebuild with cmocka support (remove -DNO_CMOCKA from CFLAGS in Makefile)
make clean && make all
```

See `tests/README.md` for detailed test documentation.

---

## Code Style and Conventions

### C Code (Firmware and Driver)

- **Kernel driver**: Follow Linux kernel coding style
  - Tab indentation (8-column tabs)
  - 80-column line limit (100 max)
  - `snake_case` for functions and variables
  - Prefix module functions: `apex_bridge_`, `cc1101_`, `st25r3916_`
  - Use `dev_err()`, `dev_info()`, `dev_dbg()` for logging

- **Firmware**: Similar kernel-style adapted for bare-metal
  - Use `__attribute__((packed))` for protocol structures
  - Use `volatile` for hardware register access
  - Define register blocks with `#define` at file top
  - Use `static` for file-local functions

### Python Code

- PEP 8 style
- Type hints preferred
- Docstrings for all public functions

### Commit Messages

Format:
```
subsystem: Brief description of change

Longer explanation if needed. Wrap at 72 columns.

Signed-off-by: Your Name <email@example.com>
```

Example:
```
firmware: Add CC1101 sub-GHz radio initialization

Implement the full CC1101 register configuration for 868 MHz ISM band
with GFSK modulation at 250 kbps. Includes RX/TX mode control, RSSI
conversion, and PATABLE power settings.

Signed-off-by: Jane Doe <jane@example.com>
```

---

## Contributing Workflow

1. **Fork** the repository on GitHub
2. **Create a branch**: `improvement/YYYY-MM-DD-topic`
3. **Make changes** following code style conventions
4. **Run tests** locally before pushing
5. **Commit** with descriptive messages (see format above)
6. **Push** your branch to your fork
7. **Open a Pull Request** against `main`
8. Address review feedback

See `docs/contributing.md` for full details.

---

## Hardware-in-the-Loop Testing

For developers with physical GhostBlade hardware:

1. Flash the latest firmware to the RP2350B
2. Load the kernel driver on the RK3576
3. Run the libapex test suite
4. Verify telemetry readings (battery voltage, temperature)
5. Test SDR streaming at various frequencies
6. Test CC1101 TX/RX at 868 MHz
7. Test NFC tag detection with ISO 14443A cards

**Important**: Always verify RF emissions comply with local regulations
before transmitting. Use terminators on unused antenna ports.

---

## Documentation Guidelines

- All docs use Markdown with consistent formatting
- Include timing diagrams for hardware protocols
- Add register tables for new peripheral drivers
- Keep README.md concise; detailed docs go in `docs/`
- Use ASCII art for diagrams when SVG/Excalidraw isn't available
- Reference datasheets by document number and section

---

## Getting Help

- **Issues**: https://github.com/jayis1/ghostblade/issues
- **Discussions**: https://github.com/jayis1/ghostblade/discussions
- **Wiki**: Architecture details and design decisions

## License

This project is licensed under GPL-2.0-or-later. By contributing, you
agree that your contributions will be licensed under the same terms.