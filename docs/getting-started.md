<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# Getting Started

Author: jayis1

Start with host-only checks. GhostBlade is a design-stage project, not a
hardware-tested release: editable schematics and a routed PCB are not tracked.
Firmware, Linux driver, and device-tree sources are available, but passing host
tests does not establish a working board. See the
[hardware readiness review](hardware-export-readiness.md).

## First local checks

Clone the repository and enter its root:

```sh
git clone https://github.com/jayis1/ghostblade.git
cd ghostblade
```

The checks below need Python 3, GNU Make, and a native C compiler. The test suite
also builds a production Python extension and exercises builds using a
GCC-compatible compiler/preprocessor and `dtc`. On Debian/Ubuntu, install
`git`, `build-essential`, `python3`, `python3-dev`, `python3-setuptools`, and
`device-tree-compiler`. Review scripts before running them. No board, SDK,
cross-compiler, root access, flashing, or RF transmission is required to run
these checks after their dependencies have been installed:

```sh
make docs-check validate-dts validate-netlist
make -C tests run
make -j4 libapex
make -C software/dts all validate
```

Interpret the results separately:

| Check | What success establishes | What it does not establish |
|-------|--------------------------|----------------------------|
| `docs-check` | Existing local inline/reference-definition link paths | Heading anchors, external URLs, or complete Markdown parsing |
| `validate-dts` | Selected text-based GPIO, memory, and pinctrl checks | Kernel bindings, schematic endpoints, or electrical correctness |
| `validate-netlist` | Selected component and assignment checks | A complete KiCad parse or identical manifest/netlist coverage |
| `tests run` | Host tests and their embedded build regressions | Firmware cross-build or physical peripheral operation |
| `libapex` | Native userspace C-library build | Python-extension build or communication with a device |
| DTS `all validate` | Syntax compilation of available sources | Base-tree overlay application or hardware compatibility |

The netlist check currently warns about 14 unmatched exact names (20/34 coverage).
`make validate-netlist-strict` intentionally returns failure for those findings;
do not rename electrical signals just to clear them. See the
[readiness review](hardware-export-readiness.md#exact-net-name-coverage).

Without an external `rk3576.dtsi`, the DTS command builds eight optional overlays
and skips the base DTB. A successful exit does not mean
`ghostblade-rk3576.dtb` was produced.

## Repository map

| Path | Contents |
|------|----------|
| `GhostBlade.mf` | Design manifest; compare with source artifacts, not a hardware sign-off |
| `hardware/` | Project settings, text netlist, BOM, draft libraries and rules |
| `firmware/rp2350b/` | RP2350B firmware sources and CMake configuration |
| `software/dts/` | RK3576 base source, optional overlays, compatibility includes |
| `software/linux-drivers/` | Linux SPI bridge driver |
| `software/libapex/` | C API, Python bindings, and userspace helpers |
| `tests/` | Host regressions and separate hardware-test scripts |
| `docs/` | Design notes, build instructions, and validation limits |

## Move to target builds deliberately

Host checks are independent of the cross-build prerequisite probe. When ready,
select the SDK, compiler versions, and exact target kernel build tree, then run:

```sh
make check PICO_SDK_PATH="$HOME/pico-sdk" KDIR=/path/to/target/kernel/build
```

This command must fail if required tools or marker files are missing; it is not
an installation command or a successful cross-build. The
[build instructions](build-instructions.md) describe each target independently:
RP2350B firmware, AArch64 kernel module, userspace library, and Python extension.
The [reproducible-build notes](reproducible-builds.md) describe environment controls;
these alone do not prove bit-for-bit reproducibility.

Do not source environment helpers or download an unpinned SDK as a substitute
for selecting a reviewed build baseline. Record tool versions and source
revisions with any artifacts shared for review.

### Building the complete device tree

Select a reviewed kernel tree and pass the include root containing
`dt-bindings/`, not `include/dt-bindings` itself:

```sh
make -C software/dts all validate \
  RK3576_DTSI=/path/to/linux/arch/arm64/boot/dts/rockchip/rk3576.dtsi \
  DTS_INCLUDE_PATHS="-I/path/to/linux/include -I/path/to/linux/arch/arm64/boot/dts/rockchip"
```

This is a target-build template, not a locally verified base-tree build.
Local compatibility headers take precedence; review them against the selected
kernel. Overlay syntax success does not verify that exported symbols exist in
the base or that optional configurations can be enabled together.

### Cleaning before changing toolchains

Normal library builds are incremental. Clean when changing compiler, flags,
architecture, or tool versions, and keep clean/build invocations separate:

```sh
make -C software/libapex clean
make -C software/libapex -j4 all
```

Do not combine independent `clean all` goals in one parallel Make invocation:
cleanup can delete outputs while they are being built. See
[incremental dependency limits](build-instructions.md#libapex).

## Troubleshooting the first checks

- Missing `dtc`: install/select the device-tree compiler before running the DTS
  build regressions; a skipped check is not evidence of a passing build.
- Missing SDK, cross-compiler, or target kernel: expected prerequisite failures
  from `make check` do not prevent host-only development.
- Missing DTS or firmware pin header: restore files from the same reviewed
  revision. Do not add empty placeholders to bypass the netlist validator.
- Missing base DTB after a successful overlay build: supply the external SoC
  include and rebuild; never deploy a stale DTB from another kernel revision.
- Net-name warnings: reconcile schematic endpoints before changing manifest,
  DTS, or firmware assignments.

## Before using hardware

Read the [flashing guide](flashing-guide.md) before writing firmware, installing a
module, or modifying boot files. It distinguishes unverified deployment templates
from checks that run on the workstation. Confirm board revision, power/reset
requirements, debug access, backup, and recovery before any hardware operation.

## Next reading

- [Build Instructions](build-instructions.md)
- [Contributor Onboarding](getting-started-contributors.md)
- [FAQ & Troubleshooting](faq-troubleshooting.md)
- [Documentation Index](index.md)
