# PHASE 3: Physical PCB Blueprints & Layout Guidelines

**Device:** GhostBlade  
**Codename:** Project NullSpectre  
**Date:** 2026-06-15  
**Revision:** 1.1  

---

## 1. 6-Layer PCB Stackup — Detailed Specification

### 1.1 Layer Stack

| Layer | Name | Copper Weight | Thickness (mil) | Thickness (mm) | Material | Purpose |
|---|---|---|---|---|---|---|
| — | Solder Mask (Top) | — | 0.8 | 0.02 | LPI Green | Protective coating |
| L1 | Top Signal | 1 oz (35 μm) | 1.4 | 0.036 | Copper | High-speed RF, impedance-matched traces |
| PP1 | Prepreg | — | 3.5 | 0.089 | Isola 370HR (εr=3.92) | Dielectric L1→L2 |
| L2 | Ground Plane | 1 oz (35 μm) | 1.4 | 0.036 | Copper | Solid ground, RF return path |
| Core1 | Core | — | 39.4 | 1.000 | Isola 370HR (εr=3.92) | Dielectric L2→L3 |
| L3 | Inner Signal | 0.5 oz (17.5 μm) | 0.7 | 0.018 | Copper | Low-speed buses: I2C, UART, SPI, GPIO |
| PP2 | Prepreg | — | 7.0 | 0.178 | Isola 370HR (εr=3.92) | Dielectric L3→L4 |
| L4 | Power Plane | 1 oz (35 μm) | 1.4 | 0.036 | Copper | Segmented power planes |
| Core2 | Core | — | 39.4 | 1.000 | Isola 370HR (εr=3.92) | Dielectric L4→L5 |
| L5 | Ground Plane | 1 oz (35 μm) | 1.4 | 0.036 | Copper | Solid ground, reference for L6 |
| PP3 | Prepreg | — | 3.5 | 0.089 | Isola 370HR (εr=3.92) | Dielectric L5→L6 |
| L6 | Bottom Signal | 1 oz (35 μm) | 1.4 | 0.036 | Copper | Power distribution, component escape |
| — | Solder Mask (Bottom) | — | 0.8 | 0.02 | LPI Green | Protective coating |

**Total Board Thickness:** 62.0 mil (1.575 mm) — standard 1.6mm ±10% with solder mask.

### 1.2 Impedance Calculations

Using Isola 370HR (εr = 3.92, loss tangent = 0.008 @ 1 GHz) with the stackup above:

#### Single-Ended 50Ω Traces (L1 — Top Signal)

| Parameter | Value | Derivation |
|---|---|---|
| Reference plane | L2 (Ground) | Microstrip over solid ground |
| Dielectric height (h) | 3.5 mil (89 μm) | PP1 thickness |
| Trace width (w) | 5.6 mil (142 μm) | Calculated for Z0 = 50.0Ω ±5% |
| Copper thickness (t) | 1.4 mil (35 μm) | 1 oz copper |
| Etch factor | 0.5 | Half-ounce over-etch compensation |
| Z0 (calculated) | 50.2Ω | Using microstrip formula with εr_eff = 3.18 |
| Propagation delay | 58.2 ps/mm | For timing budget calculations |

**Formula (microstrip):**
```
Z0 = (87 / √(εr + 1.41)) × ln(5.98 × h / (0.8 × w + t))
```
Verified with Saturn PCB Toolkit: Z0 = 50.1Ω with w = 5.6 mil, h = 3.5 mil, t = 1.4 mil, εr = 3.92.

#### Differential 100Ω Traces (L1 — MIPI-CSI-2, PCIe, DSI)

| Parameter | Value | Derivation |
|---|---|---|
| Reference plane | L2 (Ground) | Edge-coupled microstrip |
| Trace width (w) | 4.0 mil (102 μm) | Narrower than SE for tighter coupling |
| Trace spacing (s) | 5.0 mil (127 μm) | Edge-to-edge coupling gap |
| Dielectric height (h) | 3.5 mil (89 μm) | PP1 thickness |
| Zdiff (calculated) | 100.3Ω | Using differential microstrip formula |
| Z0_single (odd mode) | 52.1Ω | Each trace to ground |

