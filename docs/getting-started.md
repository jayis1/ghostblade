# Getting Started — GhostBlade (Project NullSpectre)

Welcome to the GhostBlade open-source hardware project! This guide will
help you set up your development environment, understand the project
structure, and start contributing.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Repository Structure](#repository-structure)
3. [Hardware Architecture](#hardware-architecture)
4. [Development Environment Setup](#development-environment-setup)
5. [Building the Firmware](#building-the-firmware)
6. [Building the Kernel Driver](#building-the-kernel-driver)
7. [Building libapex and Python Bindings](#building-libapex-and-python-bindings)
8. [Running Tests](#running-tests)
9. [Code Style and Conventions](#code-style-and-conventions)
10. [Contributing Workflow](#contributing-workflow)
11. [Hardware-in-the-Loop Testing](#hardware-in-the-loop-testing)
12. [Documentation Guidelines](#documentation-guidelines)

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
│   └── dts/                      # Device tree overlays
├── hardware/
│   ├── kicad/                    # KiCad schematic & PCB files
│   └── datasheets/               # Component datasheets (not in repo)
├── docs/
│   ├── phase1-conceptual/        # Architecture & requirements
│   └── spi-protocol-timing.md   # SPI timing diagrams
├── tests/
│   └── test_spi_protocol.c       # SPI protocol unit tests
├── tests/                        # Unit and integration tests
├── README.md                     # Project overview
├── CONTRIBUTING.md               # Contribution guidelines
└── LICENSE                       # GPL-2.0-or-later
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
protocol with CRC-16 integrity. See `docs/spi-protocol-timing.md`
for detailed timing diagrams.

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
gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_spi_protocol test_spi_protocol.c
./test_spi_protocol
```

Expected output:
```
=== SPI Protocol Unit Tests ===
Running: test_crc16_known_vectors
Running: test_valid_nop_frame
Running: test_valid_payload_frame
...
=== Results: 26/26 passed, 0 failed ===
```

### With cmocka (if installed)

```bash
sudo apt-get install libcmocka-dev
gcc -Wall -Wextra -std=c11 -lcmocka -o test_spi_protocol test_spi_protocol.c
./test_spi_protocol
```

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

See `CONTRIBUTING.md` for full details.

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