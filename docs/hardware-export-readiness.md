<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# Hardware export readiness

Author: jayis1

Status: design assets, not a fabrication release. No editable `.kicad_sch`,
routed `.kicad_pcb`, or STEP model is tracked in the current repository.
Do not derive pad mappings, electrical limits, or validated power timing from
concept art or the names of library entries.

## Evidence and unresolved gaps

| Area | Repository evidence | Required before hardware release |
|------|---------------------|----------------------------------|
| Schematic and layout | Project settings and a text netlist exist, but no editable schematic or PCB is tracked | Supply the reviewed schematic and routed board for a named revision; regenerate the netlist from that schematic |
| Netlist syntax | The connector section of `hardware/kicad/ghostblade.net` uses literal backslash-escaped quotes, unlike the earlier component/net sections | Re-export through KiCad; do not silently normalize an electrical source or assume the existing regex validator parses the entire file |
| Signal names | Manifest `NET_HOST_READY` differs from netlist `NET_HOST_RDY`; manifest SDR/CC SPI MOSI/MISO/CS names differ from netlist TX/RX/CSN names | Reconcile endpoints, direction, series components, and revision before selecting names; spelling similarity is not proof of connectivity |
| Footprints | The `.kicad_mod` file wraps multiple footprints in `kicad_footprint_lib`; the RK3576 entry has three pads and a comment deferring the remaining pads | Produce individually loadable, complete footprints; verify every pad and package against manufacturer drawings in KiCad |
| Library identity | U9 footprint is `FBGA-200_12x12mm` in the library manifest but `FBGA-200_0.4mm_12x12mm` in the netlist | Resolve identifiers against a verified footprint, not by guessing which name is correct |
| 3D models | Footprints reference `${KIPRJMOD}/3dmodels/` paths; the directory contains guidance rather than tracked model files | Obtain compatible, licensed models and validate orientation/offset; a README entry is not an existing STEP file |
| Rule files | Draft ERC/DRC text exists under `hardware/drc/` | Validate supported syntax, project integration, stackup, and manufacturer limits; no rule execution or IPC compliance is established |
| Device tree | Base source and optional overlays exist; the SoC `rk3576.dtsi` is not bundled | Select the target kernel/revision, compile the base, apply supported overlay combinations, and validate bindings and actual wiring |

`tools/validate_netlist.py` performs selected name/reference checks with regular
expressions. It does not parse a complete KiCad document, compare every manifest
net or endpoint, validate every footprint, or prove that a referenced model file
exists. Passing it is not ERC, DRC, or a manufacturing sign-off.

## Safe local export commands

The export command now fails if the board is missing or any KiCad layer, drill,
or placement command fails or times out. Export requires a new or empty output
directory so old files cannot be included by accident. A failed run may leave
partial exports; preserve them for diagnosis and retry into another empty
location. No notes or ZIP are created after a failed export by that invocation.

When a reviewed board becomes available, from the repository root:

```sh
python3 tools/generate_gerbers.py \
  --kicad-pcb /path/to/reviewed/ghostblade.kicad_pcb \
  --output /path/to/new-empty-export-directory --fab-note --zip
```

This is an invocation example, not a tested board export. Check the resulting
layers, drill data, placement data, and archive independently before release.
A successful process exit does not certify electrical correctness or completeness
of manufacturer deliverables. KiCad is required for actual exports.

For draft notes only, without a PCB or KiCad:

```sh
python3 tools/generate_gerbers.py --notes-only --output /path/to/design-notes
```

`--notes-only` cannot be combined with `--zip`. `--fab-note` now means adding
notes after a successful board export; it no longer permits a missing board.
The JSON contains existing design assumptions and is not an approved fabrication
specification. Notes-only mode can overwrite the notes JSON in its output folder.

## Repeatable checks

```sh
python3 tests/test_generate_gerbers.py -v
make docs-check validate-dts validate-netlist
make -C software/dts all validate
make -C tests run
```

The exporter regression tests exercise production Python control flow with
mocked KiCad subprocess results and temporary path/archive fixtures. They cover
missing inputs, occupied output directories, failure/timeout/missing-executable
handling at every export stage, notes-only behavior, and duplicate-free ZIP
entries. They do not parse or export a real PCB. All eight optional overlays can
compile locally without the missing base include; this does not prove base-tree
symbol resolution or overlay compatibility on hardware.

## Release review order

```text
Reviewed revision and datasheets
             |
             v
Editable schematic -> regenerated netlist -> firmware/DTS interface review
             |
             v
Verified symbols + complete footprints + resolved model paths
             |
             v
Routed board + approved stackup -> actual ERC/DRC -> export
             |
             v
Independent fabrication-data review -> release decision
```

This is a review dependency diagram, not an electrical power-sequencing chart.
No pin assignment, rail delay, or flashing offset is inferred by this guide.

See [build instructions](build-instructions.md),
[rule-file status](../hardware/drc/README.md), and
[KiCad library manifest](../hardware/kicad/library-manifest.md).