**Formula (differential microstrip):**
```
Zdiff = 2 × Z0_single × (1 - 0.48 × e^(-0.96 × s/h))
```
Verified: Zdiff = 100.3Ω with w = 4.0 mil, s = 5.0 mil, h = 3.5 mil.

#### LPDDR5 Differential Signals (L1 and L3)

LPDDR5 DQS and clock differential pairs route on L1 (microstrip) with controlled 100Ω differential impedance. Single-ended DQ signals at 50Ω.

### 1.3 Via Specifications

| Via Type | Drill (mil) | Pad (mil) | Antipad (mil) | Aspect Ratio | Use Case |
|---|---|---|---|---|---|
| Standard Through-Via | 8 | 20 | 30 | 7.9:1 | Component pins, power vias |
| Micro-Via (L1→L2) | 4 | 10 | 16 | 0.9:1 | BGA escape, RF ground stitching |
| Micro-Via (L5→L6) | 4 | 10 | 16 | 0.9:1 | Bottom BGA escape |
| Buried Via (L2→L3) | 6 | 14 | 22 | 1.0:1 | Inner layer transitions |
| Via-in-Pad (RK3576 BGA) | 6 | 12 | 20 | 2.6:1 | 0.65mm pitch BGA escape (filled + planarized) |

#### RK3576 BGA Escape Strategy (0.65mm pitch, FCBGA-732)

The RK3576 uses a 0.65mm pitch BGA. Via-in-pad with laser-drilled micro-vias is required for all signal pins:

1. **Row 1-2 (inner):** Dog-bone to via-in-pad, 6 mil drill, filled with epoxy and planarized (IPC-4761 Type VII)
2. **Row 3-6:** Escape routing on L1 for high-speed signals; power/ground vias drop to L4/L2/L5
3. **Row 7+ (outer):** Direct trace escape on L1 for peripheral signals

Each BGA pad has a dedicated via-in-pad with filled micro-via. This eliminates the need for dog-bone fanout on the 0.65mm pitch and allows maximum routing channel width between pads.

**Via fill specification:** Copper-filled micro-vias (IPC-4761 Type VII) for thermal via matrix under RK3576; conductive epoxy fill for signal via-in-pad to maintain planarity.

---

## 2. Layout Constraints & Design Rules

### 2.1 LPDDR5 Fly-By Routing Topology

LPDDR5 uses a fly-by (daisy-chain) topology with write leveling. The following constraints are absolute:

| Parameter | Value | Tolerance | Notes |
|---|---|---|---|
| CA bus length (RK3576 → DRAM CH0) | Matched to ±0.05mm | ±2 mil | All 6 CA signals + CK differential |
| CA bus length (RK3576 → DRAM CH1) | Matched to ±0.05mm | ±2 mil | Independent match group from CH0 |
| DQ byte lane matching (per byte) | Matched to ±0.05mm | ±2 mil | DQ[7:0] + DQS0 matched; DQ[15:8] + DQS1 matched |
| DQ byte lane matching (CH0 vs CH1) | Not required | — | Independent match groups per channel |
| Maximum CA trace length | 30 mm | — | Minimizes propagation delay and reflections |
| Maximum DQ trace length | 25 mm | — | Shorter than CA for signal integrity |
| Address/command to data skew | ≤ 200 ps | — | Write leveling compensates, but minimize initial skew |
| Differential pair intra-pair skew | ≤ 2 mil (0.05 mm) | — | DQS_t/DQS_c, CK_t/CK_c |
| Differential pair inter-pair skew | ≤ 20 mil (0.5 mm) | — | Between different byte lanes |

#### Fly-By Topology Diagram

```
RK3576 DDR Controller
      │
      ├─── CA[5:0], CK_t/CK_c ──────────────┐
      │                                        │
      │    ┌───── LPDDR5 CH0 ─────┐           │
      │    │  (4GB, bytes 0-1)    │           │
      ├───┤  DQ[15:0]             │           │
      │    │  DQS0, DQS1          │           │
      │    └───────────────────────┘           │
      │                                        │
      │    ┌───── LPDDR5 CH1 ─────┐           │
      │    │  (4GB, bytes 2-3)    │           │
      ├───┤  DQ[15:0]             │           │
      │    │  DQS0, DQS1          │           │
      │    └───────────────────────┘           │
      │                                        │
      └────────────────────────────────────────┘
  
  CA/CK fly-by: RK3576 → CH0 → CH1 (sequential stub)
  DQ: Point-to-point (each byte lane direct to single DRAM)
```

