# GhostWisp PCB Layout Review

**Author: jayis1**  
**Review scope:** signal integrity, power delivery, manufacturability, and comparison with the frozen GhostBlade
reference  
**Disposition:** **NO-GO — no PCB layout exists to release or manufacture**

## Executive result

The repository contains no GhostWisp schematic (`.kicad_sch`), PCB layout (`.kicad_pcb`), netlist, BOM,
stackup, fabrication drawing, placement drawing, Gerbers, drill files, or DRC report. A geometric layout review
therefore cannot be performed.

The current GhostWisp documentation is still intentionally pre-schematic. Its roadmap leaves the power budget,
RP2350B pin allocation, regional RF front ends, NFC/IR interfaces, exact parts, antenna assumptions, and USB
power path open. Those decisions are prerequisites to routing. Firmware pin definitions must not be treated as
an approved electrical design until they are cross-checked against a schematic and RP2350B electrical
constraints.

This review is a release-gate review, not an approval. It records the evidence gap and the checks that the first
layout must pass.

## Evidence inspected

### GhostWisp

- `devices/ghostwisp/README.md`
- `devices/ghostwisp/docs/architecture.md`
- `devices/ghostwisp/docs/ghostblade-integration.md`
- `devices/ghostwisp/docs/roadmap.md`
- `devices/ghostwisp/firmware/rp2350b/include/ghostwisp_pins.h`

### Frozen GhostBlade reference

- `hardware/kicad/ghostblade.net`
- `hardware/kicad/ghostblade.kicad_pro`
- `hardware/drc/ghostblade-drc-rules.kicad_drc`
- `docs/phase3-pcb/pcb-blueprints-and-layout.md`
- `docs/pin-assignments.md`
- `docs/power-tree.md`

Neither device has a committed `.kicad_pcb` file in the reviewed tree. GhostBlade can therefore provide
documented principles and interface precedent, but not a board-to-board geometry comparison or reproducible
DRC baseline.

## Blocking findings

| ID | Area | Severity | Finding | Required closure evidence |
| --- | --- | --- | --- | --- |
| GW-LYT-001 | Reviewability | Blocker | No GhostWisp PCB, schematic, or netlist exists. | KiCad sources, clean ERC/DRC, and a routed-net report. |
| GW-LYT-002 | Architecture | Blocker | Routing prerequisites remain open. | Approved architecture tables linked to named nets. |
| GW-LYT-003 | Power | Blocker | Parts, power paths, and rail requirements are unspecified. | Rail budget, sequencing, margins, and copper calculations. |
| GW-LYT-004 | RF/NFC | Blocker | RF geometry, matching, keepouts, and tuning plans are absent. | Stackup, impedance targets, matching, keepouts, and tuning plan. |
| GW-LYT-005 | Manufacturing | Blocker | Outline, footprints, fab limits, and test access are absent. | Drawings, vetted footprints, DFM, BOM, and test-point plan. |
| GW-LYT-006 | Reference | Major | GhostBlade has no committed board file; its rules are product-specific. | Derive namespaced GhostWisp rules without changing GhostBlade. |

## Signal-integrity gate for the first layout

The following checks are mandatory before a layout can pass review.

### USB-C

- Define whether Rev A supports USB device only, dual-role data, USB host power sourcing, or a constrained
  combination. Do not route until the connector, CC/power-role circuit, VBUS protection, and current limits
  agree.
- Route USB D+/D− as a fabricator-calculated 90 Ω differential pair over one continuous reference plane.
- Keep the pair short, coupled, and symmetric, with equal via count and no stubs or plane-split crossings.
- Place low-capacitance ESD protection at the connector with a short, low-inductance return to ground. Place
  common-mode filtering only if simulations or emissions testing justify it.
- Separate VBUS power switching from data routing and prove that battery, charger, GhostBlade, and any host-mode
  source cannot back-power one another.

### QSPI, display, microSD, and peripheral buses

- Freeze the RP2350B pin map and bus topology before placement. Cross-check schematic nets, firmware symbols,
  and electrical pin capabilities automatically.
- Give QSPI flash the shortest clock path and local return plane. Avoid sharing its boot-critical nets.
- Decide whether the display, microSD, CC1101, and ST25R3916 share SPI controllers or only physical routing
  regions. Document maximum clock, loading, chip-select idle behavior, and series-termination footprints per
  clock/source.
- Avoid long star branches on shared clocks. If a shared bus is retained, place the controller near the branch
  point and validate edge rate and loading rather than only nominal clock frequency.
- Keep I²C pull-ups and bus capacitance within the selected speed limit. Provide isolation or level translation
  where powered-off peripherals could clamp the bus.

### Sub-GHz RF

- Select the regional front-end variant before routing. The matching network, filtering, antenna connector, and
  regulatory assumptions are one controlled variant set.
- Use a stackup-specific 50 Ω trace calculation confirmed by the chosen fabricator. GhostBlade's documented
  0.142 mm trace is not portable to another stackup.
- Place the CC1101 balun/matching/filter chain in the vendor-recommended order with minimum interconnect length
  and no test-point stubs on the RF path.
- Maintain an unbroken RF reference plane, dense ground stitching at layer transitions and RF boundaries, an
  antenna/feed keepout, and physical separation from display clocks, switch-mode inductors, USB, and NFC drive
  currents.
