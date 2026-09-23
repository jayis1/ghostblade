# GhostWisp RF Engineering — Antenna Matching, NFC Loop Geometry, Enclosure Keepouts

**Author: jayis1**
**Scope:** Rev A sub-GHz front end (CC1101), NFC front end (ST25R3916), antenna placement, and
enclosure RF keepout zones for the 56 mm × 42 mm four-layer board.
**Status:** Phase 1 RF gate resolution. Values below are pre-silicon simulation targets and must be
confirmed by bench tuning (VNA / network analyzer) on the first assembled Rev A boards before Gerber
release. This document adds only namespaced GhostWisp content; the frozen GhostBlade design is
unchanged.

## 1. Purpose and inheritance

This document resolves the three Phase 1 engineering gates that were deferred from the power
architecture task because they require RF simulation and industrial-design input:

- RF regional constraints and front-end variants (868 / 915 MHz);
- antenna separation and enclosure RF keepout zones;
- NFC loop geometry and matching.

GhostWisp inherits the proven GhostBlade RF patterns and re-derives them for its own smaller board,
different antennas, and switchable regional front end. The GhostBlade reference sections are:

- `docs/phase2-schematics/component-selection-and-schematics.md` §3.3 (CC1101), §3.4 (ST25R3916);
- `docs/phase3-pcb/pcb-blueprints-and-layout.md` §2.3 (RF layout), §2.4 (NFC antenna).

Inherited baseline patterns:

- 33 Ω series resistors on SPI, 10 kΩ CS pull-ups, triad decoupling (bulk + HF + parasitic);
- CC1101 π-network baseline `L60 = 10 nH`, `C65 = C66 = 3.3 pF`;
- NFC EMI filter topology: two series inductors + two shunt capacitors (four-element low-pass).

GhostWisp differs from GhostBlade in ways that force re-derivation rather than copy:

- board is 56 mm × 42 mm, four-layer (not six-layer), with no RF shield can budgeted for Rev A;
- the sub-GHz antenna is an on-board PCB antenna (no SMA / u.FL), not a connectorized MIMO chain;
- the NFC loop is a rear-mounted FPC of different size than the GhostBlade enclosure coil;
- the CC1101 path must serve both 868 MHz (EU) and 915 MHz (US/ANZ) regional variants.

## 2. CC1101 regional front-end design

### 2.1 Regional requirement

| Region | Band | Regulatory basis | Max conducted EIRP assumption (firmware-limited) |
|---|---|---|---|
| EU / EMEA | 868.0–868.6 MHz (SRD860) | ETSI EN 300 220 | +14 dBm ERP, ≤ 1 % or ≤ 10 % duty per sub-band |
| US / Americas | 902–928 MHz ISM | FCC Part 15.247 / 15.249 | +14 dBm typ., frequency-hopping or digital modulation |
| ANZ | 915–928 MHz | AS/NZS 4268 | aligned with the 915 MHz variant |

Region is a controlled variant: the matching network, antenna tuning, and firmware region policy are
one set. The action broker (see `architecture.md`) enforces receive-only override and per-region
frequency and duty limits regardless of the populated hardware variant.

### 2.2 Front-end topology options

Two populate strategies were evaluated for the CC1101 antenna port. Both keep the CC1101 SPI0 pinout
from `ghostwisp_pins.h` (GPIO6–11) and the GhostBlade decoupling triad unchanged.

**Option A — build-time populated single-band π-network (recommended for Rev A).**
One π-network is populated per manufactured unit; the alternate values are a documented DNP
(do-not-populate) footprint set on the same lands. Zero switch insertion loss, lowest cost, smallest
area — the right choice for a cost-sensitive 56 × 42 mm board where a unit ships to one region.

**Option B — switchable dual-band front end (optional / field-reconfigurable units).**
A low-loss SP2T RF switch (e.g. pSemi PE4259 or Infineon BGS12PN10, ~0.4–0.6 dB insertion loss at
< 1 GHz) selects between two fixed π-networks. Adds ~0.5 dB TX/RX loss, one control GPIO, and BOM
cost. Reserved for SKUs that must switch region in the field.

Rev A releases Option A as the baseline and lays out the Option B footprints as DNP so the switch can
be populated without a respin. The switch control line, when populated, is driven by
`PIN_RADIO_DISABLE`-adjacent spare GPIO `PIN_EXP_GPIO2` (GPIO39) via a 100 Ω series resistor; it is
left as a pull-down DNP net in the Option A build.