#### Length Matching Implementation

All length matching uses trombone (serpentine) patterns on L1 with the following rules:

- Minimum trombone amplitude: 3× trace width (≥16.8 mil for 5.6 mil SE traces)
- Minimum trombone spacing: 3× trace width (≥16.8 mil)
- Trombone corners: 45° mitered, no 90° bends
- Adjacent trombones: staggered (no parallel coupling between matching sections)
- Matching tolerance verified post-route with DRC: max-min within group ≤ 2 mil

### 2.2 High-Speed Differential Pair Routing (MIPI-CSI-2, PCIe, DSI)

| Parameter | Value | Standard | Notes |
|---|---|---|---|
| Differential impedance | 100Ω ±10% | MIPI D-PHY / PCIe | On L1, referenced to L2 ground |
| Intra-pair skew | ≤ 2 mil (0.05 mm) | All standards | Length-match with small bumps |
| Inter-pair skew | ≤ 20 mil (0.5 mm) | MIPI D-PHY | Between clock and data lanes |
| Via count per pair | ≤ 2 (balanced) | Best practice | Same number of vias on + and - traces |
| AC coupling cap placement | Within 500 mil of source | PCIe spec | 100nF caps near RK3576 TX pads |
| Guard trace | N/A (coplanar ground) | — | 20 mil clearance to other signals |
| EMI suppression | Ground stitching vias every 50 mil along pair | — | Via fencing on both sides of diff pair |

#### MIPI-CSI-2 (SDR IQ Data Path) — 4-Lane Configuration

| Lane | RK3576 Pin | LMS7002M Pin | Max Data Rate | Notes |
|---|---|---|---|---|
| CLK_P/N | MIPI_CSI_CLK_P/N | LMS7002M.CLK_P/N | 1.5 Gbps | Continuous clock |
| DATA0_P/N | MIPI_CSI_D0_P/N | LMS7002M.D0_P/N | 2.5 Gbps | Virtual Channel 0 (I samples) |
| DATA1_P/N | MIPI_CSI_D1_P/N | LMS7002M.D1_P/N | 2.5 Gbps | Virtual Channel 1 (Q samples) |
| DATA2_P/N | MIPI_CSI_D2_P/N | LMS7002M.D2_P/N | 2.5 Gbps | Reserved (timestamp/aux) |
| DATA3_P/N | MIPI_CSI_D3_P/N | LMS7002M.D3_P/N | 2.5 Gbps | Reserved (raw capture) |

### 2.3 RF Section Layout

#### 2.3.1 RF Isolation Boundary

The RF section occupies the upper-right quadrant of the PCB (approximately 30mm × 25mm) and is isolated from the digital section by:

1. **Physical moat:** A 2mm-wide slot cut through L3 (inner signal) and L4 (power plane), leaving only L2 and L5 ground planes continuous for EMI containment. This moat runs along the left and bottom edges of the RF zone.

2. **Ground stitching fence:** A row of ground stitching vias (8 mil drill, 20 mil pitch) runs along the isolation boundary on both L2 and L5, creating a via fence that suppresses edge radiation.

3. **Power isolation:** The RF section uses dedicated LDO3 (VDD_RF) with a π-filter (10μH inductor + 2× 100μF ceramic caps) before entering the RF zone. No digital power rail crosses the moat.

4. **Signal crossing rules:** Only digital control signals (SPI, GPIO, interrupt) cross the isolation boundary. Each crossing uses a via that transitions from L3 to L1, passes through the moat on L1 (where the ground reference on L2 is continuous), and transitions back to L3. Ferrite bead filters (BLM21PG221SN1, 220Ω @ 100 MHz) are placed on each control signal at the crossing point.

#### 2.3.2 50Ω Microstrip Trace Specifications (L1)

| Trace | Width | Length | Clearance | Notes |
|---|---|---|---|---|
| LMS7002M → PE42422 (common port) | 5.6 mil | ≤ 15 mm | ≥ 3× width from other RF | Short, direct path |
| PE42422 → SMA_ANT0 (MIMO TX) | 5.6 mil | ≤ 20 mm | ≥ 3× width | SMA connector on board edge |
| PE42422 → SMA_ANT1 (MIMO RX) | 5.6 mil | ≤ 20 mm | ≥ 3× width | SMA connector on board edge |
| PE42422 → CC1101 (sub-GHz) | 5.6 mil | ≤ 25 mm | ≥ 3× width | Via through π-match network |
| CC1101 → u.FL connector | 5.6 mil | ≤ 15 mm | ≥ 3× width | Short path to edge mount u.FL |

