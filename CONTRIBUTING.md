<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Contributing to GhostBlade

Thanks for helping with GhostBlade (Project NullSpectre). This repository contains hardware design files, RP2350B firmware, RK3576 device-tree sources, Linux driver code, userspace libraries, and manufacturing/test documentation. Contributions are most useful when they keep those layers in sync.

## Ground Rules

- Be respectful, technical, and specific.
- Keep documentation, schematic/netlist assumptions, DTS files, and firmware pin maps aligned.
- Do not add repository-hosted automation files.
- Prefer small, reviewable commits with a clear scope.

## Recommended Workflow

1. Sync your local clone with `main`.
2. Make your change.
3. Run the relevant local validation steps from the checklist below.
4. Update impacted documentation.
5. Share the commit, patch series, or repository link with maintainers for review and integration.

## Project Areas

### Hardware

For changes under `hardware/`:

1. Use KiCad 8 or newer.
2. Keep symbol names, footprint names, and net names consistent with `GhostBlade.mf`.
3. Verify 3D model references for changed footprints.
4. Re-run ERC/DRC using:
   - `hardware/drc/ghostblade-erc-rules.kicad_erc`
   - `hardware/drc/ghostblade-drc-rules.kicad_drc`
5. Update hardware-facing docs when pinout, power, RF routing, or component choices change.

### RP2350B Firmware

For changes under `firmware/rp2350b/`:

1. Preserve the SPI framing protocol in `spi_protocol.c` and `apex_bridge_regs.h`.
2. Keep `include/board_pins.h` aligned with the schematic manifest and DTS GPIO mapping.
3. Respect the custom memory layout in `rp2350b_memmap.ld`.
4. Treat watchdog, brownout, and peripheral power sequencing as board-level behavior, not isolated code paths.

### Linux Driver and Userspace

For changes under `software/`:

1. Keep ioctl/sysfs documentation aligned with implementation.
2. Ensure cross-build settings still work for RK3576/aarch64.
3. When changing the bridge protocol, update both kernel and userspace headers.
4. Keep install paths, SONAMEs, and pkg-config metadata coherent.

### Device Tree

For changes under `software/dts/`:

1. Keep `ghostblade-rk3576.dts` aligned with `GhostBlade.mf` and `board_pins.h`.
2. Put optional hardware in overlays instead of bloating the base DTS.
3. Ensure pinctrl groups, regulator dependencies, and interrupt polarity match the documented hardware behavior.
4. Keep comments focused on actual bus topology and populated hardware.

### Documentation

For changes under `docs/` or top-level Markdown:

1. Use explicit units.
2. Prefer exact filenames and commands that exist in the repository.
3. Verify internal links before submitting.
4. When changing build or flashing steps, make sure the commands match the current Makefiles/CMake files.

## Local Validation Checklist

Run the subset that applies to your change.

### Repository-wide

```bash
python3 tools/check_internal_links.py
python3 tools/validate_dts.py
python3 tools/validate_netlist.py
```

### Test suite

```bash
make -C tests run
```

### DTS compilation

```bash
make -C software/dts validate
```

### Driver build

```bash
make -C software/linux-drivers KDIR=/path/to/kernel/build
```

### libapex build

```bash
make -C software/libapex clean all
```

### Firmware configure/build

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DPICO_PLATFORM=rp2350
cmake --build firmware/rp2350b/build
```

## Commit Guidance

Use a short subject that states the subsystem and change.

Examples:

- `docs: align build and flashing guides with current outputs`
- `dts: add wakeup and regulator metadata for bridge peripherals`
- `build: make libapex installs reproducible and toolchain-aware`
- `hw: document symbol-footprint-3d coverage`

## What to Update Together

If you touch one of these, check the related files too:

- **Bridge GPIOs / pin numbers** → `GhostBlade.mf`, `board_pins.h`, `ghostblade-rk3576.dts`, pin assignment docs
- **RF peripheral topology** → DTS overlays, firmware init code, timing docs, FAQ
- **Build commands / outputs** → `README.md`, `docs/getting-started.md`, `docs/build-instructions.md`, `docs/flashing-guide.md`
- **KiCad libraries / 3D models** → footprint library, 3D model reference docs, validation tooling

## License

By contributing, you agree that your work is licensed under the repository's existing licensing split:

- Hardware: CERN-OHL-S v2
- Firmware/software: GPL-2.0-or-later or file-specific SPDX identifier
- Documentation: CC-BY-SA 4.0

## Security

Please follow [SECURITY.md](SECURITY.md) for vulnerability disclosure. Do not post undisclosed security issues in public trackers.