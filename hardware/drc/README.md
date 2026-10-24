<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# GhostBlade ERC / DRC rule status

Author: jayis1

Status: draft design intent, not validated or automatically applied KiCad rules.
There is no tracked editable schematic or routed PCB on which to run ERC/DRC.
The presence of these files does not establish electrical correctness,
manufacturability, controlled impedance, or IPC Class 3 compliance.

## Files and limitations

| File | Current purpose | Integration still required |
|------|-----------------|----------------------------|
| `ghostblade-drc-rules.kicad_drc` | Draft layout constraint expressions | Review supported syntax and constraints with the selected KiCad version, then configure the reviewed project-local `.kicad_dru` custom rules |
| `ghostblade-erc-rules.kicad_erc` | Draft electrical and physical review requirements | Translate supported electrical requirements into the schematic's ERC settings and pin types; physical spacing, trace length, and impedance require layout/stackup review rather than a schematic-only check |

Do not assume these filenames cause KiCad to load the rules. Do not blindly
rename or import them: successful parsing and actual enforcement must be
verified. No tested import procedure is provided here. Do not suppress warnings
or loosen limits to obtain a passing report.

## Required review

1. Identify the board revision, target KiCad version, and manufacturer-approved
   stackup and fabrication capabilities.
2. Supply editable schematic and routed PCB sources with complete, verified
   symbols and footprints.
3. Check each draft constraint's syntax, units, net-class selectors, applicability,
   and electrical/manufacturing justification. Fixed widths alone do not prove
   controlled impedance.
4. Configure supported electrical rules in the schematic and supported physical
   rules in the board project. Confirm that the intended nets match each rule.
5. Run actual ERC/DRC, inspect individual findings, and retain reports with the
   revision and tool version. A regex cross-reference check is not a substitute.
6. Independently review exported fabrication data before a release decision.

No electrical limits or package assignments were changed as part of this status
clarification. See [hardware export readiness](../../docs/hardware-export-readiness.md)
for concrete library/netlist gaps and export failure behavior.

## Related documentation

- [KiCad library manifest](../kicad/library-manifest.md)
- [Hardware contributor guide](../../docs/hardware-contributor-guide.md)
- [Power-tree design](../../docs/power-tree.md)