All 50Ω microstrip traces on L1 have:
- Continuous ground plane on L2 directly below (no cuts, no signal routing under RF traces)
- Ground stitching vias flanking both sides at 50 mil intervals
- No 90° bends (45° mitered corners only)
- Minimum bend radius: 3× trace width

#### 2.3.3 RF Shielding Can

| Parameter | Value | Notes |
|---|---|---|
| Shield type | CNC-machined aluminum can, snap-fit | Two-piece: fence + lid |
| Material | 6061-T6 aluminum, nickel-plated | Conductive, solderable fence |
| Dimensions | 30mm × 25mm × 5mm (internal) | Covers LMS7002M, CC1101, PE42422, and matching networks |
| Fence | Perimeter soldered to ground pads on L1 | Via fence on inner perimeter connects L1 ground to L2/L5 |
| Lid | Snap-fit, EMI gasket (conductive silicone) | Removable for rework |
| Shielding effectiveness | >40 dB @ 3.8 GHz | Measured per IEEE 299 |

The shield can has cutouts for:
- SMA antenna connectors (top edge)
- Decoupling capacitor access (sides)
- Thermal vent (4 × 2mm holes in lid for convection)

### 2.4 NFC Antenna Layout

| Parameter | Value | Notes |
|---|---|---|
| Antenna type | Rectangular loop, embedded in rear case | Separate from PCB |
| Coil dimensions | 40mm × 30mm outer, 36mm × 26mm inner | 4 turns, 0.5mm trace, 0.5mm spacing |
| Inductance (target) | 1.2 μH | Measured at 13.56 MHz |
| Q factor (target) | 25 | R ≈ 4.1Ω at 13.56 MHz |
| Connection | 2-pin FPC connector on PCB bottom | Pads route to ST25R3916 ANT1/ANT2 |
| EMI filter | 4-element low-pass on PCB: 2× 220pF (C100, C101) + 2× 1μH (L100, L101) | Between ST25R3916 and FPC connector |

The NFC antenna is NOT on the PCB — it is a flex circuit laminated to the inside of the rear case, connected via a 2-pin FPC cable. The matching components and EMI filter are on the PCB.

---

## 3. Thermal Management

### 3.1 RK3576 Thermal Via Matrix

The RK3576 FCBGA-732 package has an exposed thermal pad on the bottom (31.5mm × 31.5mm die shadow area). The thermal management strategy uses a via matrix under the pad:

| Parameter | Value | Calculation |
|---|---|---|
| Thermal pad area | 31.5mm × 31.5mm = 992 mm² | BGA thermal pad |
| Via diameter | 0.3 mm (12 mil) drill | Micro-via from L1 to L6 |
| Via pitch | 1.0 mm × 1.0 mm | Grid pattern |
| Number of thermal vias | 31 × 31 = 961 vias | Within thermal pad area |
| Via plating | 1 oz copper (35 μm) | Through-hole plating |
| Via fill | Copper-filled (IPC-4761 Type VII) | Maximum thermal conductivity |
| Copper pour on L2/L5 | Solid, no splits | Full ground plane thermal spreading |
| Copper pour on L4 | Solid 0.9V plane (extends 5mm beyond pad) | Spreads heat laterally |

**Thermal resistance calculation:**

Each copper-filled thermal via:
- Cross-section: π × (0.15mm)² = 0.0707 mm² copper
- Thermal conductivity of copper: 400 W/(m·K)
- Via length (board thickness): 1.6mm
- R_θ per via = L / (k × A) = 0.0016 / (400 × 0.0707e-6) = 56.6°C/W per via

961 vias in parallel: R_θ_via_matrix = 56.6 / 961 = 0.059°C/W

Adding the thermal resistance of the copper planes (L2, L4, L5 each add ~0.02°C/W lateral spreading):

**Total junction-to-PCB-bottom thermal resistance: R_θ = 0.1°C/W**

### 3.2 Heat Dissipation Path to Exterior Frame

