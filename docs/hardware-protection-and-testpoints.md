# Hardware Protection, Reset Circuits, and Test Points

This document describes the ESD protection strategy, reset circuits, and test
point placement for the GhostBlade (Project NullSpectre) board. These details
supplement the schematic and are essential for manufacturing, debugging, and
field reliability.

## Table of Contents

- [ESD Protection](#esd-protection)
- [Reset Circuit Design](#reset-circuit-design)
- [Test Points](#test-points)
- [Power Sequencing Margins](#power-sequencing-margins)

---

## ESD Protection

### Design Philosophy

The GhostBlade board operates in pentesting environments where it may be
exposed to electrostatic discharge, RF injection, and physical handling
without proper ESD precautions. All external connectors must have TVS diode
protection to prevent damage to the RK3576, RP2350B, and attached peripherals.

### Protected Interfaces

| Interface | Connector | TVS Device | Manufacturer | Part Number | Clamp Voltage | Notes |
|-----------|-----------|-----------|--------------|-------------|--------------|-------|
| USB-C (power+data) | J1 | 4-channel TVS | TPD4E05U06 | TPD4E05U06DQAR | 6.0 V | D+ / D- and VBUS |
| SMA_ANT0 (MIMO TX) | J2 | RF ESD protector | BGS8N4 | BGS8N4H6327XTSA1 | 5.0 V | PE42422 input |
| SMA_ANT1 (MIMO RX) | J3 | RF ESD protector | BGS8N4 | BGS8N4H6327XTSA1 | 5.0 V | PE42422 input |
| u.FL (sub-GHz) | J4 | RF ESD protector | BGS8N4 | BGS8N4H6327XTSA1 | 5.0 V | CC1101 RF path |
| NFC antenna | J5 | TVS for 13.56 MHz | PRTR5V0U2X | PRTR5V0U2X,215 | 5.5 V | ST25R3916 antenna |
| microSD | J6 | 4-channel TVS | TPD4E05U06 | TPD4E05U06DQAR | 6.0 V | Data + CMD lines |
| M.2 2230 (NVMe) | J7 | Not fitted | — | — | — | Inside enclosure |
| MIPI-CSI-2 (camera) | J8 | 4-channel TVS | TPD4E05U06 | TPD4E05U06DQAR | 6.0 V | Data + CLK lines |
| HDMI 2.1 TX | J9 | 4-channel TVS | TPD4E05U06 | TPD4E05U06DQAR | 6.0 V | TMDS pairs |

### ESD Protection Placement Rules

1. **TVS diodes must be placed within 5 mm of the connector pin** on the PCB
   to minimize trace inductance before the protection device.

2. **RF paths use low-capacitance ESD protectors** (< 0.5 pF) to avoid
   degrading RF performance at GHz frequencies. The BGS8N4 devices have
   0.35 pF typical capacitance.

3. **All TVS devices route directly to the ground plane** via multiple vias
   (minimum 2 vias per ground connection) to minimize ground inductance
   during an ESD event.

4. **Series resistors on debug UART and JTAG** (100 Ω) provide current
   limiting in addition to TVS protection.

5. **The RP2350B BOOTSEL and RESETn pins** have 10 kΩ pull-ups to the
   RP2350B 3.3V rail, with 100 pF decoupling capacitors for noise
   filtering.

### HBM and IEC 61000-4-2 Compliance

All external connectors are rated for:

| Standard | Level | Voltage |
|----------|-------|---------|
| HBM (Human Body Model) | Class 2 | ±4 kV contact, ±8 kV air |
| IEC 61000-4-2 | Level 4 | ±8 kV contact, ±15 kV air |

Internal test points and debug headers (J10, J11) are not required to meet
ESD ratings since they are only accessed with proper ESD precautions during
manufacturing and debugging.

---

## Reset Circuit Design

### RK3576 Reset

The RK3576 uses a supervised reset with the PMIC (RK860-2):

```
                  VCC_SYS_5V
                     |
                  [10 kΩ]
                     |
    PMIC_PWRGD ─────┤────── RK3576 PMIC_RSTn
                     |
                  [0.1 µF]
                     |
                   GND
```

- **PMIC_PWRGD** from RK860-2 asserts when all rails are within regulation.
- **RK3576 RESETn** is driven by the PMIC power-good output, deglitched
  with a 10 kΩ pull-up and 0.1 µF capacitor for a ~1 ms time constant.
- **Reset assertion time**: minimum 10 ms after all power rails are stable
  (RK3576 datasheet requirement).
- **Brownout detection**: PMIC undervoltage lockout at 3.8 V (VCC_SYS_5V)
  triggers a clean reset sequence.

### RP2350B Reset

The RP2350B has two reset sources:

1. **Hardware reset (RP2350B RUNn pin)**:
   - Driven by RK3576 GPIO1_A0 (active-low, open-drain with 10 kΩ pull-up).
   - RK3576 can assert RUNn for ≥ 1 ms to reset the RP2350B.
   - After deassertion, the RP2350B boot sequence takes ~2 ms before
     the SPI0 slave interface is ready.

2. **Software watchdog reset**:
   - The RP2350B watchdog timer (configured in `main.c`) has a 5-second
   timeout. If the firmware hangs, the watchdog triggers a chip reset.
   - Watchdog is fed by the main loop; any hang > 5 seconds causes reset.

3. **BOOTSEL button (momentary ground on RP2350B BOOTSEL pin)**:
   - Holds RP2350B in USB boot mode for firmware recovery.
   - Not a traditional reset — places the RP2350B in bootloader mode.

### LMS7002M Reset

- **LMS7002M RESETn** is driven by RP2350B GPIO 21 (active-low).
- Asserted for minimum 100 ns (LMS7002M datasheet requirement).
- After deassertion, the LMS7002M requires 10 ms before SPI configuration
  can begin.

### CC1101 Reset

- **CC1101 CSn/SRES**: The RP2350B issues an SRES command strobe via SPI1
  to reset the CC1101. No dedicated hardware reset pin is used.
- Reset completion is indicated by MISO going low after CSn assertion.
- Timeout for reset completion: 300 μs (per CC1101 datasheet).

### ST25R3916 Reset

- **ST25R3916 RSTn** is driven by RP2350B GPIO 42 (active-low).
- Minimum reset pulse width: 10 μs.
- After RSTn deassertion, the ST25R3916 oscillator requires 1 ms startup
  time before SPI commands are accepted.

### Reset Timing Diagram

```
Time (ms)  0      1      2      5      10     15     20
           |      |      |      |      |      |      |
PMIC_PWRGD ___/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
RK3576_RSTn ________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
           |<10ms>|
RP2350B_RUNn ______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
           |     |<-2ms->|
LMS_RESETn  _____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
NFC_RSTn    ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
CC1101_SRES     † (SPI command, not shown)
```

Key timing constraints:

| Signal | Deassert After | Minimum Pulse | Stable After |
|--------|---------------|---------------|--------------|
| PMIC_PWRGD | All rails in regulation | — | +0 ms |
| RK3576_RSTn | PMIC_PWRGD high | 10 ms | +10 ms |
| RP2350B_RUNn | RK3576 GPIO1_A0 | 1 ms | +2 ms (boot) |
| LMS7002M_RSTn | RP2350B GPIO 21 | 100 ns | +10 ms (osc startup) |
| ST25R3916_RSTn | RP2350B GPIO 42 | 10 μs | +1 ms (osc startup) |
| CC1101 SRES | RP2350B SPI1 command | 300 μs (MISO poll) | +1 ms (calibration) |

---

## Test Points

The GhostBlade board includes dedicated test points for manufacturing
testing, debugging, and calibration. All test points are 1 mm plated-through
holes (0.7 mm finished) on 1.6 mm centers, compatible with standard probe
tips and 0.1" header pins.

### Power Rail Test Points

| Reference | Net | Nominal Voltage | Tolerance | Location |
|-----------|-----|----------------|-----------|----------|
| TP1 | VCC_SYS_5V | 5.0 V | ±5% | Near J1 (USB-C) |
| TP2 | VCC_3V3_SYS | 3.3 V | ±3% | Near RK3576 |
| TP3 | VCC_1V8_SYS | 1.8 V | ±3% | Near RK3576 |
| TP4 | VCC_1V0_CORE | 1.0 V | ±3% | Under RK3576 BGA |
| TP5 | VCC_3V3_MCU | 3.3 V | ±3% | Near RP2350B |
| TP6 | VCC_1V1_MCU | 1.1 V | ±3% | Near RP2350B core |
| TP7 | VCC_3V3_SDR | 3.3 V | ±3% | Near LMS7002M |
| TP8 | VCC_1V8_SDR | 1.8 V | ±3% | Near LMS7002M |
| TP9 | VCC_1V2_SDR | 1.2 V | ±3% | Near LMS7002M (LNA) |
| TP10 | VCC_3V3_NFC | 3.3 V | ±3% | Near ST25R3916 |
| TP11 | VCC_3V3_RADIO | 3.3 V | ±3% | Near CC1101 |
| TP12 | VCC_5V_ANT | 5.0 V | ±5% | Antenna power feed |

### Signal Test Points

| Reference | Net | Description | Location |
|-----------|-----|-------------|----------|
| TP13 | SPI0_CLK | RK3576 ↔ RP2350B SPI clock | Near RP2350B |
| TP14 | SPI0_CSn | RK3576 ↔ RP2350B SPI chip select | Near RP2350B |
| TP15 | SPI0_MOSI | RK3576 → RP2350B SPI data | Near RP2350B |
| TP16 | SPI0_MISO | RP2350B → RK3576 SPI data | Near RP2350B |
| TP17 | SPI1_CLK | RP2350B ↔ LMS7002M/CC1101 SPI | Near LMS7002M |
| TP18 | LMS_RSTn | LMS7002M reset (active-low) | Near LMS7002M |
| TP19 | CC1101_GDO0 | CC1101 GDO0 interrupt output | Near CC1101 |
| TP20 | CC1101_GDO2 | CC1101 GDO2 interrupt output | Near CC1101 |
| TP21 | NFC_IRQ | ST25R3916 interrupt output | Near ST25R3916 |
| TP22 | MCU_RUNn | RP2350B reset (active-low) | Near RP2350B |
| TP23 | MCU_UART_TX | RP2350B debug UART TX | Near RP2350B |
| TP24 | MCU_UART_RX | RP2350B debug UART RX | Near RP2350B |
| TP25 | ANT_SEL0 | PE42422 select bit 0 | Near PE42422 |
| TP26 | ANT_SEL1 | PE42422 select bit 1 | Near PE42422 |

### Ground Test Points

| Reference | Net | Description | Location |
|-----------|-----|-------------|----------|
| TP27 | GND | Ground reference (power section) | Near PMIC |
| TP28 | GND | Ground reference (digital section) | Near RK3576 |
| TP29 | GND | Ground reference (RF section) | Near LMS7002M |
| TP30 | GND | Ground reference (analog section) | Near ST25R3916 |

### Debug Header (J11)

A 2×5 pin 0.1" header provides access to the RP2350B SWD debug interface:

| Pin | Signal | Pin | Signal |
|-----|--------|-----|--------|
| 1 | VCC_3V3_MCU | 2 | GND |
| 3 | SWDIO | 4 | SWCLK |
| 5 | RUNn | 6 | BOOTSEL |
| 7 | UART_TX | 8 | UART_RX |
| 9 | SPI0_IRQ | 10 | GND |

---

## Power Sequencing Margins

The PMIC (RK860-2) provides supervised power sequencing. All rails must be
stable within the following margins before the downstream device reset is
deasserted:

| Rail | Target | Margin | Rise Time | Settling |
|------|--------|--------|-----------|----------|
| VCC_1V0_CORE | 1.0 V | ±30 mV | < 5 ms | < 10 ms |
| VCC_1V8_SYS | 1.8 V | ±54 mV | < 5 ms | < 10 ms |
| VCC_3V3_SYS | 3.3 V | ±99 mV | < 10 ms | < 15 ms |
| VCC_3V3_MCU | 3.3 V | ±99 mV | < 5 ms | < 10 ms |
| VCC_3V3_SDR | 3.3 V | ±99 mV | < 10 ms | < 15 ms |
| VCC_1V8_SDR | 1.8 V | ±54 mV | < 5 ms | < 10 ms |
| VCC_1V2_SDR | 1.2 V | ±36 mV | < 5 ms | < 10 ms |

**Total power-up sequence**: approximately 50 ms from PMIC enable to all
resets deasserted and all subsystems ready for configuration.

### Decoupling Requirements

| Device | Bulk Cap | Decoupling | Per-Pin |
|--------|----------|-----------|---------|
| RK3576 | 100 µF (VCC_1V0) | 10 × 100 nF | 1 per VDD pin |
| RP2350B | 47 µF (VCC_3V3) | 4 × 100 nF | 1 per VDD pin |
| LMS7002M | 47 µF (VCC_3V3) | 6 × 100 nF | 1 per power pin |
| ST25R3916 | 10 µF (VCC_3V3) | 3 × 100 nF | 1 per VDD pin |
| CC1101 | 10 µF (VCC_3V3) | 2 × 100 nF | 1 per VDD pin |

All decoupling capacitors must be placed **within 2 mm** of the IC power
pin they serve, with via connections directly to the ground plane.

---

*Document version: 1.0 — 2026-06-18*