# Hardware Contributor Guide

**GhostBlade — Project NullSpectre**

This guide describes how to contribute to the GhostBlade hardware design: schematics, PCB layout, device tree, and manufacturing files.

---

## 1. Design Philosophy

GhostBlade is a **dual-processor pentesting device** with strict requirements for:

- **RF integrity**: The SDR path (LMS7002M) and sub-GHz path (CC1101) require controlled impedance, proper decoupling, and RF isolation from digital noise.
- **Thermal management**: The RK3576 can dissipate up to 5.2W. The aluminum mid-frame serves as both EMI shield and heatsink.
- **Power efficiency**: Battery life targets are 4h active SDR, 12h light compute. Every mW matters.
- **Security**: The SPI bridge between RK3576 and RP2350B uses CRC-64/CRC-32 integrity. Future versions will add AES-128-CTR encryption.

When proposing changes, consider the impact on these design priorities.

---

## 2. Schematic & PCB Workflow

### 2.1 Required Software

- **KiCad 8+** (nightly or stable). Download from <https://www.kicad.org/>
- **KiCad 8 project file**: `hardware/kicad/ghostblade.kicad_pro`

### 2.2 Making Schematic Changes

1. Open `ghostblade.kicad_pro` in KiCad.
2. Make changes in the **Schematic Editor** (Eeschema).
3. Run **Electrical Rules Check** (ERC) and fix all violations.
4. Update the netlist: `Tools → Generate Netlist`.
5. Commit the following files:
   - `ghostblade.kicad_sch` (schematic)
   - `ghostblade.net` (netlist)
   - Any changed symbol library files in `symbols/`

### 2.3 Making PCB Layout Changes

1. Import the updated netlist in **PCB Editor** (Pcbnew).
2. Route changes following the design rules:
   - **RF traces**: 50Ω impedance, coplanar waveguide with ground stitching
   - **High-speed**: Length-matched differential pairs for MIPI-CSI-2, USB 3.2, PCIe
   - **Power**: 2oz copper for power planes, star-topology decoupling
   - **Clearance**: 0.1mm minimum for 6-layer FR-4 (IPC Class 3)
3. Run **Design Rules Check** (DRC) and fix all violations.
4. Commit the `.kicad_pcb` file and any changed footprint files.

### 2.4 Design Rule Configuration

The project includes a KiCad DRC profile. Key rules:

| Parameter | Value |
|-----------|-------|
| Minimum track width | 0.1mm (digital), 0.15mm (RF) |
| Minimum clearance | 0.1mm |
| Minimum via diameter | 0.3mm |
| Minimum via drill | 0.2mm |
| Differential pair gap | 0.15mm (MIPI), 0.1mm (USB) |
| Board thickness | 1.6mm (6-layer, Isola 370HR) |

### 2.5 Net Naming Conventions

Nets in the schematic follow these prefixes:

| Prefix | Domain | Example |
|--------|--------|---------|
| `NET_SPI_` | SPI bus signals | `NET_SPI_CLK`, `NET_SPI_MOSI` |
| `NET_I2C_` | I2C bus signals | `NET_I2C_SDA`, `NET_I2C_SCL` |
| `NET_UART_` | UART signals | `NET_UART_TX`, `NET_UART_RX` |
| `NET_GPIO_` | General-purpose GPIO | `NET_GPIO_INT_REQ` |
| `NET_SDR_` | SDR-specific signals | `NET_SDR_RESET`, `NET_SDR_LNA_EN` |
| `NET_NFC_` | NFC-specific signals | `NET_NFC_IRQ`, `NET_NFC_SPI_CSN` |
| `NET_CC_` | CC1101-specific signals | `NET_CC_GDO0`, `NET_CC_GDO2` |
| `NET_ANT_` | Antenna switch signals | `NET_ANT_SEL0`, `NET_ANT_SEL1` |
| `VDD_` | Power rails | `VDD_CORE`, `VDD_3V3`, `VDD_1V2_SDR` |
| `GND` | Ground | Single net name |

All new nets must follow these conventions. Check existing netlists before adding new ones.

---

## 3. Device Tree Contributions

Device tree source files are in `software/dts/`:

- `ghostblade-rk3576.dts` — Main DTS for the RK3576 platform
- `ghostblade-options.dts` — Overlay for optional hardware configurations

### 3.1 DTS Style Guide

- Use 4-space indentation (no tabs).
- Node names use lowercase with hyphens: `spi-controller@fe610000`
- Property names use lowercase with hyphens: `spi-max-frequency`
- Hex values use lowercase: `0xfe610000`
- Always include `compatible`, `reg`, and `status` properties.
- Add pinctrl entries for every GPIO-consuming node.
- Add regulator entries with proper `startup-delay-us` and `off-on-delay-us`.

### 3.2 Netlist Cross-Reference

When adding a DTS node, verify the pin assignments against the schematic netlist (`ghostblade.net`). The DTS GPIO numbers must match the netlist signal names.

---

## 4. Manufacturing File Generation

### 4.1 Gerber Files

```bash
python3 tools/generate_gerbers.py --fab-note --zip
```

This produces `ghostblade-gerbers.zip` containing all manufacturing layers plus a fabrication note.

### 4.2 BOM Review

Before submitting a PR that changes the BOM:

1. Update `hardware/bom/ghostblade-bom.csv` with new/changed parts.
2. Verify pricing in the interactive HTML BOM.
3. Ensure all parts have valid MPNs and at least two sources (manufacturer + distributor).

---

## 5. Code Review Checklist

Before opening a PR, verify:

- [ ] ERC passes with zero violations
- [ ] DRC passes with zero violations
- [ ] Netlist is regenerated after schematic changes
- [ ] DTS GPIO numbers match schematic net names
- [ ] New components have symbols, footprints, and 3D model references
- [ ] Power domain changes are reflected in `docs/power-tree.md`
- [ ] BOM is updated with new parts
- [ ] `stats.json` is updated if line counts have changed

---

## 6. File Organization

```
hardware/
├── bom/
│   ├── ghostblade-bom.csv
│   └── ghostblade-bom-interactive.html
├── kicad/
│   ├── ghostblade.kicad_pro
│   ├── ghostblade.kicad_sch
│   ├── ghostblade.kicad_pcb
│   ├── ghostblade.net
│   ├── symbols/
│   │   └── ghostblade-symbols.kicad_sym
│   ├── footprints/
│   │   └── ghostblade-footprints.pretty/
│   │       └── ghostblade-footprints.kicad_mod
│   └── 3dmodels/
│       └── README.md
└── drc/
    └── ghostblade-drc-rules.kicad_drc  ← DRC rule file
```

---

## 7. Questions?

- Open an issue at <https://github.com/jayis1/ghostblade/issues>
- See [CONTRIBUTING.md](../CONTRIBUTING.md) for general contribution guidelines
- See [getting-started.md](getting-started.md) for dev environment setup