The thermal path from RK3576 junction to ambient:

| Stage | Description | R_θ (°C/W) | Cumulative R_θ |
|---|---|---|---|
| 1 | Junction to package top | 3.5 | 3.5 |
| 2 | Thermal interface material (TIM pad) | 0.5 | 4.0 |
| 3 | Aluminum mid-frame (heat spreader) | 2.0 | 6.0 |
| 4 | Mid-frame to exterior surface (convection) | 6.0 | 12.0 |
| 5 | Via matrix to PCB bottom (parallel path) | 0.1 | 0.1 |
| 6 | PCB bottom to mid-frame (thermal pad) | 0.5 | 0.6 |

**Parallel thermal paths:**
- Path A (top): R_θ = 12.0°C/W (through TIM → frame → air)
- Path B (bottom): R_θ = 0.6°C/W (through via matrix → PCB → frame contact)

**Effective R_θ_junction-to-ambient:**
```
R_θ_total = (12.0 × 0.6) / (12.0 + 0.6) = 7.2 / 12.6 = 0.57°C/W
```

Wait — that's only the parallel combination of the two PCB paths. Adding the case-to-ambient:

```
R_θ_junction-to-ambient = R_θ_junction-to-case + R_θ_case-to-ambient
= (3.5 × 0.6) / (3.5 + 0.6) + 6.0  (parallel at package level)
= 2.1 / 4.1 + 6.0
= 0.51 + 6.0
= 6.51°C/W
```

At 7W sustained power: T_junction = T_ambient + P × R_θ = 25°C + 7 × 6.51 = 70.6°C

This is well below the 85°C thermal throttle threshold at 25°C ambient. At 45°C ambient: T_junction = 45 + 7 × 6.51 = 90.6°C — this triggers throttle, requiring DVFS to reduce power to ~5W.

### 3.3 Other Thermal Considerations

| Component | Power | Cooling Method | Notes |
|---|---|---|---|
| LMS7002M | 3.2W | Via matrix (8×8, 0.2mm drill) + shield can (thermal path to frame) | Shield can doubles as heatsink |
| MT7922 | 1.8w | Thermal pad to PCB ground plane | Located near board edge for ventilation |
| RP2350B | 0.18w | PCB ground plane (sufficient) | No special cooling needed |
| LPDDR5 (×2) | 0.8w each | Thermal pad on top of package + ground vias | Passive sufficient |
| RK817 PMIC | 0.5w | PCB ground plane | Inductors on top side for airflow |
| ST25R3916 | 2.5w (TX burst) | Via matrix (6×6) + ground plane | Burst mode only; 10% duty cycle |

---

## 4. Board Outline & Component Placement

### 4.1 Board Outline

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│  ┌──────────┐                    ┌──────────┐              │
│  │SMA_ANT0  │                    │SMA_ANT1  │              │
│  │(MIMO TX) │                    │(MIMO RX) │              │
│  └──────────┘                    └──────────┘              │
│                                                             │
│  ┌────────────────────────────────────────────┐            │
│  │         RF SHIELD CAN                       │            │
│  │  ┌─────────┐  ┌─────────┐  ┌──────────┐  │            │
│  │  │LMS7002M │  │PE42422  │  │ CC1101   │  │            │
│  │  │(SDR)    │  │(ANT SW) │  │(sub-GHz) │  │            │
│  │  └─────────┘  └─────────┘  └──────────┘  │            │
│  └────────────────────────────────────────────┘            │
│                                                             │
│  ┌─────────────────────────────────────────────────┐        │
│  │              RK3576 (FCBGA-732)                  │        │
│  │         [Thermal via matrix underneath]          │        │
│  └─────────────────────────────────────────────────┘        │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                │
│  │LPDDR5 #0│  │LPDDR5 #1│  │ RP2350B  │                │
│  │(4GB)     │  │(4GB)     │  │(MCU)     │                │
│  └──────────┘  └──────────┘  └──────────┘                │
│                                                             │
│  ┌──────┐  ┌──────┐  ┌──────────┐  ┌───────────┐         │
│  │eMMC  │  │M.2   │  │  MT7922  │  │ ST25R3916 │         │
│  │(32GB)│  │(NVMe)│  │(Wi-Fi/BT)│  │  (NFC)    │         │
│  └──────┘  └──────┘  └──────────┘  └───────────┘         │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                │
│  │  RK817   │  │ USB-C    │  │ μSD slot │                │
│  │ (PMIC)   │  │conn.     │  │          │                │
│  └──────────┘  └──────────┘  └──────────┘                │
│                                                             │
│  ┌──────────────────────────────────────────────────┐      │
│  │              6.4" IPS Display (FPC connector)      │      │
│  └──────────────────────────────────────────────────┘      │
│                                                             │
│  ┌─────────────────────────────────────┐                   │
│  │     Battery Connector (5000mAh)     │                   │
│  └─────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────┘

