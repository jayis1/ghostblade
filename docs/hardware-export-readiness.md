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
expressions. It now compares every parsed manifest `NET_` name exactly against
parsed netlist names. It does not parse a complete KiCad document, compare net
endpoints, validate every footprint, or prove that a referenced model file
exists. Passing it is not ERC, DRC, or a manufacturing sign-off.

## Exact net-name coverage

Basis: checked-in `GhostBlade.mf` and `hardware/kicad/ghostblade.net`, not an
export from a reviewed editable schematic or a hardware measurement.

```sh
make validate-netlist
make validate-netlist-strict
python3 tests/test_netlist_validation.py -v
```

The default command reports missing exact names as warnings and retains its
existing error checks. The strict command promotes those warnings to errors.
Both reject empty parsed manifest/netlist name sets, including unrecognized
input; neither is a full syntax validator. Additional netlist names are allowed
because power rails and other board signals need not appear in the manifest's
interface definitions. Names are case-sensitive; no alias or prefix matching is
used. The strict target runs all the same selected checks, not just name coverage.

Both modes also require `software/dts/ghostblade-rk3576.dts` and
`firmware/rp2350b/include/board_pins.h`. Missing either file is an error, not a
silently skipped assignment check. Empty files fail the existing required-property
or pin-definition checks. Restore the source from the same reviewed revision;
do not create an empty placeholder to get a passing result. These are text-only
checks against selected documented assignments, not schematic-derived endpoint
validation, DTS compilation, or a firmware build. Ancillary library/model
documentation remains warning-only when absent.

Current result: 20 of 34 parsed manifest names occur exactly in the 58 parsed
netlist names. The strict command exits nonzero for these 14 unmatched names:

| Group | Manifest names absent by exact spelling |
|-------|-----------------------------------------|
| Host handshake | `NET_HOST_READY` |
| CC1101 | `NET_CC_SPI_CS`, `NET_CC_SPI_MISO`, `NET_CC_SPI_MOSI` |
| NFC | `NET_NFC_I2C_SCL`, `NET_NFC_I2C_SDA`, `NET_NFC_SPI_CS` |
| SDR control | `NET_SDR_GPIO0`, `NET_SDR_GPIO1`, `NET_SDR_SPI_CS`, `NET_SDR_SPI_MISO`, `NET_SDR_SPI_MOSI` |
| SDR samples | `NET_SDR_IQ_I`, `NET_SDR_IQ_Q` |

An unmatched name is a review finding, not proof that the circuit is absent.
The connector escaping and partial regex extraction noted above still limit the
result. Reconcile reference/pin endpoints from a reviewed schematic before
renaming a net, declaring two names equivalent, or changing firmware/DTS wiring.
No electrical sources are changed by either command. Regression tests use
synthetic text fixtures and exercise the actual command and strict Make target;
they do not establish KiCad parsing or electrical correctness.

## Required component records

Both validation modes require the existing expected U1-U9 component records:
RK3576, RP2350B, LMS7002M, CC1101, ST25R3916, PE42422, MT7922, RK817, and
K3LKBKB0BM-MGCJ, respectively. A missing record is now an error, just like an
incorrect value; complete net-name coverage cannot compensate for missing ICs.
Every duplicate component reference recognized by the parser is also an error,
even if the values agree. The first record is retained for further diagnostics,
not silently overwritten by a later record.

If a record is missing or duplicated, inspect the exact reference in the source
netlist and re-export from the reviewed schematic for the intended revision.
Do not append placeholder records or delete conflicting records merely to make
the checker pass. These expected identities predate this validation change;
they are not newly verified package selections or electrical assignments.

Run `python3 tests/test_netlist_validation.py -v` to exercise the production
command and both Make targets with synthetic fixtures. Coverage includes each
missing required component, net-only input, identical and conflicting duplicate
records in either order, and the existing name/assignment checks. Ancillary
library/model documentation remains warning-only; parsed records do not establish
valid KiCad syntax or complete connectivity.

## Safe local export commands

The export command now fails if the board is missing or any KiCad layer, drill,
or placement command fails or times out. A zero process exit is not sufficient:
each layer command must create a new, nonempty regular `*.gbr` file, the drill
command must create new, nonempty regular `*.drl` files, and placement must
create the requested `ghostblade-PickPlace.pos`. Matching directories, symlinks,
and zero-byte files are rejected; a prior layer's files cannot satisfy a later
command's check. The file checks use the exporter's existing Gerber-extension
convention, not Protel extensions. They check presence and size, not file syntax,
correct layer identity, pad geometry, drill completeness, or electrical safety.

Export requires a new or empty output
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
handling at every export stage, zero-exit commands with missing, empty,
directory, symlink, or unrelated output at every stage, notes-only behavior,
and duplicate-free ZIP
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
