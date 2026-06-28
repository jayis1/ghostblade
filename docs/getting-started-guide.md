# GhostBlade (Project NullSpectre) — Getting Started Guide

Welcome to the GhostBlade open-source hardware project! This guide will help you
set up your development environment, build the firmware and drivers, flash hardware,
and contribute effectively.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Development Environment Setup](#development-environment-setup)
4. [Building the RP2350B Firmware](#building-the-rp2350b-firmware)
5. [Building the Linux Kernel Driver](#building-the-linux-kernel-driver)
6. [Building libapex and Python Bindings](#building-libapex-and-python-bindings)
7. [Flashing the RP2350B](#flashing-the-rp2350b)
8. [Loading the Kernel Driver](#loading-the-kernel-driver)
9. [Running Unit Tests](#running-unit-tests)
10. [Running HIL Tests](#running-hil-tests)
11. [Code Style and Conventions](#code-style-and-conventions)
12. [Submitting Changes](#submitting-changes)
13. [Debugging Tips](#debugging-tips)
14. [Repository Layout](#repository-layout)

---

## Project Overview

GhostBlade is a dual-processor pentesting device featuring:

| Component        | Function                   |
|------------------|----------------------------|
| **RK3576**       | Main processor (Linux host)|
| **RP2350B**      | Coprocessor (firmware bridge)|
| **LMS7002M**     | SDR (100 kHz – 3.8 GHz)   |
| **CC1101**       | Sub-GHz radio (433/868/915 MHz)|
| **ST25R3916**    | NFC reader/writer (13.56 MHz)|
| **MT7922**       | Wi-Fi 6E + Bluetooth 5.3  |

The RK3576 runs Linux and communicates with the RP2350B over SPI. The RP2350B
manages all radio peripherals and streams IQ data back to the host via a
scatter-gather DMA pipeline.

---

## Hardware Architecture

```
┌──────────────────────────────────────────────┐
│                    RK3576                     │
│  ┌─────────┐  ┌──────────┐  ┌────────────┐  │
│  │apex_    │  │  libapex  │  │   pyapex   │  │
│  │bridge   │◄─┤  (C API)  ├──┤  (Python)  │  │
│  │(driver) │  └──────────┘  └────────────┘  │
│  └────┬────┘                                 │
│       │ SPI0 (host)                          │
└───────┼──────────────────────────────────────┘
        │
┌───────┼──────────────────────────────────────┐
│  RP2350B                                     │
│  ┌────┴────┐  ┌──────────┐  ┌────────────┐  │
│  │ SPI0    │  │  SDR DMA │  │  Battery   │  │
│  │ slave   │  │  engine  │  │  monitor   │  │
│  └────┬────┘  └─────┬────┘  └────────────┘  │
│       │              │                        │
│  ┌────┴────┐  ┌─────┴─────┐  ┌──────────┐  │
│  │ SPI1    │  │  SPI2      │  │  I2C1    │  │
│  │(LMS7002M│  │(ST25R3916) │  │(CC1101   │  │
│  │ +CC1101)│  │            │  │ aux I/O) │  │
│  └────┬────┘  └─────┬─────┘  └──────────┘  │
└───────┼─────────────┼───────────────────────┘
        │             │
   ┌────┴────┐   ┌────┴────┐
   │LMS7002M│   │ST25R3916│
   │  SDR   │   │  NFC    │
   └─────────┘   └─────────┘
```

---

## Development Environment Setup

### Prerequisites

- **Host OS**: Ubuntu 22.04+ (or any Linux with ARM cross-compile support)
- **Git**: 2.34+
- **ARM GCC toolchain**: `arm-none-eabi-gcc` 12.3+ (for RP2350B firmware)
- **AARCH64 GCC toolchain**: `aarch64-linux-gnu-gcc` (for kernel driver cross-compile)
- **Pico SDK**: 2.0+ (for RP2350B)
- **Python**: 3.10+
- **Linux kernel headers**: matching your target RK3576 kernel (5.10+)

### Install Build Dependencies

```bash
# ARM bare-metal toolchain (RP2350B firmware)
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib

# AArch64 cross-compiler (kernel driver)
sudo apt-get install gcc-aarch64-linux-gnu

# Build tools
sudo apt-get install cmake ninja-build python3-pip

# Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
cd /opt/pico-sdk
git submodule update --init --recursive

# Python dependencies
pip3 install --user meson pytest
```

### Clone the Repository

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
```

---

## Building the RP2350B Firmware

The RP2350B firmware uses the Pico SDK CMake build system.

```bash
# Set up environment
export PICO_SDK_PATH=/opt/pico-sdk
export PICO_TOOLCHAIN_PATH=/usr/bin

# Configure and build
cd firmware/rp2350b
mkdir -p build && cd build
cmake -G Ninja ..
ninja -j$(nproc)
```

The build produces:
- `ghostblade_rp2350b.uf2` — UF2 flash image (drag-and-drop via BOOTSEL)
- `ghostblade_rp2350b.elf` — ELF with debug symbols
- `ghostblade_rp2350b.bin` — Raw binary
- `ghostblade_rp2350b.hex` — Intel HEX format

### Firmware Configuration

The firmware can be configured via CMake options:

```bash
cmake -G Ninja .. \
    -DDEBUG_LOG=ON           \   # Enable debug UART logging
    -DWATCHDOG_TIMEOUT_MS=5000\   # Watchdog timeout (default: 5000)
    -DBROWNOUT_THRESHOLD_MV=2800\# Brownout voltage threshold
    -DSPI_CLOCK_HZ=21428571      # SPI0 clock frequency
```

---

## Building the Linux Kernel Driver

The `apex_bridge` driver compiles as an out-of-tree kernel module.

### Cross-Compile for RK3576

```bash
# Set your kernel source and arch
export KDIR=/path/to/rk3576-kernel-source
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

cd software/linux-drivers
make -C ${KDIR} M=$(pwd) modules
```

### Native Build (on the device)

```bash
# On the GhostBlade device itself
cd software/linux-drivers
make
sudo insmod apex_bridge.ko
```

### Device Tree Overlays

Compile and apply device tree overlays for runtime configuration:

```bash
cd software/dts

# SDR configuration overlay
dtc -I dts -O dtb -@ -o ghostblade-sdr-overlay.dtbo ghostblade-sdr-overlay.dts
sudo mkdir -p /sys/kernel/config/device-tree/overlays/sdr
sudo cp ghostblade-sdr-overlay.dtbo /sys/kernel/config/device-tree/overlays/sdr/dtbo

# CC1101 configuration overlay
dtc -I dts -O dtb -@ -o ghostblade-cc1101-overlay.dtbo ghostblade-cc1101-overlay.dts
# ... apply similarly

# Wi-Fi 6E configuration overlay
dtc -I dts -O dtb -@ -o ghostblade-wifi-overlay.dtbo ghostblade-wifi-overlay.dts
# ... apply similarly

# NFC configuration overlay
dtc -I dts -O dtb -@ -o ghostblade-nfc-overlay.dtbo ghostblade-nfc-overlay.dts
# ... apply similarly
```

---

## Building libapex and Python Bindings

```bash
cd software/libapex

# Build shared library
mkdir -p build && cd build
cmake -G Ninja ..
ninja

# Install system-wide (optional)
sudo ninja install
sudo ldconfig

# Build Python bindings
cd ../python
pip3 install --user .
```

### Using pyapex

```python
import pyapex

# Open the device
dev = pyapex.ApexDevice()

# Read telemetry
telem = dev.get_telemetry()
print(f"RSSI: {telem['rssi_dbm_x10'] / 10.0:.1f} dBm")
print(f"Temp: {telem['temp_c_x10'] / 10.0:.1f} °C")
print(f"VBat: {telem['vbat_mv']} mV")

# Configure SDR
dev.sdr_tune(freq_hz=868000000, bw_hz=2000000, gain_db_x10=300)

# Start streaming IQ data
dev.sdr_stream_start()
iq_data = dev.sdr_read_iq(32768)
dev.sdr_stream_stop()

# Select antenna
dev.ant_select(pyapex.ANT_MIMO_RX)

# Configure CC1101
dev.cc1101_configure(freq_band=1, modulation=1, data_rate=38400)

# NFC transaction
response = dev.nfc_transact(b'\x26\x00')  # REQA

dev.close()
```

---

## Flashing the RP2350B

### Method 1: UF2 Bootloader (Recommended)

1. Hold the BOOTSEL button on the RP2350B
2. Press and release the RESET button
3. Release BOOTSEL — the RP2350B appears as a USB mass storage device
4. Copy `ghostblade_rp2350b.uf2` to the mass storage device
5. The MCU automatically reboots and runs the new firmware

### Method 2: OpenOCD / SWD

```bash
# Using OpenOCD with an SWD adapter (e.g., Picoprobe)
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "program ghostblade_rp2350b.elf verify reset exit"
```

### Method 3: Kernel Driver Managed Flash

```bash
# Via apex-ctl utility (when driver is loaded)
apex-ctl --mcu-reset
sleep 0.5
apex-ctl --flash-firmware ghostblade_rp2350b.bin
```

---

## Loading the Kernel Driver

```bash
# Load the module
sudo insmod apex_bridge.ko

# Verify it loaded
dmesg | tail -20
lsmod | grep apex

# Check sysfs attributes
cat /sys/class/apex/apex_bridge0/driver_status
cat /sys/class/apex/apex_bridge0/rssi_dbm_x10
cat /sys/class/apex/apex_bridge0/temp_c_x10
cat /sys/class/apex/apex_bridge0/vbat_mv
cat /sys/class/apex/apex_bridge0/brownout_count
cat /sys/class/apex/apex_bridge0/uptime_ms

# Unload
sudo rmmod apex_bridge
```

---

## Running Unit Tests

The unit tests run on the host (x86_64) and validate SPI protocol framing,
CRC checksums, and state machine logic without hardware.

```bash
cd tests

# Build all unit tests
make

# Run SPI protocol tests
./test_spi_protocol

# Run sleep/wake state machine tests
./test_sleep_wake

# Run libapex C API tests (requires mock device)
./test_libapex

# Run kernel driver mock tests
./test_apex_bridge

# Run all tests with verbose output
make test
```

### Test Coverage

| Test Suite             | What it covers                              |
|------------------------|---------------------------------------------|
| `test_spi_protocol`   | CRC-64/CRC-32 framing, sync byte, validation|
| `test_sleep_wake`      | Sleep state machine transitions and timeouts |
| `test_libapex`         | C API: device open/close, SDR tune, stream   |
| `test_apex_bridge`    | Kernel driver: SG DMA, sysfs, ioctls         |

---

## Running HIL Tests

Hardware-in-the-Loop (HIL) tests require an actual GhostBlade device with
both processors running.

```bash
cd tests

# SPI bridge communication test
sudo ./hil_spi_bridge_test.sh

# SDR DMA streaming test
sudo ./hil_sdr_dma_stream_test.sh

# Quick smoke tests (reduced duration)
sudo ./hil_spi_bridge_test.sh --quick
sudo ./hil_sdr_dma_stream_test.sh --quick

# Continuous loop mode (run until failure)
sudo ./hil_spi_bridge_test.sh --loop
```

### HIL Test Prerequisites

- `/dev/apex_bridge0` exists and is readable/writable
- RP2350B firmware is running (check `driver_status` sysfs)
- `apex-ctl` and `pyapex` are installed
- For SDR tests: LMS7002M initialized or test pattern mode enabled

---

## Code Style and Conventions

### Licensing

| File type   | License             | SPDX identifier       |
|-------------|---------------------|-----------------------|
| `.c` files  | GPL-2.0-or-later    | `GPL-2.0-or-later`    |
| `.h` files  | MIT                 | `MIT`                 |
| `.py` files | MIT                 | `MIT`                 |
| `.sh` files | GPL-2.0-or-later    | `GPL-2.0-or-later`    |
| DTS files   | GPL-2.0+ OR MIT     | `(GPL-2.0+ OR MIT)`  |

Every source file **must** include an SPDX license identifier as the first line
(or in the file header comment block).

### C Code Style

- **Indentation**: 4 spaces (no tabs in `.c`/`.h` files)
- **Line length**: 80 characters preferred, 100 maximum
- **Naming**: `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for macros/enums
- **Comments**: Use `/* ... */` style, not `//` for multi-line comments
- **Headers**: Every `.c` file includes its own header first
- **Kernel code**: Follow the Linux kernel coding style (see `Documentation/process/coding-style.rst`)

### Commit Messages

Follow the conventional commit format:

```
area: brief description

Detailed explanation of what changed and why.
```

Examples:
- `firmware: add SPI protocol CRC-32 boundary test`
- `driver: fix brownout_count to track cumulative events`
- `docs: add getting-started guide for contributors`
- `dts: add Wi-Fi 6E overlay for MT7922`

---

## Submitting Changes

1. **Fork** the repository on GitHub
2. **Create a feature branch** from `main`
3. **Make your changes** with clear, descriptive commits
4. **Test** your changes — run the unit test suite and HIL tests if applicable
5. **Push** your branch and open a Pull Request against `main`

### PR Checklist

- [ ] All unit tests pass (`make test` in `tests/`)
- [ ] Code follows project style conventions
- [ ] SPDX license identifiers are present in all new files
- [ ] New functionality has corresponding tests
- [ ] Documentation is updated (if applicable)
- [ ] No `.github/workflows/` files (the project does not use CI workflows)

---

## Debugging Tips

### RP2350B Firmware Debug UART

The firmware outputs debug messages on UART (GPIO 0/1) at 115200 baud when
built with `-DDEBUG_LOG=ON`:

```bash
# Connect USB-serial adapter to GPIO 0 (TX) and GND
minicom -b 115200 -D /dev/ttyUSB0
```

### Kernel Driver Debug Messages

```bash
# Enable dynamic debug for apex_bridge
echo 'module apex_bridge +p' | sudo tee /sys/kernel/debug/dynamic_debug/control

# Watch kernel log
dmesg -w | grep apex
```

### SPI Bus Analysis

Use a logic analyzer on the SPI0 bus (CLK, MOSI, MISO, CS) to capture and
decode SPI frames. Refer to `docs/spi-protocol-timing.md` for the frame format
and timing specifications.

### Common Issues

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `/dev/apex_bridge0` not created | Driver not loaded | `sudo insmod apex_bridge.ko` |
| `ENODEV` on sysfs reads | MCU not ready | Check MCU reset GPIO, wait for boot |
| SPI transfer timeouts | Wrong SPI mode/speed | Verify `spi-mode = <0>` and `spi-max-frequency` in DTS |
| Brownout count stays 0 | MCU battery OK | This is normal — count increments only on brownout events |
| SDR stream returns 0 bytes | LMS7002M not initialized | Send `CMD_SDR_TUNE` before `CMD_SDR_STREAM` |
| NFC transact fails | No tag in field | Place ISO 14443A tag near antenna |

---

## Repository Layout

```
ghostblade/
├── docs/                           # Documentation
│   ├── spi-protocol-timing.md      # SPI protocol timing diagrams
│   ├── power-sequencing-timing.md  # Power sequencing charts
│   └── getting-started-guide.md    # This file
├── firmware/
│   └── rp2350b/                    # RP2350B firmware
│       ├── src/                    # Source files
│       │   ├── main.c              # Entry point and main loop
│       │   ├── spi_protocol.c      # SPI command handler
│       │   ├── spi0_isr.c          # SPI0 interrupt handler
│       │   ├── sdr_dma.c           # SDR DMA engine
│       │   ├── cc1101_init.c       # CC1101 sub-GHz radio
│       │   ├── st25r3916_init.c    # ST25R3916 NFC controller
│       │   ├── battery_monitor.c   # ADC battery/temp monitor
│       │   ├── watchdog.c          # Hardware watchdog
│       │   └── sleep_wake.c        # Sleep/wake state machine
│       └── include/                # Header files
├── software/
│   ├── linux-drivers/              # Kernel driver
│   │   ├── src/apex_bridge.c       # Main driver source
│   │   └── include/
│   │       └── apex_bridge_regs.h  # Register definitions
│   ├── libapex/                    # Userspace C library
│   │   ├── src/libapex.c           # C implementation
│   │   ├── src/pyapex.c            # Python C extension
│   │   └── include/libapex.h       # Public API header
│   └── dts/                        # Device tree overlays
│       ├── ghostblade-rk3576.dts   # Base device tree
│       ├── ghostblade-sdr-overlay.dts    # SDR runtime config
│       ├── ghostblade-nfc-overlay.dts    # NFC runtime config
│       └── ghostblade-wifi-overlay.dts   # Wi-Fi 6E config
└── tests/                          # Test suites
    ├── test_spi_protocol.c         # SPI protocol CRC unit tests
    ├── test_sleep_wake.c           # Sleep state machine unit tests
    ├── test_libapex.c              # libapex C API tests
    ├── test_apex_bridge.c          # Kernel driver mock tests
    ├── hil_spi_bridge_test.sh      # HIL SPI bridge test
    ├── hil_sdr_dma_stream_test.sh  # HIL SDR DMA test
    └── Makefile                    # Test build system
```

---

*For detailed SPI protocol specifications, see `docs/spi-protocol-timing.md`.*
*For power sequencing requirements, see `docs/power-sequencing-timing.md`.*