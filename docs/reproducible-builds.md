<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Reproducible Builds

This note documents the knobs already supported in the repository for deterministic local builds.

## Environment Variables

```bash
export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export KBUILD_BUILD_TIMESTAMP="$(date -u -d @${SOURCE_DATE_EPOCH} '+%Y-%m-%d %H:%M:%S')"
export PYTHONHASHSEED=0
```

## Firmware

The RP2350B firmware can be configured with the repository toolchain file:

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DCMAKE_TOOLCHAIN_FILE=firmware/rp2350b/toolchain-arm-none-eabi.cmake \
  -DPICO_SDK_PATH=$HOME/pico-sdk \
  -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/rp2350b/build
```

## Kernel Driver

Use a fixed kernel build tree and exported timestamp:

```bash
make -C software/linux-drivers \
  KDIR=/path/to/kernel/build \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu-
```

## libapex

`software/libapex/Makefile` supports staged installs and deterministic archives:

```bash
make -C software/libapex clean all
make -C software/libapex DESTDIR=$PWD/out/rootfs PREFIX=/usr install
```

## Device Tree

```bash
make -C software/dts validate
make -C software/dts all
```

## Validation

```bash
python3 tools/check_internal_links.py
python3 tools/validate_dts.py
python3 tools/validate_netlist.py
make -C tests run
```