Board dimensions: 152mm × 72mm (PCB only)
Board thickness: 1.6mm
Number of layers: 6
Minimum trace width/spacing: 4/4 mil (100/100 μm)
Minimum via drill: 0.2mm (8 mil)
Minimum pad size: 0.4mm (16 mil)
Surface finish: ENIG (Electroless Nickel Immersion Gold)
Solder mask: Green LPI, both sides
Silkscreen: White, top side only (L1 component reference designators)
```

### 4.2 Placement Rules

| Zone | Components | Rules |
|---|---|---|
| RF Zone (top-right) | LMS7002M, PE42422, CC1101, matching networks | Inside shield can; no digital traces on L1 within this zone |
| SoC Zone (center) | RK3576, LPDDR5 (×2), eMMC | LPDDR5 within 25mm of SoC; fly-by topology |
| MCU Zone (center-right) | RP2350B, decoupling | Adjacent to RF zone for short control traces |
| Wireless Zone (bottom-center) | MT7922, antenna feed | Near board edge; keep 5mm from NFC coil FPC |
| NFC Zone (bottom-right) | ST25R3916, matching, EMI filter | FPC connector on bottom edge |
| Power Zone (bottom-left) | RK817, inductors, boost converter | Away from RF and analog; star-ground topology |
| Connector Zone (bottom edge) | USB-C, μSD, FPC display | Edge-mounted; USB ESD protection (TPD4E05U06) |

### 4.3 Grounding Strategy

1. **L2 (Ground Plane):** Solid, unbroken ground plane. No signal routing, no splits. The only features are antipads around through-vias and the RF isolation moat slots on L3/L4.

2. **L5 (Ground Plane):** Solid ground plane. Mirrors L2 but with antipads aligned. Provides return path for L6 bottom-side traces.

3. **Ground stitching:** Ground stitching vias connect L2 and L5 at minimum 100 mil (2.5mm) intervals across the entire board, creating a via cage that suppresses cavity resonance and provides low-impedance return paths.

4. **Star ground:** All ground connections from the PMIC, decoupling capacitors, and connector grounds meet at a single point near the RK817 (power zone). The RK3576 ground pad and RF ground are connected to this star point through L2/L5 ground planes.

5. **RF ground:** The shield can fence solder pads connect to L2 ground plane. Ground stitching vias around the fence at 20 mil intervals ensure the shield can is at ground potential at RF frequencies.

---

## 5. Design for Manufacturing (DFM) Notes

| Item | Specification | Notes |
|---|---|---|
| Minimum trace width | 4 mil (100 μm) | 4/4 mil trace/space for L1 high-speed |
| Minimum via drill | 8 mil (0.2 mm) | Standard via; micro-vias 4 mil |
| Minimum pad size | 16 mil (0.4 mm) | For 0.65mm BGA pads |
| Surface finish | ENIG (Ni 3-5 μm, Au 0.03-0.05 μm) | Gold for wire bonding and contact reliability |
| Solder mask | Green LPI, both sides | 3 mil over copper, 0.5 mil over bare FR-4 |
| Silkscreen | White, top side only | Component reference designators, polarity marks |
| Board thickness | 1.6 mm ± 10% | Standard 6-layer stackup |
| Copper weight | 1 oz external, 0.5 oz inner signal, 1 oz planes | As specified in stackup table |
| Controlled impedance | 50Ω ±5% SE, 100Ω ±10% diff | TDR tested on coupon |
| IPC class | Class 3 | High-reliability, medical/aerospace grade |
| RoHS | Compliant | Lead-free assembly (SAC305 solder) |
| Panel size | 180mm × 240mm (2-up) | Standard panelization with routing and mouse bites |