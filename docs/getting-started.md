<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Getting Started

This guide is the fastest accurate path from a fresh clone to validated local builds for GhostBlade.

## What You Are Building

GhostBlade combines:

- **RK3576** Linux host SoC
- **RP2350B** real-time coprocessor
- **LMS7002M** SDR front end
- **CC1101** sub-GHz radio
- **ST25R3916** NFC front end
- **MT7922** Wi-Fi 6E / Bluetooth module

The repository includes hardware design assets, firmware, DTS files, a Linux SPI bridge driver, `libapex`, Python bindings, and test tooling.

## Recommended First Steps

From the repository root:

```bash
make check
python3 tools/check_internal_links.py
python3 tools/validate_dts.py
python3 tools/validate_netlist.py
make -C tests run
```

Those commands verify the repository before you try cross-builds.

## Repository Map

```text
ghostblade/
├── README.md
├── GhostBlade.mf
├── hardware/                # KiCad project, netlist, BOM, DRC/ERC rules
├── firmware/rp2350b/        # RP2350B firmware (CMake + Pico SDK)
├── software/
│   ├── dts/                 # RK3576 base DTS + overlays
│   ├── linux-drivers/       # apex_bridge kernel module
│   ├── libapex/             # userspace C library + Python bindings
│   └── toolchains/          # cross-build CMake toolchain files
├── tests/                   # host-side unit tests and HIL scripts
├── tools/                   # validators and project maintenance helpers
└── docs/                    # project documentation
```

## Host Dependencies

### Debian / Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git python3 python3-pip python3-venv \
  gcc-aarch64-linux-gnu gcc-arm-none-eabi libnewlib-arm-none-eabi \
  device-tree-compiler libssl-dev flex bison
```

### Pico SDK

```bash
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk
git submodule update --init
```

## Toolchain Environment

You can either export variables manually or source the helper file:

```bash
source software/toolchain.conf
```

Important variables:

- `GHOSTBLADE_PICO_SDK_PATH`
- `GHOSTBLADE_AARCH64_CROSS_COMPILE`
- `GHOSTBLADE_KERNEL_SRC`

## Build Order

### 1. Device-tree validation

```bash
make -C software/dts validate
python3 tools/validate_dts.py
```

### 2. libapex

```bash
make -C software/libapex clean all
```

Outputs:

- `software/libapex/build/libapex.a`
- `software/libapex/build/libapex.so`
- `software/libapex/build/libapex.so.0`
- `software/libapex/build/libapex.pc`

### 3. RP2350B firmware

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DPICO_SDK_PATH=$HOME/pico-sdk \
  -DPICO_PLATFORM=rp2350 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/rp2350b/build -j$(nproc)
```

Outputs:

- `firmware/rp2350b/build/ghostblade.uf2`
- `firmware/rp2350b/build/ghostblade.elf`
- `firmware/rp2350b/build/ghostblade.bin`
- `firmware/rp2350b/build/ghostblade.hex`

### 4. RK3576 Linux driver

Native build on target:

```bash
make -C software/linux-drivers
```

Cross-build on workstation:

```bash
make -C software/linux-drivers \
  KDIR=/path/to/kernel/build \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu-
```

### 5. Host-side tests

```bash
make -C tests run
```

## Using the CMake Toolchain Files

For reproducible or scripted builds, the repository includes:

- `software/toolchains/rk3576-aarch64.cmake`
- `firmware/rp2350b/toolchain-arm-none-eabi.cmake`

Example:

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DCMAKE_TOOLCHAIN_FILE=firmware/rp2350b/toolchain-arm-none-eabi.cmake \
  -DPICO_SDK_PATH=$HOME/pico-sdk
```

## Flashing Summary

### RP2350B via BOOTSEL

```bash
cp firmware/rp2350b/build/ghostblade.uf2 /media/$USER/RPI-RP2/
```

### RP2350B via OpenOCD

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "program firmware/rp2350b/build/ghostblade.elf verify reset exit"
```

### Driver bring-up on RK3576

```bash
sudo insmod software/linux-drivers/apex_bridge.ko
ls -l /dev/apex_bridge0
ls -l /sys/class/apex/apex_bridge0
```

## First Hardware Bring-Up Checks

1. RP2350B flashes and reboots cleanly.
2. RK3576 sees the SPI bridge node from the installed DTB/overlay set.
3. `apex_bridge.ko` probes successfully.
4. `/dev/apex_bridge0` appears.
5. Sysfs entries appear under `/sys/class/apex/apex_bridge0/`.

## Common Pitfalls

### `dtc` cannot resolve includes

Pass kernel include paths explicitly:

```bash
make -C software/dts validate \
  DTS_INCLUDE_PATHS="-I/path/to/linux/include/dt-bindings -I/path/to/linux/arch/arm64/boot/dts/rockchip"
```

### Pico SDK not found

Set `PICO_SDK_PATH` or use the provided toolchain file and pass `-DPICO_SDK_PATH=...`.

### Wrong firmware output names in old notes

Current firmware outputs are named `ghostblade.*`, not `ghostblade_rp2350b.*`.

### Driver builds against the wrong kernel tree

Always pass `KDIR=/path/to/kernel/build` for workstation cross-builds.

## Next Reading

- [Build Instructions](build-instructions.md)
- [Flashing Guide](flashing-guide.md)
- [Contributor Onboarding](getting-started-contributors.md)
- [FAQ & Troubleshooting](faq-troubleshooting.md)
- [Documentation Index](index.md)
