<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# Build Instructions

This document describes local build entry points for GhostBlade. These commands
are not evidence of a completed cross-build, bit-for-bit reproducibility, or
working hardware; see the [verification limits](maintenance-log.md).

## Check cross-build prerequisites

From the repository root:

```sh
make check PICO_SDK_PATH="$HOME/pico-sdk" KDIR=/path/to/target/kernel/build
```

The check returns nonzero if any probe fails and reports all results. Unlike
`make tests`, it checks cross-build prerequisites, not host test dependencies.
It defaults to `aarch64-linux-gnu-gcc`, even when `CROSS_COMPILE` is empty, and
checks compiler target triples with `-dumpmachine` rather than accepting a
native compiler's version string. A custom `CROSS_COMPILE` prefix selects its
`gcc`; individual executable paths can also be supplied:

```sh
make check CHECK_AARCH64_CC=/opt/toolchains/bin/aarch64-linux-gnu-gcc \
  CHECK_ARM_CC=/opt/toolchains/bin/arm-none-eabi-gcc \
  CHECK_DTC=/usr/bin/dtc CHECK_CMAKE=/usr/bin/cmake \
  PICO_SDK_PATH="$HOME/pico-sdk" KDIR=/path/to/target/kernel/build
```

Each compiler/version probe has a ten-second timeout. SDK and kernel checks
require `pico_sdk_init.cmake` and a kernel `Makefile`, respectively; an empty
directory is not enough. These marker files do not establish SDK submodule
availability, a configured target kernel, version compatibility, or a successful
build. No downloads, installation, flashing, or device access occur. Overrides
must name single executables, not shell command strings or compiler flags.

A missing CMake, cross-compiler, SDK marker, or kernel Makefile is an environment
prerequisite failure; install/select the relevant tools or source trees before
retrying. Host-only development can still use `make tests` and `make docs-check`
without the cross-build prerequisites. The sourced `software/toolchain.conf`
helper is advisory and does not replace this exit-status-bearing check.

## Build Matrix

| Component | Path | Build System | Primary Target |
|-----------|------|--------------|----------------|
| RP2350B firmware | `firmware/rp2350b/` | CMake + Pico SDK | `ghostblade.uf2` / `ghostblade.elf` |
| RK3576 kernel driver | `software/linux-drivers/` | Kbuild | `apex_bridge.ko` |
| Userspace C library | `software/libapex/` | GNU Make | `libapex.a` / `libapex.so` |
| Python bindings | `software/libapex/` | setuptools | `pyapex` extension |
| Device tree | `software/dts/` | `dtc` + Make | `.dtb` / `.dtbo` |
| Test suite | `tests/` | GNU Make | host-side test binaries |

## Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git python3 python3-pip python3-venv \
  gcc-aarch64-linux-gnu gcc-arm-none-eabi libnewlib-arm-none-eabi \
  device-tree-compiler libssl-dev flex bison
```

Optional but recommended:

- KiCad 8 for hardware edits
- OpenOCD and `picotool` for RP2350B flashing

## Reproducible Build Environment

Set deterministic timestamps before packaging or publishing artifacts:

```bash
export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export KBUILD_BUILD_TIMESTAMP="$(date -u -d @${SOURCE_DATE_EPOCH} '+%Y-%m-%d %H:%M:%S')"
export PYTHONHASHSEED=0
```

You can also source the repository helper:

```bash
source software/toolchain.conf
```

## Device Tree

Validate syntax:

```bash
make -C software/dts validate
```

Compile all DTB/DTBO outputs:

```bash
make -C software/dts all
```

If kernel include paths are required:

```bash
make -C software/dts all \
  DTS_INCLUDE_PATHS="-I/path/to/linux/include/dt-bindings -I/path/to/linux/arch/arm64/boot/dts/rockchip"
```

## RP2350B Firmware

### Configure

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DPICO_SDK_PATH=$HOME/pico-sdk \
  -DPICO_PLATFORM=rp2350 \
  -DCMAKE_BUILD_TYPE=Release
```

### Build

```bash
cmake --build firmware/rp2350b/build -j$(nproc)
```

### Outputs

- `ghostblade.uf2`
- `ghostblade.elf`
- `ghostblade.bin`
- `ghostblade.hex`
- `ghostblade.map`

### Toolchain-file driven configure

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DCMAKE_TOOLCHAIN_FILE=firmware/rp2350b/toolchain-arm-none-eabi.cmake \
  -DPICO_SDK_PATH=$HOME/pico-sdk
```

## RK3576 Kernel Driver

### Native build on target

```bash
make -C software/linux-drivers
```

### Cross-build on workstation

```bash
make -C software/linux-drivers \
  KDIR=/path/to/kernel/build \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu-
```

### Install into staging root

```bash
make -C software/linux-drivers \
  KDIR=/path/to/kernel/build \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  INSTALL_MOD_PATH=$PWD/out/modules \
  install
```

## libapex

### Native build

```bash
make -C software/libapex clean all
```

### Cross-build

```bash
make -C software/libapex \
  CC=aarch64-linux-gnu-gcc \
  AR=aarch64-linux-gnu-ar \
  RANLIB=aarch64-linux-gnu-ranlib \
  STRIP=aarch64-linux-gnu-strip \
  all
```

### Staged install

```bash
make -C software/libapex DESTDIR=$PWD/out/rootfs PREFIX=/usr install
```

Generated files include:

- `software/libapex/build/libapex.a`
- `software/libapex/build/libapex.so`
- `software/libapex/build/libapex.so.0`
- `software/libapex/build/libapex.pc`

## Python Bindings

Use a virtual environment on systems enforcing PEP 668:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install ./software/libapex
```

Editable install for development:

```bash
python3 -m pip install -e ./software/libapex
```

## Test Suite

Build and run all host-side tests:

```bash
make -C tests run
```

Build a single test target:

```bash
make -C tests test_spi_protocol
./tests/test_spi_protocol
```

## Repository-wide Validation

```bash
python3 tools/check_internal_links.py
python3 tools/validate_dts.py
python3 tools/validate_netlist.py
```

## Top-Level Convenience Targets

From the repository root:

```bash
make check
make validate
make validate-dts
make validate-netlist
make libapex
make tests
```

## Troubleshooting

### Firmware configure fails

- Confirm `PICO_SDK_PATH` points to a valid Pico SDK checkout.
- Confirm the Pico SDK submodules are initialized.

### `apex_bridge.ko` build fails

- Check `KDIR` matches the exact target kernel build tree.
- Verify `ARCH=arm64` and `CROSS_COMPILE=aarch64-linux-gnu-` for workstation builds.

### `libapex.so` installs but applications cannot find it

- Use `DESTDIR` for packaging.
- Ensure the final rootfs refreshes the dynamic linker cache or ships the library in a known runtime path.

### DTS compile fails on missing `dt-bindings`

- Pass `DTS_INCLUDE_PATHS` to the `software/dts/Makefile`.

## Related Docs

- [Getting Started](getting-started.md)
- [Flashing Guide](flashing-guide.md)
- [FAQ & Troubleshooting](faq-troubleshooting.md)
- [Documentation Index](index.md)
