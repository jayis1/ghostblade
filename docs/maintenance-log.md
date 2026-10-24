<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->

# Maintenance log and verification limits

Author: jayis1

## 2026-10-24 — Complete device-tree overlay build coverage

The DTS Makefile previously enumerated seven overlays and omitted
`ghostblade-audio-overlay.dts`. It now discovers `ghostblade-*.dts`, excluding
only the base board source. Build, syntax validation, and cleanup therefore
cover the audio overlay and future overlays using that naming convention.
An unused, unavailable sound-binding include in the audio overlay was removed;
no GPIO assignments, clocks, supplies, or electrical connections were changed.

Three regression tests exercise the real Makefile in disposable copies:

- Every checked-in overlay, including audio, produces a DTBO with the DTB magic.
- `validate` invokes the compiler for every overlay.
- A temporary test overlay is discovered, validated, and removed by `clean`.

These tests also run at the end of `make -C tests run`. They explicitly skip
when `make`, `cc`, or `dtc` is missing; a skip is not build verification.

### Repeat the local checks

Run from the repository root with Python 3, GNU Make, a C preprocessor (`cc`),
and `dtc` installed:

```sh
python3 tests/test_dts_build.py -v
make -C software/dts clean
make -C software/dts all validate
make docs-check validate-dts validate-netlist
make -C tests run
make -C software/dts clean
```

Verified with DTC 1.7.2: all eight existing overlays compiled and passed the
Makefile's syntax-validation command; the three new regression tests, existing
host test suite, internal-link checker, and existing DTS/netlist scripts passed.
These are local software checks, not a reproducible firmware build or a board
qualification result. The Makefile's existing warning suppressions remain
unchanged.

### Open gaps and interpretation

| Area | Evidence / limitation | Required next evidence |
|------|-----------------------|------------------------|
| Base device tree | `rk3576.dtsi` is not checked in; this run explicitly skipped the base DTB. | Supply the intended kernel's source and binding headers, then compile and validate against that kernel. |
| Audio integration | The overlay references external labels including `gpio_mcu`, `i2s2_8ch`, `mclkout_i2s2`, `vcc_1v8`, and `vcc_3v3`; these labels are not defined in the checked-in board DTS. The overlay comments describe both MCU-side control and native host I2C2 access. | Resolve labels against the intended base tree; establish the actual codec control path from a reviewed schematic before applying the overlay. |
| Cross-reference scripts | Existing scripts check selected strings, numeric assignments, and documented model names. They do not establish complete electrical connectivity, kernel-binding validity, or actual STEP-file availability. | Export connectivity from reviewed schematics and compare endpoints; validate the compiled tree with the selected kernel bindings. |
| KiCad manufacturing | No `.kicad_sch` or `.kicad_pcb` source is tracked in this revision; `kicad-cli` was unavailable in this run. A project file, netlist, libraries, and rule files alone do not establish a manufacturable board. | Reviewed schematic and routed board, then real ERC/DRC results and package/pad/model inspection. |
| Firmware and deployment | No hardware flashing, RF operation, firmware cross-build, or power-sequence measurement was performed. | The supported SDK/toolchains, exact board revision, recovery procedure, and recorded bench results. |
| Documentation | Existing build/flashing guides and diagrams are not proof of implemented hardware. The internal-link checker checks inline link paths, not heading anchors or every Markdown link form. | Review hardware procedures against actual board artifacts and expand link-checker coverage separately. |

Do not use the README's target capabilities, conceptual diagrams, or a passing
syntax check as authorization to fabricate, power, or flash a board.