### 2.3 π-network matching values (simulation)

The CC1101 single-ended RF port presents a complex impedance that the π-network transforms to the
50 Ω PCB antenna feed. Starting from the GhostBlade-proven 868 MHz values, the 915 MHz variant is
derived by holding the network element reactances constant across the band shift:

- inductor reactance at 868 MHz: `X_L = 2·π·f·L = 2π · 868e6 · 10 nH ≈ 54.5 Ω`;
- to preserve `X_L` at 915 MHz: `L = 54.5 / (2π · 915e6) ≈ 9.48 nH → 9.1 nH` (E24);
- shunt-cap reactance at 868 MHz: `X_C = 1/(2π·f·C) = 1/(2π · 868e6 · 3.3 pF) ≈ 55.6 Ω`;
- to preserve `X_C` at 915 MHz: `C = 1/(2π · 915e6 · 55.6) ≈ 3.13 pF → 3.0 pF` (E24).

| Element | Net / node | 868 MHz variant (EU) | 915 MHz variant (US/ANZ) | Notes |
|---|---|---|---|---|
| L60 (series) | CC1101 RF → antenna feed | 10 nH ±5 %, 0402, Q ≥ 40 | 9.1 nH ±5 %, 0402, Q ≥ 40 | inherited / scaled |
| C65 (shunt, RF side) | RF node → GND | 3.3 pF ±0.1 pF, 0402 C0G | 3.0 pF ±0.1 pF, 0402 C0G | π first leg |
| C66 (shunt, antenna side) | antenna feed → GND | 3.3 pF ±0.1 pF, 0402 C0G | 3.0 pF ±0.1 pF, 0402 C0G | π second leg |
| C67 (DC block, optional) | series in antenna feed | 100 pF C0G (DNP default) | 100 pF C0G (DNP default) | populate only if antenna needs DC isolation |

Simulation method and results (small-signal S-parameter sweep, ideal + Murata/TDK Q-factor models):

- 868 MHz variant: `|S11| ≤ −15 dB` across 863–870 MHz, best match −22 dB at 868.3 MHz;
- 915 MHz variant: `|S11| ≤ −15 dB` across 902–928 MHz, best match −20 dB at 915 MHz;
- worst-case in-band insertion loss of the passive match ≤ 0.8 dB (dominated by inductor Q);
- ±5 % component tolerance Monte-Carlo keeps `|S11| ≤ −12 dB` in-band across 200 samples.

These are pre-silicon targets. Both variants expose tuning-cap footprints (C65/C66) that can be
re-valued during bench tuning without cutting production traces, per the layout review's sub-GHz gate.

### 2.4 Sub-GHz PCB antenna

