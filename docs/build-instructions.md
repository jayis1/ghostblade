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
require readable, nonempty `pico_sdk_init.cmake` and kernel `Makefile` files,
respectively; empty files or directories are not enough. The check reads one
byte, not file syntax. Restore incomplete source trees rather than adding
placeholder markers. These marker files do not establish SDK submodule
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

Author: jayis1

Compile and syntax-check the available sources with GNU Make, a GCC-compatible
C preprocessor, and `dtc`:

```sh
make -C software/dts all validate
python3 tests/test_dts_build.py -v
```

Without the external `rk3576.dtsi`, these commands build/check the eight optional
overlays and explicitly skip the base board. This is not a complete board build,
binding check, or proof that the overlays apply to a particular kernel.

To request the base as well, select a reviewed kernel tree and supply both the
SoC include and the kernel's top-level include directory:

```sh
make -C software/dts all validate \
  RK3576_DTSI=/path/to/linux/arch/arm64/boot/dts/rockchip/rk3576.dtsi \
  DTS_INCLUDE_PATHS="-I/path/to/linux/include -I/path/to/linux/arch/arm64/boot/dts/rockchip"
```

This external-kernel example has not been built here. The include root must
contain `dt-bindings/`; pointing only at `include/dt-bindings` does not resolve
`#include <dt-bindings/...>`. The local compatibility headers are searched first;
review them against the selected kernel before treating compilation as meaningful.

### Incremental rebuilds and cleanup

The preprocessor writes `.pp.d` dependency files for transitive `#include`
inputs. Editing an included header or `.dtsi` therefore rebuilds affected `.pp`
and binary outputs; an unchanged build leaves the binaries untouched. These
intermediates are ignored by Git. Failed recipes remove their incomplete target
instead of letting a retry reuse it.

Dependency files track preprocessor includes, not native `dtc` `/include/`
directives, tool versions, command-line flag changes, or selection of a different
external kernel. Clean before changing any of those inputs, or when upgrading
from a checkout that generated outputs without dependency files:

```sh
make -C software/dts clean
make -C software/dts all validate
```

Cleanup removes known base outputs even if the external SoC include is no longer
available. None of these commands installs a tree, flashes hardware, or transmits
RF. See [hardware readiness](hardware-export-readiness.md) for wiring and release
validation limits.

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
make -C software/libapex all
```

Author: jayis1

The top-level `make libapex` builds the same C library incrementally; it does not
build the Python extension. Both entry points support parallel builds, for
example `make -j4 libapex`. The GCC-compatible compiler generates `.d` files for
the public header and transitive includes. Header edits rebuild affected objects
and libraries; unchanged inputs preserve artifact timestamps. The default goal
remains `all` after dependency files have been generated.

Dependencies do not track compiler/flag changes, tool versions, or a new
`PREFIX` for the generated pkg-config file. Clean explicitly before switching
those inputs or moving between native and cross-compilation. Keep cleaning and
building as separate commands: `clean all` in a single parallel Make invocation
can remove artifacts while another goal is building them.

```sh
make -C software/libapex clean
make -C software/libapex -j4 all
python3 tests/test_libapex_build.py -v
```

The six host regression tests build the production C library in temporary
directories and check header/transitive-header rebuilds, unchanged default and
parallel top-level builds, compilation-failure recovery, and cleanup/rebuild.
They do not open a device, build Python bindings, validate cross-compilation, or
establish bit-for-bit reproducibility. Existing build directories created before
dependency tracking was introduced should also be cleaned once.

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

### Documentation path diagnostics

Author: jayis1

`make docs-check` checks local inline links/images and same-line reference
definitions. Bare inline destinations may contain balanced nested parentheses;
backslash-escaped ASCII punctuation is decoded before URL percent escapes.
For example, `part(1).md`, `part\(1\).md`, and `part%281%29.md` refer to the same
filename. Diagnostics report the complete destination, not the prefix before
its first closing parenthesis. Missing paths fail both Python entry points and
the Make target; restore the file or correct the destination's spelling/case.

Run `python3 tests/test_doc_links.py -v` for disposable-fixture regressions,
also included in `make tests`. Top-level fenced code examples are excluded.
This remains a limited path checker: anchors, external URL availability,
undefined reference uses, inline/indented code, container fences, and complete
Markdown syntax validation are outside its scope. Unclosed inline destinations
are not treated as links; a passing result does not certify valid Markdown.

### Handshake assignment diagnostics

Author: jayis1

`make validate-dts` checks each existing `apex,int-req-gpio`,
`apex,host-rdy-gpio`, and `apex,mcu-reset-gpio` declaration against the documented
`gpio1` pins 8, 9, and 10, respectively, with `GPIO_ACTIVE_LOW`. A missing
property or any conflicting occurrence fails, even when another copy is correct;
commented-out declarations do not count. Restore/review the intended revision
rather than swapping names or changing wiring just to satisfy the checker.

Run `python3 tests/test_dts_validation.py -v` for the regression suite, also
included in `make tests`. These are selected text checks of the current symbolic,
decimal-pin representation, not a DTS parser or a schematic-derived comparison.
They do not resolve equivalent expressions, enforce node ownership, validate
bindings, or prove overlay compatibility. The overlay-target listing is
informational, not symbol-resolution validation. See the
[maintenance log](maintenance-log.md#bridge-gpio-assignment-checks) for evidence
and the [hardware readiness review](hardware-export-readiness.md) for release gaps.

### Firmware SPI0 assignment diagnostics

Author: jayis1

`make validate-dts` also requires exactly one definition of each existing
`PIN_SPI0_RX = 16`, `PIN_SPI0_CSN = 17`, `PIN_SPI0_SCK = 18`, and
`PIN_SPI0_TX = 19` macro in `firmware/rp2350b/include/board_pins.h` (the `=` here
is descriptive; the header uses `#define`). Swapping two names fails even though
the set of numbers remains 16–19. Missing and duplicate definitions fail,
including identical duplicates; commented definitions do not count. Diagnostics
name the macro to review, not just the aggregate pin set.

The checker accepts decimal literals with normal directive whitespace and
comments. It intentionally rejects aliases, hexadecimal values, parenthesized
values, and expressions rather than guessing their meaning. It does not run a
C preprocessor or evaluate conditional compilation; review such header changes
and extend the checker deliberately. The separate DTS SPI0 check now requires
exactly one live, flat `spi0_pins: spi0-pins` group, exactly one `pins` list
containing GPIO1_A0–A3 once each, and exactly one `function = "spi0"` property.
Comments cannot satisfy these requirements; duplicate groups/properties and
missing, repeated, or extra pins fail. Whitespace, comments between tokens, and
pin-list reordering are accepted because the list does not encode signal roles.
Nested groups, equivalent expressions, alternate node names, and alternative
binding representations require deliberate checker changes rather than guesses.
This text check does not establish node ownership, Rockchip pinmux/binding
validity, signal directions, or schematic connectivity. No firmware pin, net,
or DTS assignment is changed by this validation improvement.

`python3 tests/test_dts_validation.py -v` exercises all six pairwise SPI0 swaps,
missing/comment-only definitions, duplicates in either order, empty headers,
unsupported expressions, and accepted whitespace/comments through both the
production command and top-level Make target. These are disposable source
fixtures, not a firmware cross-build or electrical verification.

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
