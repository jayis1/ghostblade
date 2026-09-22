# GhostWisp Roadmap

**Author: jayis1**

Work proceeds in evidence-producing increments. A phase is complete only when its documents, interfaces, tests, and implementation agree.

## Phase 0 — Product contract

- [x] Establish GhostWisp as GhostBlade's simpler sister device.
- [x] Keep it in `devices/ghostwisp/` on the default branch.
- [x] Define capabilities, omissions, safety boundaries, and companion role.
- [ ] Resolve the Rev A cost target, enclosure target, and battery-life target.
- [ ] Select exact display, controls, charger, fuel gauge, and storage parts.

## Phase 1 — Architecture

- [ ] Create the power budget and operating-state table.
- [ ] Allocate RP2350B pins and buses without conflicts.
- [ ] Define regional CC1101 front-end variants.
- [ ] Define NFC and IR electrical interfaces.
- [ ] Freeze the GhostBlade companion transport and frame requirements.
- [ ] Record a threat model for USB, profiles, firmware update, and active RF actions.

## Phase 2 — Verifiable design

- [ ] Add a machine-readable BOM with manufacturer part numbers and lifecycle notes.
- [ ] Add schematic source, symbols, footprints, ERC rules, and test points.
- [ ] Add a netlist validator that cross-checks schematic, pin map, and firmware definitions.
- [ ] Document antenna, grounding, ESD, battery, and USB power-path assumptions.
- [ ] Produce a manufacturing and bring-up checklist.

## Phase 3 — Firmware foundation

- [ ] Add a buildable RP2350B project and reproducible toolchain instructions.
- [ ] Implement board initialization, watchdog, power states, and recovery.
- [ ] Implement display/input shell and action broker.
- [ ] Add bounded storage/profile parsers with host-side tests.
- [ ] Add A/B signed-update metadata and rollback behavior.

## Phase 4 — Capabilities

- [ ] CC1101 receive and packet inspection before any transmit path.
- [ ] NFC tag inspection before emulation.
- [ ] IR learning before replay.
- [ ] UART console before active bus probing.
- [ ] USB serial before HID test profiles.
- [ ] Companion status exchange before capture transfer or remote execution.

## Phase 5 — Integration and usability

- [ ] Verify standalone operation with no GhostBlade attached.
- [ ] Verify companion mode with version mismatch and disconnect recovery.
- [ ] Confirm visible arming and physical confirmation for active operations.
- [ ] Measure boot time, sleep current, active current, and battery life.
- [ ] Validate captures and profiles against malformed-input test vectors.
- [ ] Create a concise build, flash, recover, and first-use guide.

## Per-run rule

Scheduled work should choose one incomplete item, inspect the current repository first, make a coherent bounded change, run the relevant local checks, and update this roadmap only with verified progress. It must not continue expanding the completed GhostBlade platform unless a shared interface change is required for GhostWisp.
