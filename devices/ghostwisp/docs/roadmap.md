# GhostWisp Roadmap

**Author: jayis1**

Work proceeds in evidence-producing increments. A phase is complete only when its documents, interfaces, tests, and implementation agree.

## Phase 0 — Product contract

- [x] Establish GhostWisp as GhostBlade's simpler sister device.
- [x] Keep it in `devices/ghostwisp/` on the default branch.
- [x] Define capabilities, omissions, safety boundaries, and companion role.
- [x] Establish GhostBlade and GhostWisp as one highly interconnected system with reduced detached operation.
- [x] Freeze existing GhostBlade hardware and behavior; permit only additive, backward-compatible companion features.
- [ ] Resolve the Rev A cost target, enclosure target, and battery-life target.
- [ ] Select exact display, controls, charger, fuel gauge, and storage parts.

## Phase 1 — Architecture

- [ ] Create the power budget and operating-state table.
- [ ] Allocate RP2350B pins and buses without conflicts.
- [x] Define regional CC1101 front-end variants. (RF engineering: [rf-engineering.md](rf-engineering.md))
- [ ] Define NFC and IR electrical interfaces.
- [x] Freeze the GhostBlade companion transport and frame requirements on both devices. (Protocol codec: [protocol/frame.py](../protocol/frame.py), C header: [firmware/rp2350b/include/ghostwisp_protocol.h](../firmware/rp2350b/include/ghostwisp_protocol.h))
- [x] Define the GhostBlade companion daemon, `libapex`/Python surface, and shared capture/profile schemas. (Schema: [protocol/schema.py](../protocol/schema.py), Golden vectors: [protocol/golden_vectors.py](../protocol/golden_vectors.py), Tests: [tests/test_protocol.py](../tests/test_protocol.py))
- [ ] Record a threat model for USB, profiles, firmware update, and active RF actions.

## Phase 2 — Verifiable design

- [ ] Add a machine-readable BOM with manufacturer part numbers and lifecycle notes.
- [ ] Add schematic source, symbols, footprints, ERC rules, and test points.
- [ ] Add a netlist validator that cross-checks schematic, pin map, and firmware definitions.
- [ ] Document antenna, grounding, ESD, battery, and USB power-path assumptions.
- [ ] Produce a manufacturing and bring-up checklist.

## Phase 3 — Firmware foundation

- [x] Add a buildable RP2350B project and reproducible toolchain instructions.
- [x] Implement board initialization, watchdog, power states, and recovery.
- [x] Implement peripheral driver stack: I2C, SPI, UART, GPIO with host-runnable unit tests. (API reference: [peripheral-drivers.md](peripheral-drivers.md))
- [ ] Implement display/input shell and action broker.
- [x] Add bounded storage/profile parsers with host-side tests. (Profile schema/validator: [protocol/profile.py](../protocol/profile.py), Tests: [tests/test_profile.py](../tests/test_profile.py), Golden vectors: [protocol/golden_vectors.py](../protocol/golden_vectors.py))
- [ ] Add A/B signed-update metadata and rollback behavior.

## Phase 4 — Capabilities

- [ ] CC1101 receive and packet inspection before any transmit path.
- [ ] NFC tag inspection before emulation.
- [ ] IR learning before replay.
- [ ] UART console before active bus probing.
- [ ] USB serial before HID test profiles.
- [ ] Companion pairing/status exchange before synchronized policy, capture transfer, or dual-consent execution.

## Phase 5 — Integration and usability

- [ ] Verify standalone operation with no GhostBlade attached.
- [ ] Verify companion mode with version mismatch and disconnect recovery.
- [ ] Verify time, region policy, profile index, capture index, and audit-record synchronization.
- [ ] Verify GhostBlade analysis of a streamed GhostWisp capture and a returned bounded action plan.
- [ ] Verify dual consent: GhostBlade prepares the action and GhostWisp requires physical confirmation.
- [ ] Confirm visible arming and physical confirmation for active operations.
- [ ] Measure boot time, sleep current, active current, and battery life.
- [ ] Validate captures and profiles against malformed-input test vectors.
- [ ] Create a concise build, flash, recover, and first-use guide.

## Isolation-check evidence

The frozen-baseline boundary is also checked by `make -C devices/ghostwisp check`.
The binary isolation gate rejects missing or failed `nm`, `strings`, and `grep`
inspection, including partial output followed by failure and empty symbol tables.
It preserves the existing test-symbol exemption and forbidden-symbol rules.
Host regression tests in `tests/test_check_isolation.py` exercise the real helper
with controlled tool fixtures, including large symbol output and filter errors.
These checks establish host-test binary isolation, not electrical correctness,
physical-board behavior, or a successful target firmware build.

### Header isolation gate

Author: jayis1

The same frozen-baseline requirement applies to the header inspection in
`check.sh headers`. It rejects missing directories, directories without headers,
missing `grep`, and inspection errors rather than reporting an empty or failed
inspection as a pass. All regular `.h` files matched recursively under the
include directory are checked, including `include/boards/ghostwisp.h`.
Indented directives and whitespace between `#` and `include` no longer bypass
the existing case-insensitive forbidden-header-name rules.

Run `python3 -m unittest devices.ghostwisp.tests.test_check_headers -v` from the
repository root, or `make -C devices/ghostwisp check`. The eight host regression
tests exercise clean headers, missing/empty inputs, missing tools, failed and
partial-output inspection, nested headers, paths with spaces, directive
whitespace, and preservation of case-insensitive matching and comment handling.
The tests demonstrated failures against the previous helper before the fix.

This remains a source-text sanity gate, not complete dependency isolation:
macro-expanded includes, line-spliced directives, comments inside directives,
and dependencies outside the scanned tree require compiler/dependency review.
It does not prove target compilation, electrical correctness, or hardware
behavior. No GhostBlade source, API, or default behavior is changed.

## Host boot build dependencies

Author: jayis1

Phase 3's reproducible-toolchain requirement also applies to incremental host
checks. The host boot build records compiler-discovered local dependencies in
`build/test_ghostwisp_boot.d`, including headers reached through other headers.
Editing a transitive header therefore rebuilds the boot test instead of running
a stale binary. The Makefile itself is a prerequisite so recipe changes also
rebuild existing binaries. The default goal remains `all`; `clean` removes the
binary and dependency file together. Use a clean build when changing compiler
selection, command-line flags, or toolchain versions; those are not tracked.

Run `python3 -m unittest devices.ghostwisp.tests.test_host_build -v` from the
repository root, or `make -C devices/ghostwisp check`. The three host regressions
copy the production Makefile and sources into isolated fixtures and invoke real
GCC: direct header rebuild, transitive header compile-error propagation and
repair, unchanged-input no-op behavior, default-goal preservation, and cleanup
followed by rebuilding. Before the fix, the transitive edit incorrectly returned
success without compiling, and no dependency file was produced. These tests do
not cross-compile target firmware or validate physical hardware. No frozen
GhostBlade source or interface is modified.

## Per-run rule

Scheduled work should choose one incomplete item, inspect the current repository first, make a coherent bounded change, run the relevant local checks, and update this roadmap only with verified progress. GhostBlade is a frozen baseline. Work may add namespaced, backward-compatible companion services, APIs, UI surfaces, schemas, documentation, and tests, but must not redesign or regress existing GhostBlade hardware or behavior. With GhostWisp absent, GhostBlade must continue to behave exactly as before.
