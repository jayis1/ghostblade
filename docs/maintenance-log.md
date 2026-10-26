<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->

# Maintenance log and verification limits

Author: jayis1

## SPI character-device maximum write boundary

Author: jayis1

The production driver's `write()` buffer contains an opcode byte followed by
payload bytes. The previous count guard incorrectly included the opcode in the
4092-byte payload allowance, rejecting a valid 4093-byte write. It now allows
one opcode plus the full payload while rejecting larger counts before allocation
or user-memory access. The wire format and maximum frame size are unchanged.

Verification performed:

- The new `tests/test_kernel_write_production.py` compiles the actual production
  write function with host API stubs, warnings as errors, and undefined-behavior
  and bounds sanitizers. It failed on the old maximum-count guard and passes
  after the fix.
- Cases include opcode-only, small, near-maximum and maximum writes, oversized
  and `SIZE_MAX` counts, zero count, each allocation failure, copy failure,
  frame-build failure, transport failures, subsequent recovery and absent devices.
  Allocation accounting checks cleanup; the stub does not verify secure wiping.
- `make -C tests run` passes before and after the change. The new regression is
  included in that target. The separate GhostWisp Python suite passes 158 tests.
- `make libapex` passes. `make -C software/dts all` succeeds with existing overlay
  outputs; the base DTB is skipped because the external `rk3576.dtsi` is absent.
- Linux v6.12 checkpatch with `--no-tree --strict` reports no errors, warnings or
  checks on the driver patch. Full-file checkpatch still reports pre-existing
  style debt (747 errors and 1471 warnings, versus 772 and 1528 before the fix).
  Only the modified write function was normalized to kernel indentation, comment
  layout and equivalent `u8`/`u16` types; a whole-driver reformat was avoided.
- Cppcheck 2.17.1 with exhaustive checking, `--platform=unix32`, C11 and warning,
  style, performance and portability checks completes without diagnostics on
  the 15 main firmware C files. Separate analysis of the seven GhostWisp C files
  reports existing constant-condition and const-qualification style findings;
  these remain unresolved. Missing SDK headers and the Unix-width approximation
  prevent treating this as a complete target analysis.
- Independent read-only review found no security or logic errors in the patch
  and regression. `git diff --check` passes.

This is a bounded driver correction, not a completed all-source security audit.
`make check` fails because the AArch64 and Arm bare-metal compilers, CMake, Pico
SDK and configured kernel build tree are absent. No target firmware/module build,
physical SPI transaction, RF transmission or board testing was performed. No new
protocol handlers, power-state, watchdog, brownout or DMA behavior was invented;
hardware-dependent feature work and repository-wide warning cleanup remain open.

## Cross-build marker checks and reproducibility guidance

Author: jayis1

The prerequisite checker accepted zero-byte SDK/kernel marker files. It now
requires a readable nonempty regular-file target, catches file-access errors,
and reports the failed marker while continuing other probes. Reading one byte
is not a syntax or source-tree completeness check; symlinks to usable source
files remain supported. The existing assignment sources and build targets are
unchanged.

Verification performed:

- New CLI and Make regressions reproduced empty-marker success before the fix
  (four failing subcases).
- `python3 tests/test_toolchain_check.py -v` passes all 10 tests, covering empty
  files, directory markers, missing sources/tools, wrong compiler triples,
  nonzero/empty probe output, and paths containing spaces. Successful fixture
  probes are orchestration evidence, not real target builds.
- `make -C tests run` passes the full host suite.
- `make docs-check validate-dts validate-netlist` passes with the existing 14
  exact-name warnings (20/34 coverage); selected text checks do not certify wiring.
- `make -C software/dts all validate` builds and syntax-checks eight overlays;
  the base DTB is skipped because the external `rk3576.dtsi` is absent.
- Two consecutive clean native libapex builds produce matching SHA-256 sums for
  `software/libapex/build/libapex.a` and `software/libapex/build/libapex.so`.
  This is a same-directory, same-host comparison, not target reproducibility.
