<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade — Development Environment Quick-Setup

This is a condensed quick-setup guide. For detailed instructions, see
[Getting Started](getting-started.md) and [Build Instructions](build-instructions.md).

## One-Line Install (Ubuntu 22.04+)

```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake ninja-build git python3 python3-pip \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  gcc-aarch64-linux-gnu device-tree-compiler \
  cppcheck clang-format codespell \
  linux-headers-$(uname -r) kmod
```

## Pico SDK

```bash
git clone --depth 1 --branch 2.0.0 \
  https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
cd /opt/pico-sdk && git submodule update --init --recursive
export PICO_SDK_PATH=/opt/pico-sdk
```

## Build Everything

```bash
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
source software/toolchain.conf    # Verify toolchain

# Firmware (RP2350B)
make firmware PICO_SDK_PATH=/opt/pico-sdk

# Kernel driver (native or cross-compile)
make driver KDIR=/path/to/kernel/source

# Userspace library
make libapex

# All tests
make tests

# Device tree
make dtb

# Verify DTS syntax
make validate
```

## Flash and Verify

```bash
# Flash RP2350B (BOOTSEL mode)
cp firmware/rp2350b/build/ghostblade_rp2350b.uf2 /media/$USER/RPI-RP2/

# Load kernel driver
sudo insmod software/linux-drivers/apex_bridge.ko

# Quick smoke test
python3 -c "import pyapex; d=pyapex.ApexDevice(); print(d.get_telemetry()); d.close()"
```

## Verify DTS Cross-References

```bash
make validate-dts    # Cross-reference DTS GPIOs with firmware and schematic
```

## Editor Configuration

The project includes `.editorconfig`, `.clang-format`, and `.markdownlint.json` for
consistent formatting. Your editor should pick these up automatically.

- **C code**: Linux kernel style (8-column tabs, 80/100 column limit)
- **Markdown**: `.markdownlint.json` rules (line length 120, no inline HTML)
- **General**: `.editorconfig` (UTF-8, LF line endings, trailing newline)

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `PICO_SDK_PATH not set` | `export PICO_SDK_PATH=/opt/pico-sdk` |
| `arm-none-eabi-gcc: not found` | `sudo apt install gcc-arm-none-eabi` |
| `aarch64-linux-gnu-gcc: not found` | `sudo apt install gcc-aarch64-linux-gnu` |
| `dtc: not found` | `sudo apt install device-tree-compiler` |
| `cmake version too old` | `pip3 install cmake --upgrade` |

See [FAQ & Troubleshooting](faq-troubleshooting.md) for more.