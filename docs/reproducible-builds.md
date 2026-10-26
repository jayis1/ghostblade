<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# Reproducible Builds

These are controls for investigating reproducibility, not a claim of bit-for-bit
reproducible firmware, kernel modules, or board images. No complete target-build
comparison is recorded here. Timestamp settings alone do not pin dependencies,
compiler versions, paths, or generated metadata.

## Record inputs first

Record the GhostBlade commit, compiler versions, build flags, host environment,
Pico SDK commit and submodule revisions, and configured kernel commit and config
alongside each artifact. The repository does not currently prescribe one tested
SDK/kernel/toolchain version set. Do not replace those records with an unpinned
branch name. Use fresh build directories when changing tools or flags.

Run the read-only prerequisite checks from the repository root:

```sh
make check PICO_SDK_PATH="$HOME/pico-sdk" KDIR=/path/to/target/kernel/build
python3 tests/test_toolchain_check.py -v
```

`make check` checks compiler target triples, executable version probes, and
readable nonempty SDK/kernel marker files. It does not parse those files or
validate SDK submodules, kernel configuration, or a completed build. Synthetic
fixtures in the regression tests exercise failure propagation, not cross-builds.
See [build instructions](build-instructions.md) for supported entry points.

## Environment Variables

On a GNU/Linux host, from the repository root:

```sh
export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export KBUILD_BUILD_TIMESTAMP="$(date -u -d @${SOURCE_DATE_EPOCH} '+%Y-%m-%d %H:%M:%S')"
export PYTHONHASHSEED=0
```

`make reproducible` prints these exports; it does not apply them to the invoking
shell. Review and run them in the shell that will start the build.

## Firmware

With a reviewed Pico SDK and Arm bare-metal compiler available:

```sh
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/firmware/rp2350b/toolchain-arm-none-eabi.cmake" \
  -DPICO_SDK_PATH="$HOME/pico-sdk" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/rp2350b/build
```

The absolute toolchain-file path avoids ambiguity between source and build
paths. These target commands are not host-test results or flashing instructions.

## Kernel Driver

Use a fixed, configured target kernel build tree and the exported timestamp:

```sh
make -C software/linux-drivers \
  KDIR=/path/to/kernel/build \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu-
```

Kernel configuration, generated headers, compiler selection, and target release
must match the intended deployment; see the [flashing guide](flashing-guide.md).

## libapex

Run cleanup and build as separate commands, so an inherited parallel `MAKEFLAGS`
cannot schedule cleanup alongside compilation:

```sh
make -C software/libapex clean
make -C software/libapex all
make -C software/libapex DESTDIR="$PWD/out/rootfs" PREFIX=/usr install
```

For a comparison, repeat clean builds with identical recorded inputs and compare
`sha256sum` results for `software/libapex/build/libapex.a` and
`software/libapex/build/libapex.so`. Preserve each run's artifacts
outside the build directory. Compare artifacts, not build logs; investigate any
difference before claiming reproducibility. Host builds do not validate the
AArch64 ABI or board behavior.

## Device Tree

```sh
make -C software/dts clean
make -C software/dts all validate
```

Without the external `rk3576.dtsi`, only eight overlays are built and checked;
the base DTB is explicitly skipped. Follow the [external-kernel build
instructions](build-instructions.md#device-tree) for the base. Overlay syntax
checks do not validate kernel bindings, symbol resolution, or electrical wiring.

## Local Validation

```sh
make docs-check validate-dts validate-netlist
make -C tests run
```

Default netlist validation retains documented name-coverage warnings. The strict
mode currently fails on those discrepancies; see [hardware export
readiness](hardware-export-readiness.md). Do not rename electrical signals merely
to obtain a passing result.
