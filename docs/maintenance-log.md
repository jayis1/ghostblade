<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->

# Maintenance log and verification limits

Author: jayis1

## Exact manifest net-name coverage

Author: jayis1

Basis: checked-in manifest/netlist text, not a reviewed schematic export.
The existing netlist checker printed net prefixes without checking full-name
coverage. It now compares exact names, warns on missing names by default, and
provides `make validate-netlist-strict` to make those findings fail the command.
Empty parsed name sets fail in both modes; repeated in-process runs reset their
diagnostics. No automatic aliasing or electrical changes were made.

The repository currently matches 20 of 34 manifest names; the 14 unmatched names
are listed in [hardware export readiness](hardware-export-readiness.md).
This is a spelling/coverage result, not a connectivity or KiCad syntax verdict.
The README now exposes design-stage status before its feature descriptions and
records the warning/strict-failure distinction rather than an all-passed claim.

Ten regression tests cover exact matches, extra board nets, same-prefix and
case differences, default warnings, missing/empty/unrecognized inputs,
deterministic diagnostics, repeated runs, retained component errors, and strict
Make exit propagation.
They run through `make -C tests run` with synthetic text fixtures. Host suite,
link-path checks, selected DTS checks, and eight overlay builds/syntax checks
passed. Strict net-name validation intentionally fails on the existing findings.
The base DTS, real ERC/DRC, cross-builds, and hardware operation remain unverified
for the source/tool limitations documented below.

## Fabrication export failure handling and readiness review

Author: jayis1

The exporter previously returned success without a board and continued after
failed layer, drill, and placement commands. It now fails before notes or ZIP
creation on those paths, requires a new or empty export directory, and offers
explicit `--notes-only` for draft design notes. Notes carry `jayis1` authorship
and a draft-status warning. ZIP creation no longer includes each `.gbr` twice.

Ten host regression tests exercise production control flow, stage failures,
timeouts, missing executables, notes-only mode, occupied paths, and packaging
using explicitly synthetic subprocess/file fixtures. They run through
`make -C tests run`; they do not establish that KiCad can load or export the
current design assets. See [hardware export readiness](hardware-export-readiness.md)
for existing netlist syntax/name discrepancies, incomplete footprint examples,
model gaps, and unvalidated rule integration. No electrical files, power delays,
or flashing commands were changed.

Verification in this run: the exporter tests, full host suite, documentation
link-path checker, and selected DTS/netlist cross-reference checks passed.
All eight overlays compiled and syntax-checked. The base DTB remained skipped
without `rk3576.dtsi`; real ERC/DRC and board exports were not run because
editable schematic/PCB sources and `kicad-cli` are absent. Cross-builds and
hardware operation are not validated.

## Reference-definition link path coverage

Author: jayis1

The shared documentation checker now validates same-line reference-definition
paths as well as inline destinations. Previously, `[guide][build]` could point
to a missing file through `[build]: missing.md` without failing `make docs-check`.
Both command entry points now reject that missing destination, including image
assets. Definitions are checked even when unused, so stale paths cannot hide in
an unused reference block.

Supported definitions have zero to three leading spaces, an unquoted URL or an
angle-bracket destination, and an optional same-line quoted or parenthesized
title. Relative, repository-root, encoded filenames, queries, and fragments use
the same path rules as inline links. External URLs are not fetched.

Run `python3 tests/test_doc_links.py -v` and `make docs-check` from the repository
root. All 16 regression tests and the full `make -C tests run` host suite passed
in this run. Selected DTS/netlist cross-reference checks also passed. These do
not establish electrical correctness, model availability, or a successful
firmware cross-build; no hardware assignments or flashing procedures changed.

This is a path checker, not a Markdown renderer. Undefined reference labels,
multiline definitions, heading anchors, HTML links, and reference precedence are
not validated. Four-space-indented definitions are ignored; fenced-code examples
are not filtered. Same-line definitions containing parentheses in filenames are
supported, but nested-parenthesis inline destinations remain outside its scope.

## Cross-build prerequisite exit status and target checks

The previous top-level `make check` labeled native `gcc` as AArch64 when
`CROSS_COMPILE` was empty and returned success even after reporting missing
tools. It now invokes `tools/check_toolchain.py`, which checks target triples,
bounded version probes, and SDK/kernel marker files. All results are reported;
a failed probe makes the command fail. Custom compiler prefixes and individual
executable paths are supported without changing native driver build defaults.

Repeat the regression checks from the repository root:

```sh
python3 tests/test_toolchain_check.py -v
make -C tests run
make docs-check validate-dts validate-netlist
make -C software/dts all validate
```

Seven regression tests exercise the actual checker and Makefile using temporary
executable fixtures, including wrong targets, missing executables, unsuccessful
or empty probes, empty source directories, paths with spaces, custom prefixes,
and exit-status propagation. Fixture success tests do not demonstrate installed
cross-compilers or successful firmware builds.

On this host, the corrected `make check` reports missing AArch64 and Arm
bare-metal compilers, CMake, Pico SDK, and the target kernel build tree and
correctly exits nonzero. DTC 1.7.2 is available. Baseline and updated host suites,
inline documentation links, selected DTS/netlist checks, and all eight overlay
builds/syntax checks passed. The base DTB remains skipped without `rk3576.dtsi`.
No electrical assignments, libraries, rule files, or flashing commands were
changed; the hardware and deployment evidence gaps below remain unresolved.
See [build instructions](build-instructions.md) for prerequisites and limits.

## Inline documentation link checks

The two link-checker commands now share one implementation. Previously,
`tools/check_links.py` searched from the current working directory, only examined
Markdown destinations, and returned success even when it reported broken links.
It is now a compatibility entry point for `tools/check_internal_links.py` and
returns a nonzero status for missing paths. Both locate the repository from the
script location, rather than the shell's working directory.

Run either command from any directory using its absolute path, or from the
repository root:

```sh
make docs-check
python3 tools/check_links.py
python3 tests/test_doc_links.py -v
```

Both check inline links and images, including non-Markdown assets, relative and
repository-root paths, URL-encoded filenames, angle-bracket destinations, and
optional quoted titles. URL schemes, network destinations, and fragment-only
links are not treated as local files. `--root /path/to/checkout` explicitly selects
another documentation tree; a nonexistent root is an error. Neither command
makes network requests or changes files.

Nine regression tests run the real command entry points against disposable
trees, including missing-document and missing-image failures, execution from a
different working directory, metadata exclusion, and invalid-root handling.
They also run through `make -C tests run`.

At that stage, scope remained deliberately limited: heading anchors,
reference-style links, HTML links, nested-parenthesis destinations, and rendered
Markdown semantics were not validated. Reference-definition path coverage has
since been added as described above. Inline-looking links in code examples are
still checked.
A passing result establishes only that recognized local destinations exist,
not that hardware instructions or external URLs are correct.

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