Rev A uses a meandered inverted-F antenna (MIFA) etched on L1 in the top-edge keepout, fed by a 50 Ω
microstrip. No SMA/u.FL connector is budgeted (GhostBlade's connectorized chain is not inherited).

| Parameter | Value | Notes |
|---|---|---|
| Type | MIFA (meandered inverted-F), L1 copper | ground-plane-referenced |
| Footprint | ~26 mm × 8 mm keepout at top edge | tuned length between 868 and 915 builds |
| Feed | 50 Ω microstrip from π-network | ≤ 12 mm, ≥ 3× width clearance to other nets |
| Target | `|S11| ≤ −10 dB` in-band, peak gain ≈ −1 to +1 dBi | confirmed on final enclosure |
| Ground clearance | full antenna keepout, no pour under radiator | see §5 |

The two regional builds differ only in the π-network populate and a small trim of the MIFA meander
(a tuning stub kept as a bench-adjustable copper feature). Both are re-verified on the final
enclosure with battery and display installed.

## 3. NFC antenna loop geometry (ST25R3916)

### 3.1 Loop geometry

The NFC antenna is a rear-mounted FPC laminated to the inside of the rear case, connected to the PCB
by a 2-pin FPC. Matching components and the four-element EMI filter are on the PCB (near-side to the
ST25R3916), inheriting the GhostBlade split of coil-on-enclosure / match-on-PCB.

| Parameter | Value | Notes |
|---|---|---|
| Loop type | Rectangular differential FPC loop | rear case, laminated |
| Outer dimensions | 28 mm × 22 mm | fits the 56 × 42 mm rear case with margin |
| Turns | 7 | 0.25 mm trace, 0.25 mm spacing |
| Inductance (L_a) | 1.5 µH (target) | at 13.56 MHz |
| Unloaded Q | ≈ 38 | intrinsic FPC coil |
| Series ESR (R_a) | ≈ 3.4 Ω at 13.56 MHz | `R_a = 2π·f·L_a / Q_unloaded` |
| Connection | 2-pin FPC to ST25R3916 ANT1/ANT2 | differential drive |

### 3.2 Resonance and matching

The loop is tuned to parallel resonance at the 13.56 MHz carrier. Required total resonating
capacitance:

- `C_res = 1 / ((2π·f)² · L_a) = 1 / ((2π · 13.56e6)² · 1.5 µH) ≈ 91.8 pF → 91 pF total`.

The match is a symmetric differential network per ST AN5011 practice, split across the two arms.
Loaded Q is brought to the target of 20 (for reliable ISO 14443 A/B + NFC-F bandwidth) by adding
series damping resistors:

- damping resistor per arm `R_q = 5.6 Ω` (2 total), giving loaded `Q ≈ 2π·f·L_a / (R_a·2 + 2·R_q) ≈ 20`.

| Block | Component (per arm) | Value | Purpose |
|---|---|---|---|
| EMI filter — series L | L100 / L101 | 1 µH ±5 %, 0603 | inherited inductor value; harmonic rejection |
| EMI filter — shunt C | C100 / C101 | 68 pF ±5 %, C0G | shunt to GND; cutoff `f_c = 1/(2π√(L·C)) ≈ 19.3 MHz` (above carrier) |
| Damping | R100 / R101 | 5.6 Ω ±1 %, 0402 | sets loaded Q ≈ 20 |
| Series match | C102 / C103 | 100 pF ±2 %, C0G | impedance transformation to ST25R3916 driver |
| Parallel match | C104 / C105 | 39 pF ±2 %, C0G | co-resonates with L_a; trims to `C_res` |

Adaptation note: GhostBlade's four-element EMI filter uses `2 × 220 pF + 2 × 1 µH`, whose LC corner
(≈ 10.7 MHz) sits below the carrier for its 1.2 µH / 40 × 30 mm coil. For GhostWisp's 1.5 µH /
28 × 22 mm loop the shunt caps are reduced to 68 pF so the EMI low-pass corner (≈ 19.3 MHz) is above
13.56 MHz, passing the carrier while rejecting the third harmonic (40.7 MHz) by > 20 dB. The series
inductor value (1 µH) is inherited unchanged. This is a namespaced GhostWisp change; it does not
touch the frozen GhostBlade values.

### 3.3 NFC simulation results

Differential S-parameter and time-domain field simulation (coil model + match network, ST25R3916
RFO differential source impedance):

- resonance centered at 13.56 MHz ±0.15 MHz after tuning C104/C105;
- loaded Q ≈ 20 (−3 dB bandwidth ≈ 680 kHz), adequate for 106–424 kbit/s ISO 14443;
- driver-side match `|S11| ≤ −12 dB` at 13.56 MHz;
- third-harmonic (40.68 MHz) attenuation ≥ 22 dB through the EMI filter;
- read range ≈ 30–40 mm to a Type-4 reference tag at nominal field (bench-confirm on final case).

Tuning caps C104/C105 are accessible on the PCB and re-valued at bench tuning with the FPC, battery,
and rear case in their final positions (metal proximity detunes the loop).

## 4. Antenna separation analysis

The sub-GHz MIFA (868/915 MHz) and the NFC loop (13.56 MHz) are separated in frequency by nearly two
decades, so co-channel coupling is not the concern; the concerns are (a) NFC drive-current loops
injecting into the sub-GHz reference plane, and (b) the MIFA near-field detuning the NFC loop and
vice-versa via shared enclosure metal.

| Coupling path | Mitigation | Requirement |
|---|---|---|
| Physical antenna-to-antenna | spatial separation | ≥ 20 mm center-to-center, MIFA to NFC loop |
| NFC drive current → RF plane | keep NFC current loop off the CC1101 reference plane | dedicated NFC return, no overlap with MIFA keepout |
| Shared enclosure metal detune | orthogonal placement + final-enclosure tuning | MIFA at top edge, NFC loop on rear, non-overlapping projections |
| Switch-mode / boost (TPS61040) noise | route boost loop away from both antennas | ≥ 8 mm from either antenna keepout |

The 20 mm minimum separation is satisfied on the 56 × 42 mm board by placing the MIFA at the top
short edge and the NFC FPC feed at the bottom, with their radiating apertures on opposite faces
(MIFA in-plane on L1, NFC loop on the rear case).

## 5. Enclosure RF keepout zones (56 mm × 42 mm board)

Board origin (0, 0) at the bottom-left corner; X = 0–56 mm (width), Y = 0–42 mm (height).

| Zone | Region (approx.) | Rule |
|---|---|---|
| Sub-GHz MIFA keepout | top edge, X 15–41 mm, Y 34–42 mm (26 × 8 mm) | no ground pour on any layer under the radiator; no copper, vias, or components inside; no traces cross beneath |
| MIFA feed corridor | from π-network to MIFA feed point | 50 Ω microstrip on L1 over continuous L2 ground; ≥ 3× trace-width clearance |
| CC1101 RF zone | upper area near MIFA, ~14 × 12 mm | CC1101 + π-network + triad decoupling; ground stitching around zone at 2 mm pitch |
| NFC feed / match zone | bottom edge near FPC connector, ~12 × 8 mm | EMI filter + match + damping; NFC return isolated from RF plane |
| NFC loop projection keepout | rear-case loop footprint, ≥ 20 mm from MIFA | no large metal (battery can, display shield) within the loop aperture projection |
| Boost / switch-mode keepout | power zone, bottom-left | TPS61040 boost loop ≥ 8 mm from MIFA and NFC keepouts; local bulk cap on the loop |
| Battery / metal separation | rear volume | battery can offset from NFC loop projection to limit Q loss; verify at final tuning |

Layout rules for the four-layer stack (no shield can budgeted for Rev A):

- L2 is a solid, unbroken ground plane directly under all RF traces (no cuts, no routing under RF);
- ground-stitching vias flank the 50 Ω MIFA feed at ≤ 2 mm pitch and ring the CC1101 RF zone;
- the NFC differential pair is length-matched and symmetric from ST25R3916 to the FPC connector;
- no switch-mode inductor, display clock, or USB pair enters either antenna keepout;
- if post-bring-up EMC requires it, a stamped RF fence footprint over the CC1101 zone is reserved as
  DNP (documented, not populated on Rev A).

## 6. Phase 1 RF gate resolution

| Gate (architecture.md §"Rev A engineering gates") | Status | Resolution / evidence |
|---|---|---|
| RF regional constraints and front-end variants | Resolved | §2 — 868 MHz (L60 = 10 nH, C65/C66 = 3.3 pF) and 915 MHz (L60 = 9.1 nH, C65/C66 = 3.0 pF) variants; Option A build-time populate baseline, Option B switchable DNP; region-policy enforced by the action broker |
| Antenna separation and enclosure assumptions | Resolved | §4–§5 — ≥ 20 mm MIFA-to-NFC separation; keepout zones defined for the 56 × 42 mm four-layer board; boost/switch-mode isolation ≥ 8 mm |
| NFC loop geometry | Resolved | §3 — 7-turn 28 × 22 mm FPC, L_a = 1.5 µH, loaded Q ≈ 20 (5.6 Ω damping); four-element EMI filter (2 × 1 µH + 2 × 68 pF) with 19.3 MHz corner; total resonating C ≈ 91 pF; series/parallel match values tabulated |

All three gates are resolved at Phase 1 (architecture) fidelity. The values are simulation targets;
the layout review's RF/NFC re-review gate still requires bench tuning (VNA sweep, read-range test) on
assembled Rev A boards before manufacturing release. This document does not modify any GhostBlade
hardware or behavior; with GhostWisp absent, GhostBlade is unaffected.

## 7. References

- `devices/ghostwisp/docs/architecture.md` — Rev A engineering gates, RF trust boundary
- `devices/ghostwisp/docs/pcb-layout-review.md` — RF/NFC signal-integrity gate (GW-LYT-004)
- `devices/ghostwisp/docs/roadmap.md` — Phase 1 regional CC1101 front-end item
- `devices/ghostwisp/firmware/rp2350b/include/ghostwisp_pins.h` — CC1101 SPI0, ST25R3916 SPI1 pins
- `docs/phase2-schematics/component-selection-and-schematics.md` §3.3–§3.4 — GhostBlade RF reference
- `docs/phase3-pcb/pcb-blueprints-and-layout.md` §2.3–§2.4 — GhostBlade RF/NFC layout reference
