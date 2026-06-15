# Build Instructions

**GhostBlade — Project NullSpectre**

This document describes how to build all software components of the GhostBlade project: the RP2350B firmware, the Linux kernel driver, the userspace library, and the Python bindings.

---

## 1. Prerequisites

### 1.1 Host System

A Linux x86-64 host is recommended. Tested on Ubuntu 22.04 LTS and Debian 12.

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3 python3-pip \
    gcc-arm-none-eabi libnewlib-arm-none-eabi \
    gcc-aarch64-linux-gnu libncurses-dev bison flex libssl-dev \
    kicad python3-kicad
```

### 1.2 Toolchain Versions

| Component | Toolchain | Version | Notes |
|-----------|-----------|---------|-------|
| RP2350B firmware | `arm-none-eabi-gcc` | ≥ 13.2 | Bare-metal Cortex-M33 |
| Linux kernel driver | `aarch64-linux-gnu-gcc` | ≥ 12.0 | Cross-compile for ARM64 |
| libapex (userspace C) | `gcc` or `aarch64-linux-gnu-gcc` | ≥ 11.0 | Native or cross |
| Python bindings | `gcc` + Python 3.8+ | ≥ 3.8 | CPython extension |

---

## 2. Building RP2350B Firmware

### 2.1 Install Pico SDK

```bash
git clone https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
cd /opt/pico-sdk
git submodule update --init
export PICO_SDK_PATH=/opt/pico-sdk
```

### 2.2 Build

```bash
cd firmware/rp2350b
mkdir -p build && cd build
cmake .. -DPICO_SDK_PATH=$PICO_SDK_PATH \
         -DPICO_BOARD=pico2 \
         -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Output: `ghostblade_rp2350b.elf`, `ghostblade_rp2350b.uf2`, `ghostblade_rp2350b.bin`

### 2.3 Build with Debug Symbols

```bash
cmake .. -DPICO_SDK_PATH=$PICO_SDK_PATH \
         -DPICO_BOARD=pico2 \
         -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### 2.4 Memory Map

The firmware uses a custom linker script (`rp2350b_memmap.ld`) that allocates:

| Region | Base | Size | Purpose |
|--------|------|------|---------|
| FLASH | 0x10000100 | ~16 MB - 256 | Code + read-only data (XIP) |
| SRAM | 0x20000000 | 260 KB | .data, .bss, heap, stacks |
| DMA_RAM | 0x20041000 | 4 KB | DMA ring buffers (no CPU contention) |
| PSRAM | 0x11000000 | 2 MB | SDR IQ capture buffers, large allocations |

---

## 3. Building Linux Kernel Driver

### 3.1 Cross-Compilation

```bash
cd software/linux-drivers

# Set kernel source path and cross-compiler
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export KDIR=/path/to/rk3576/kernel/source

make -C $KDIR M=$(pwd) modules
```

Output: `apex_bridge.ko`

### 3.2 Native Build on Target

```bash
cd software/linux-drivers
make
```

### 3.3 Install

```bash
sudo make install
sudo depmod -a
```

### 3.4 Loading the Driver

```bash
sudo modprobe apex_bridge
# Or with custom SPI speed:
sudo insmod apex_bridge.ko spi_speed_hz=25000000
```

Verify:

```bash
dmesg | grep apex
ls /dev/apex_bridge0
```

---

## 4. Building libapex (Userspace C Library)

### 4.1 Native Build

```bash
cd software/libapex
make all    # Builds both static and shared libraries
```

Output: `build/libapex.a`, `build/libapex.so`

### 4.2 Cross-Compilation

```bash
cd software/libapex
make CC=aarch64-linux-gnu-gcc AR=aarch64-linux-gnu-ar all
```

### 4.3 Install

```bash
sudo make install PREFIX=/usr/local
sudo ldconfig
```

### 4.4 Linking Your Application

```bash
gcc -o my_app my_app.c -lapex
```

---

## 5. Building Python Bindings (pyapex)

```bash
cd software/libapex
pip3 install .          # Install system-wide
# Or for development:
pip3 install -e .       # Editable mode
python3 setup.py build_ext --inplace   # In-place build
```

### Usage

```python
import pyapex
dev = pyapex.open()
telem = dev.get_telemetry()
print(f"Battery: {telem['vbat_mv']} mV")
dev.close()
```

---

## 6. Running Unit Tests

```bash
cd tests

# Without cmocka (built-in self-test framework):
gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_spi_protocol test_spi_protocol.c
./test_spi_protocol

# With cmocka:
gcc -Wall -Wextra -std=c11 -o test_spi_protocol test_spi_protocol.c -lcmocka
./test_spi_protocol
```

---

## 7. Generating Hardware Files

### 7.1 Gerber Files

```bash
python3 tools/generate_gerbers.py --fab-note --zip
```

### 7.2 BOM

The interactive BOM is at `hardware/bom/ghostblade-bom-interactive.html`. The CSV BOM is at `hardware/bom/ghostblade-bom.csv`.

---

## 8. Reproducible Builds

For reproducible builds, set the following environment variables:

```bash
export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export TZ=UTC
export LC_ALL=C
```

The firmware CMakeLists and driver Makefile respect `SOURCE_DATE_EPOCH` for embedded timestamps.

---

## 9. Troubleshooting Build Issues

| Problem | Solution |
|---------|----------|
| `arm-none-eabi-gcc: not found` | Install `gcc-arm-none-eabi` package |
| `PICO_SDK_PATH not set` | Export `PICO_SDK_PATH` to your pico-sdk directory |
| `aarch64-linux-gnu-gcc: not found` | Install `gcc-aarch64-linux-gnu` package |
| `Kernel headers not found` | Set `KDIR` to your kernel source tree root |
| `cmake version too old` | Install CMake ≥ 3.15 (`pip install cmake --upgrade`) |
| `undefined reference to spi_protocol_*` | Ensure all `.c` files are listed in `CMakeLists.txt` |

For more troubleshooting, see [FAQ & Troubleshooting](faq-troubleshooting.md).