- Include a conducted-RF test option and tuning footprints that can be populated without cutting production
  traces.

### NFC and IR

- Freeze whether the NFC loop is PCB, flex, or enclosure-mounted. Define loop inductance/Q target,
  ferrite/shielding, battery/display metal separation, connector parasitics, and accessible matching components.
- Keep NFC antenna current loops away from the CC1101 reference plane and sensitive analog/power feedback paths.
  Plan tuning with the final enclosure, battery, and display installed.
- Size the IR LED driver from pulse current, duty cycle, thermal limit, and battery sag. Route its pulsed-current
  loop directly to local bulk capacitance so it does not modulate MCU/RF supplies.

## Power-delivery gate for the first layout

1. Produce a state-by-state budget for off, deep sleep, standby, active receive, display-active, NFC field-on,
   IR transmit, sub-GHz transmit, USB attached, USB sourcing, and battery charging. Include simultaneous
   worst-case combinations allowed by firmware.
2. Select the charger/power-path IC and document behavior for dead battery, USB attach/detach, hard-switch off,
   GhostBlade connection, thermal regulation, and reverse-current prevention.
3. Define every rail, source, peak and steady current, allowed ripple/droop, startup order, discharge behavior,
   and controllable load switch.
4. Size power traces and pours using temperature-rise and voltage-drop calculations. Provide uninterrupted return
   paths. Do not copy GhostBlade's mixed plane/star-ground wording without a current-return analysis.
5. Place high-frequency decoupling at each supply pin with the shortest possible pin-cap-ground loop, then add
   local bulk capacitance per load transient. The schematic and layout must show capacitor value, package,
   voltage rating, and effective-bias margin.
6. Keep switch nodes, inductors, charger current loops, and motor/buzzer/IR pulse loops away from RF, NFC, clocks,
   ADC, and antenna regions.
7. Provide test points for battery, VBUS, each regulated/switched rail, reset/boot, SWD, ground, and critical
   bus/RF control signals. Test points must not create RF or high-speed stubs.

## Manufacturability gate for the first layout

- Select a fabricator and assembly capability before setting trace/space, drill, annular ring, solder-mask web,
  via type, impedance tolerance, copper weight, and finish.
- Prefer a cost-appropriate conventional stackup. Do not inherit GhostBlade's six-layer microvia/via-in-pad
  strategy unless GhostWisp routing and RF/EMC analysis require it.
- Vet every footprint against the manufacturer land pattern and an assembly-house rule set. Record package,
  courtyard, polarity/pin-1, exposed-pad paste segmentation, and hand-rework constraints.
- Keep components, copper, vias, antennas, buttons, display, USB-C, microSD, and battery connector clear of
  enclosure walls, fasteners, board edges, flex bends, and user-access zones.
- Add fiducials, tooling/panel rails as required, readable reference designators, polarity markings, revision,
  serial/2D-code area, and `jayis1` attribution.
- Generate and archive ERC/DRC outputs, Gerbers, drill files, IPC-356/netlist comparison if supported, BOM,
  centroid file, stackup, impedance table, fabrication drawing, assembly drawings, and a deterministic release
  checksum manifest.
- Run DFM with the intended vendor and inspect solder-mask slivers, copper-to-edge clearance, acid traps, annular
  rings, tombstoning risk, via-in-pad, thermal-pad voiding, connector retention, and panel breakaway stress.

## GhostBlade comparison

### Preserve as frozen reference

- Do not modify GhostBlade hardware or reinterpret GhostBlade nets to make GhostWisp fit.
- Reuse the reference discipline: named net classes, continuous high-speed/RF return planes, connector-side ESD,
  controlled-impedance targets confirmed by fabrication, RF zoning/stitching, explicit power sequencing, test
  points, pin-map cross-reference, and machine-checkable ERC/DRC.
- Keep companion integration additive. GhostWisp must connect through the agreed external interface without
  requiring a GhostBlade board respin.

### Do not copy blindly

- GhostBlade's six-layer stack, 0.142 mm 50 Ω width, DDR/MIPI/PCIe constraints, BGA escape rules, microvias,
  thermal-via arrays, and IPC Class 3 assumptions are product-specific.
- GhostWisp has different cost, size, battery, antenna, NFC, and assembly constraints. Its layer count and rule
  values must come from its own placement study, PDN/RF needs, and fabricator stackup.
- Copy any useful GhostBlade custom rule into a GhostWisp-namespaced rule file and re-derive it. Do not edit the
  frozen file.

## Required re-review package

Submit all of the following together:

1. frozen requirements: enclosure/outline, cost, battery-life target, exact parts, regional variants, and allowed
   operating-state combinations;
2. power budget, power tree, USB role/power-path design, and thermal estimates;
3. RP2350B pin/bus allocation cross-checked against firmware;
4. schematic, BOM, approved symbols/footprints, and clean ERC output;
5. fabricator stackup and impedance calculations;
6. PCB source with board outline, placement, keepouts, net classes, length/impedance constraints, zones, and
   complete routing;
7. clean DRC plus an unrouted-net count of zero, with every waiver documented;
8. RF/NFC tuning plan, DFM report, manufacturing outputs, and bring-up/test-point plan.

A follow-up review can then issue findings against actual geometry. Until that package exists, GhostWisp Rev A
is not ready for PCB fabrication.