- `make reproducible` prints environment exports; `git diff --check` passes.

The reproducibility guide now separates controls from guarantees, requires
recorded dependency/tool revisions, uses an absolute firmware toolchain-file
path, and separates cleanup from compilation to avoid parallel-goal races.
README and build guidance describe the stronger marker checks.

`make check` correctly fails on this host: AArch64 and Arm bare-metal compilers,
CMake, Pico SDK, and the configured kernel tree are missing. KiCad CLI is also
unavailable. No target build, ERC/DRC, binding validation, flashing, or physical
verification was performed. Existing hardware-readiness findings remain open;
no symbols, footprints, timing values, or pin assignments were invented.

## Named firmware SPI0 validation (2026-10-25)

Author: jayis1

The DTS validator compared firmware SPI0 definitions as an unordered set, so
all six pairwise signal swaps passed. It now checks each existing macro by name
(RX 16, CSN 17, SCK 18, TX 19), requires exactly one definition, strips comments,
and accepts ordinary directive whitespace. Missing, duplicate, commented-only,
or unsupported expression definitions fail with a named diagnostic. These
numbers preserve the existing header; they are not newly verified wiring.

Verification performed:

- The new pairwise-swap regression failed for all six swaps before the fix.
- `python3 tests/test_dts_validation.py -v` passes all 16 tests. The seven new
  test methods exercise production Python and Make entry points using disposable
  copies of real repository sources; no target hardware is accessed.
- `make -C tests run` passes the host suite, including these regressions.
- `make docs-check validate-dts validate-netlist` passes with the existing 14
  exact-net-name warnings (20/34 coverage). `make validate-netlist-strict` still
  fails on those 14 discrepancies; no nets were renamed to hide the findings.
- A clean `make -C software/dts all validate` builds and syntax-checks eight
  overlays. The base DTB remains skipped without the external `rk3576.dtsi`.
- `git diff --check` passes.

README evidence and [build diagnostics](build-instructions.md#firmware-spi0-assignment-diagnostics)
now describe the stronger check and its limits. The validator's module summary
no longer claims overlay target resolution or schematic/LED checks it does not
perform. Its separate DTS SPI0 scan still recognizes comment text; this patch
does not establish pinmux tuples, host/MCU signal direction, or electrical safety.

The repository already has setup, contribution, build, flashing, FAQ, and timing
and power diagrams; no duplicate guides or invented timing values were added.
Editable schematics, a routed PCB, and STEP models remain absent. KiCad is not
available for ERC/DRC. `make check` fails because Arm/AArch64 compilers, CMake,
Pico SDK, and the configured kernel tree are missing. Target builds, binding
validation, actual overlay application, flashing, RF operation, and physical
verification were not performed. Hardware and devicetree assignments, licensing,
and fabrication constraints were left unchanged pending authoritative evidence.

## SPI0 ISR rejection cleanup and reserved-field validation

Author: jayis1

The secondary SPI0 ISR parser retained assembled NFC/configuration bytes after
payload CRC rejection and accepted nonzero reserved header bytes with a valid
CRC. Rejection now wipes the assembled bytes through volatile stores, clamps the
wipe to buffer capacity even on defensive overflow paths, clears frame metadata,
and preserves next-sync recovery. Reserved bytes 4–7 must be zero before payload
acceptance. Complete frames awaiting main-loop consumption are unchanged. A new
internal reserved-field rejection counter does not change the public statistics
API. No wire format or hardware assignment was changed.

Verification:

- The new production-source host regression failed before the fix because
  rejected payload bytes remained in the buffer. It now passes under
  `-O2 -Wall -Wextra -Werror -fsanitize=undefined,bounds`.
- Tests cover empty, one-byte, 256-byte and maximum payloads, split trailers,
  each reserved byte with a recomputed header CRC, bad header CRC, oversized
  advertised lengths, defensive overflow paths, pending-frame ownership and
  immediate valid-frame recovery. The harness compiles the complete ISR source,
  replaces ARM memory barriers only, and supplies independent bitwise CRCs.
  It does not execute MMIO, emulate IRQ timing or validate concurrency/electrics.
- `make -C tests run` passes before and after the change; the new test is included.
  The separate GhostWisp Python suite passes.
- Cppcheck 2.17.1 passes the 15 main firmware C sources with
  `--enable=warning,style,performance,portability --check-level=exhaustive
  --platform=unix32 --inline-suppr --error-exitcode=1
  -Ifirmware/rp2350b/include firmware/rp2350b/src`.
  This width approximation without SDK include coverage is not a target build.
- The broader initial Cppcheck scan also included seven GhostWisp C sources;
  it reports existing constant-condition and const-qualification style findings
  in the boot, I2C and SPI sources. Those findings are not fixed in this patch.
- Linux v6.12 checkpatch full-file analysis reports 772 errors and 1528 warnings
  for the unchanged bridge C source, and 1 error and 49 warnings for its unchanged
  header. Repository-wide warning cleanup is not complete; no kernel code is
  changed by this patch.
- `make libapex dtb validate-dts` passes the host library build, overlay compilation
  and source cross-reference checks. The base DTB remains skipped without the
  external RK3576 include. `git diff --check` passes. Independent read-only review
  found no blocking correctness/security issues in the production changes.

The source inventory covers main firmware, the Linux bridge, libapex/native
Python extension, tools, devicetree and the GhostWisp subdevice; this run does not
claim an exhaustive manual audit of every source. `make check` fails because the
Arm/AArch64 cross-compilers, CMake, Pico SDK and configured kernel build tree are
unavailable. Target firmware/kernel builds and physical hardware checks remain
unverified. New power-management, DMA, watchdog and sleep/wake features were not
invented without hardware evidence. The full original audit/warning-cleanup
scope is therefore incomplete despite the verified bounded parser improvements.
The unrelated edits in `/root/ghostblade` were preserved; publication uses a
separate clean checkout on main.

## Complete inline link destinations (2026-10-25)

Author: jayis1

The inline path regex stopped at the first closing parenthesis, so a valid
`part(1).md` was checked as `part(1`. This rejected valid filenames and could
hide a missing full destination when the truncated prefix existed. A bounded
character scan now tracks balanced parentheses and skips Markdown punctuation
escapes. Angle destinations and reference definitions retain their existing
coverage; punctuation escapes are decoded before percent-encoded URL paths.
Malformed/unclosed inline destinations are ignored, not certified as valid
Markdown. See [path diagnostics](build-instructions.md#documentation-path-diagnostics)
for the supported subset and exclusions.

Verification:

- New tests reproduced 11 failing assertions against the original checker.
- `python3 tests/test_doc_links.py -v` passes 34 tests via both real Python
  entry points, plus a production Make target failure-propagation fixture.
  Coverage includes nested/escaped parentheses, encoded paths, titles and
  fragments, complete-path diagnostics, existing-prefix false success, and
  unclosed destinations. Fixtures contain no real hardware evidence.
- `make -C tests run` passes the complete host suite, including these tests.
- `make docs-check validate-dts validate-netlist` passes available checks;
  netlist validation retains 14 existing exact-name warnings (20/34 coverage).
- `make validate-netlist-strict` still exits nonzero for those existing names.
- `make -C software/dts all validate` passes checks for eight overlay outputs;
  the base remains skipped without the external `rk3576.dtsi`.

Existing setup, contribution, build, flashing, FAQ and diagram guides were
retained rather than duplicated. No electrical assignments, CAD libraries,
power timing, or flashing procedures were invented. Editable schematic/PCB
sources and `kicad-cli` remain unavailable; ERC/DRC, target builds and physical
board operation are not verified by this run. The
[hardware readiness review](hardware-export-readiness.md) remains applicable.
No workflow files were added or changed. Unrelated local changes in the
separate `/root/ghostblade` checkout were left untouched.

## Reject successful export commands without artifacts (2026-10-25)

Author: jayis1

The fabrication exporter previously treated zero KiCad process exits as export
success, even when no files were written. It could consequently package only
draft notes. Each layer, drill and placement command now needs new, nonempty
regular output of its expected filename/extension before the next stage runs.
Directories, symlinks, empty files and unrelated filenames do not qualify;
previous stages' files cannot satisfy a later command. Failure preserves partial
outputs for diagnosis and prevents this invocation from creating notes or a ZIP.
The [readiness guide](hardware-export-readiness.md#safe-local-export-commands)
explains the file-presence contract and its limits.

Verification:

- New regressions reproduced 76 failing assertions before the production fix.
  `python3 tests/test_generate_gerbers.py -v` now passes all 12 test methods,
  including missing/empty/directory/symlink/unrelated output at every stage,
  command errors/timeouts/missing executables, and successful synthetic packaging.
- `make -C tests run` passes the complete host suite, including exporter tests.
- `make docs-check validate-dts validate-netlist` passes its available checks;
  default netlist validation still reports 14 existing exact-name warnings.
- `make validate-netlist-strict` still fails on those same 14 names. No electrical
  net was renamed to hide a mismatch.
- `make -C software/dts all validate` passes for eight existing overlay outputs;
  the base board is skipped because the external `rk3576.dtsi` is unavailable.
- Independent read-only review found no blocking correctness/security defects;
  `git diff --check` passes. No workflow files or hardware definitions changed.

These are file-presence/control-flow checks with explicitly synthetic export
fixtures, not real CAD exports or validation of layer identity, geometry, drill
completeness, ERC/DRC or electrical safety. `kicad-cli`, an editable schematic,
and a routed board are unavailable. Existing getting-started, contributor,
build, flashing, diagram and troubleshooting guides were retained rather than
duplicated. Package pins, footprints/models, power sequencing and new devicetree
assignments still require the revision-matched evidence listed in the readiness
guide; no speculative hardware additions or target flashing were performed.
The unrelated edits in `/root/ghostblade` remain untouched; this run uses
`/root/ghostblade-maintenance` on main.

## Preserve GPIO provider probe errors

Author: jayis1

The bridge GPIO initialization helper converted all negative device-tree GPIO
lookup results into `-EINVAL`. In particular, an unavailable GPIO provider's
`-EPROBE_DEFER` was lost, preventing normal deferred probing. Each mandatory
GPIO lookup now returns its negative error immediately, before looking up the
next GPIO or requesting any pins. Positive invalid GPIO numbers still fail
validation, GPIO zero remains valid, and existing managed request failures and
reset-state ordering are unchanged. No pin assignment or electrical behavior
was changed.

`python3 tests/test_kernel_gpio_production.py -v` failed against the original
helper and passes after the fix. It extracts the production helper, compiles it
with `-Wall -Wextra -Werror -fsanitize=undefined,bounds`, and injects lookup and
request failures at every position. It checks deferred probe, missing-property,
invalid-argument and I/O errors, successful fresh-probe retry, GPIO zero,
positive invalid numbers, request ordering, and reset-state updates. This test
now runs under `make -C tests run`; the full host suite passes before and after
the change. Host stubs do not test devres cleanup, kernel integration, provider
registration, concurrent removal, or physical GPIO operation.

Analysis and verification:

- Linux checkpatch reports zero errors and warnings on the changed driver patch.
  Full-file results remain 772 errors and 1528 warnings (baseline: 772 errors and
  1531 warnings). The requested repository-wide warning cleanup is not complete.
- Cppcheck 2.17.1 passes all 15 main firmware C sources with
  `--enable=warning,performance,portability --check-level=exhaustive
  --platform=unix32 --std=c11 --error-exitcode=1 '-Dbi_decl(x)='
  -Ifirmware/rp2350b/include firmware/rp2350b/src`. The annotation definition is
  for analysis only; missing SDK context and the Unix-width approximation do not
  establish a target build. Enabling `style` additionally reports existing
  constant-return conditions in `adc_calibration.c` and `main.c` and unused
  timestamp members in `main.c`; those findings were not silently suppressed.
- The same warning/performance/portability analysis passes the GhostWisp
  firmware with its own include directory and without the annotation override.
- Source-pattern scans found no literal credential assignments or Python
  `os.system`, `shell=True`, `pickle.load(s)`, or `yaml.load` calls. Pattern scans
  are not a complete security audit or a secret-history investigation.
- `make validate-dts` and `make -C software/dts all validate` pass their available
  checks; the base board tree is skipped because `rk3576.dtsi` is unavailable.
- `make check` fails because the AArch64 and Arm bare-metal cross-compilers,
  CMake, Pico SDK, and target kernel build tree are unavailable. A direct driver
  build also fails for the missing kernel build tree. Firmware/kernel builds,
  board behavior and hardware-dependent feature changes remain unverified.

This is a focused, regression-backed hardening change, not an exhaustive manual
review of every source file or completion of the task's feature wishlist. Broad
DMA, PM, watchdog and sleep/wake changes were not added without target evidence.
The original `/root/ghostblade` checkout had unrelated modifications and was
left intact; work used the clean `/root/ghostblade-maintenance` checkout on main.

## Bridge GPIO assignment checks

Author: jayis1

The DTS checker previously compared a set of handshake pin numbers. Swapping
INT_REQ and HOST_RDY therefore passed, as did conflicting declarations hidden by
correct copies and assignments found only in comments. It now checks each
occurrence of the three existing property names against its documented
controller, pin, and `GPIO_ACTIVE_LOW` flag. Repeated consistent declarations
remain allowed because the base source currently contains two sets. No DTS,
firmware pin, netlist, footprint, model, or electrical rule was changed.

`python3 tests/test_dts_validation.py -v` reproduced 15 failing subcases before
the fix; all nine test methods pass afterward. Coverage includes swapped pins,
each missing signal, wrong controllers/polarity, malformed values, conflicting
copies in either order, comment-only input, empty input, and whitespace. The
production CLI and `make validate-dts` reject swapped copies of the real source.
These tests now run in `make -C tests run`, which passed in full.

This remains a selected text check, not a DTS parser: it expects the current
symbolic decimal-pin form and does not resolve numeric aliases, expressions,
node ownership, bindings, overlay application, or schematic connectivity. Its
other checks retain their prior limitations; printing overlay target references
does not validate that the referenced symbols exist in the target kernel.

Verification also passed `make docs-check validate-dts validate-netlist` and
`make -C software/dts all validate`. Netlist validation retains 14 exact-name
warnings; `make validate-netlist-strict` still fails on those known discrepancies.
All eight overlays compile/syntax-check, but the base is skipped without
`rk3576.dtsi`. `make check` fails for missing cross-compilers, CMake, Pico SDK,
and target kernel build tree. No target build, physical flashing, ERC/DRC,
reproducibility comparison, or hardware operation is claimed. The README driver
example now builds only and directs deployment to the guarded flashing guide.

Existing getting-started, contributor, build, flashing, FAQ, diagram, toolchain,
library-manifest, optional-overlay, and draft-rule assets were inventoried;
duplicate guides or guessed electrical artifacts were not added. The
[hardware readiness review](hardware-export-readiness.md) remains the release-gap
reference for missing editable schematics/PCB, incomplete footprints/models, and
unverified rule integration.

## Runtime PM SPI configuration failures

Author: jayis1

The driver's runtime suspend/resume callbacks previously returned success even
when `spi_setup()` failed. Suspend now returns the controller error before
masking the IRQ. Resume returns the error, restores the preceding software mode
and speed, and leaves the IRQ masked. Successful transitions retain their IRQ
ordering. This is software rollback, not proof that a controller undoes partial
hardware configuration on failure.

`tests/test_kernel_pm_production.py` extracts both production callbacks, compiles
them with `-Wall -Wextra -Werror` and undefined-behavior/bounds sanitizers, and
injects SPI setup failures. The regression failed before the fix and passes
afterward. It checks submitted mode/speed, rollback, IRQ ordering, direct callback
retry, and success/error paths without an IRQ. It runs in `make -C tests run`;
the complete host suite passes. Stubs do not establish PM-core retry behavior,
concurrency safety, a complete module build, or hardware operation.

Static analysis: cppcheck 2.17.1 passes all 15 main firmware C files using
`--enable=warning,performance,portability --check-level=exhaustive
--platform=unix32 --std=c11 --error-exitcode=1 '-Dbi_decl(x)='
-Ifirmware/rp2350b/include firmware/rp2350b/src`. The SDK's binary-information
annotation macro is omitted for analysis only. This width approximation and
absent SDK headers are not a target build or exhaustive SDK configuration check.
Linux v6.12 checkpatch reports zero errors/warnings on the driver patch; the full
file still reports 772 errors and 1531 warnings (baseline: 777 and 1556). The
requested all-warning cleanup is not complete; unrelated style churn was avoided.
Source pattern scans found no literal credential assignments or matches for
`os.system`, `shell=True`, `pickle.load(s)`, or `yaml.load` in Python sources;
these scans are not a full security audit or secret-history investigation.

`make libapex`, `make docs-check`, and `make -C software/dts all validate` pass;
the base DTB remains skipped without `rk3576.dtsi`. `make check`, `make firmware`,
and `make driver` fail because the environment lacks cross-compilers, CMake,
Pico SDK, and the target kernel build tree. No module was loaded, firmware
flashed, or radio operated. The broad source audit and requested optional
hardware-dependent features remain incomplete. In particular, the existing SG
engine's asynchronous-transfer timeout lifetime, mmap allocation boundaries,
and removal/open-file lifetime need separate remediation and kernel testing;
this callback fix does not make the whole driver production-ready.

## Required and unique netlist component records

Author: jayis1

The checker previously returned success with no parsed components if its net
names and selected assignment strings passed, and silently replaced duplicate
component references with the last parsed value. Missing expected U1-U9 records
and duplicate parsed references now fail both modes. No component identity,
footprint, power timing, rule file, or DTS assignment was changed.

Four new regression methods reproduced 32 failing subcases before the fix.
The expanded 17-test suite now passes, including every required IC removal,
net-only input, duplicate ordering/value cases, and both Make targets. Fixtures
are synthetic text, not CAD exports. Existing name tests now include all nine
expected component records to keep name-coverage checks independent.

Verification: `python3 tests/test_netlist_validation.py -v`, the full
`make -C tests run` host suite, `make docs-check validate-dts validate-netlist`,
and `make -C software/dts all validate` passed. Normal netlist validation retains
14 existing exact-name warnings (20/34 coverage); strict validation correctly
fails on those names. The base DTB remains skipped without `rk3576.dtsi`.
`make check` fails for missing cross-compilers, CMake, Pico SDK, and target kernel
build tree. No hardware, flashing, full target build, or ERC/DRC result is claimed.
See [required component records](hardware-export-readiness.md#required-component-records)
for troubleshooting and the unchanged parser limitations.

## Required netlist assignment-check inputs

Author: jayis1

The netlist validator previously skipped DTS or firmware assignment checks when
their source files were absent, while still returning success if the remaining
checks passed. Both normal and strict modes now report a missing base DTS or
`board_pins.h` as an error, including the exact missing path. No electrical
assignment, symbol, footprint, or hardware rule was changed.

The expanded 13-test regression suite uses minimal synthetic assignment inputs
and exercises the production command from outside its repository directory.
Before the fix, eight missing-input subcases failed because the validator and
both Make targets returned success. After the fix all tests pass, including
empty assignment inputs and the pre-existing name-coverage regressions. The
suite is already part of `make -C tests run`, which also passed.

Local verification: `python3 tests/test_netlist_validation.py -v`,
`make docs-check validate-dts validate-netlist`, and
`make -C software/dts all validate` passed. Exact-name coverage remains 20/34
with 14 existing warnings; the strict command still correctly rejects these
unresolved names. Eight overlays compile and syntax-check; the base DTB remains
skipped because `rk3576.dtsi` is not bundled. No target cross-build, physical
flashing, KiCad ERC/DRC, or hardware validation is established by these checks.
See [hardware export readiness](hardware-export-readiness.md) for the required
inputs, troubleshooting guidance, and remaining engineering gaps.

## Incremental libapex build dependencies

Author: jayis1

The C-library Makefile now records public and transitive header dependencies
using compiler-generated `.d` files, preserves `all` as the default goal, and
removes incomplete recipe targets on errors. The top-level `libapex` target no
longer invokes independent `clean all` goals together: that discarded incremental
outputs and could race under parallel Make. Help now correctly describes this
as a C-library build, not a Python-extension build.

Before the fix, the new six-test suite had four failures and one error: header
changes were ignored, a deliberate header compilation error was not noticed,
dependency files were missing, and the parallel top-level build removed an
expected artifact. All six tests now pass using the production Makefiles and C
source in temporary directories. They are included in `make -C tests run`, which
also passed. See [build instructions](build-instructions.md#libapex) for clean
builds when compiler flags, architecture, or pkg-config prefix changes; these
inputs are not tracked automatically. No reproducibility claim is made.

`make docs-check validate-dts validate-netlist` passed with the existing 14 exact
net-name warnings (20/34 coverage). Eight overlays passed syntax validation via
`make -C software/dts all validate`; the base remained skipped because the
external `rk3576.dtsi` is absent. No schematic, footprint, DTS assignment, power
timing, flashing procedure, or electrical rule was changed. Cross-builds,
electrical validation, and hardware operation remain outside this host-only
verification.

## Fenced-code examples in documentation checks

Author: jayis1

The link-path checker now excludes top-level backtick and tilde fenced code
blocks before extracting inline links and reference definitions. Example paths
inside those blocks are not rendered links and no longer cause false failures.
Eight new regression tests exercise both command entry points, covering marker
kinds and lengths, up to three leading spaces, longer closing markers, invalid
closers, unclosed blocks, invalid backtick info strings, and real missing links
after a block. The expanded 24-test suite passes; the new fixtures reproduced
failures before the filter was added.

The filter is deliberately limited: it is not a Markdown renderer and does not
parse container fences inside blockquotes/lists, inline code spans, or general
indented code blocks. Heading anchors and undefined reference labels remain
outside the path check's scope. External URLs are not fetched.

The full `make -C tests run` host suite also passes. Local commands:
`python3 tests/test_doc_links.py -v`, `make docs-check`,
`make validate-dts validate-netlist`, and `make -C software/dts all validate`.
The selected DTS checks pass; exact net-name coverage remains 20/34 with the
same 14 warnings. All eight overlays compile and syntax-check; the base remains
skipped without `rk3576.dtsi`. Cross-build prerequisite checks fail because the
cross-compilers, CMake, Pico SDK, and target kernel build tree are absent.
`kicad-cli` is unavailable, and editable schematic/PCB sources are not tracked;
no electrical assignments, power timing, flashing procedures, or design-rule
settings were changed. Build hygiene also now ignores DTS `.pp` intermediates
and the GhostWisp I2C test executable, and repairs a concatenated `modules.order`
ignore entry discovered after running the checks.

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
not validated. Four-space-indented definitions are ignored. Top-level fenced-code
examples are now filtered as described above. Same-line definitions containing
parentheses in filenames are supported, but nested-parenthesis inline destinations
remain outside its scope.

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
