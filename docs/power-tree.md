# Power Tree & Sequencing — GhostBlade (Project NullSpectre)

This document describes the power architecture, voltage rail tree, and
power-on sequencing requirements for the GhostBlade dual-processor
pentesting device.

## Power Source

| Source | Voltage | Current | Connector | Notes |
|--------|---------|---------|-----------|-------|
| Li-Po battery | 3.7 V nominal (3.0–4.2 V) | 2C max | JST-PH 2-pin | 2000 mAh recommended |
| USB-C PD | 5–20 V (negotiated) | 3 A max | USB-C 24-pin | BC1.2 + PD 3.0 |
| External DC | 5 V via header | 2 A max | 2.54 mm header | Direct 5 V input |

## Voltage Rails

```
┌──────────────────────────────────────────────────────────────────┐
│                        POWER INPUT TREE                          │
│                                                                  │
│  VBAT (3.0–4.2V) ──┐                                            │
│                     ├──► RK817 PMIC ──► VCC_SYS (3.4V)           │
│  USB-C 5V ──────────┘           │                                │
│                                 ├──► VDD_LOGIC_1V8 (1.8V @ 1A)  │
│                                 │    RK3576 core, I/O banks       │
│                                 │                                │
│                                 ├──► VDD_CORE_0V9 (0.9V @ 2A)   │
│                                 │    RK3576 CPU + GPU cores        │
│                                 │                                │
│                                 ├──► VDD_DDR_1V1 (1.1V @ 1A)    │
│                                 │    LPDDR5 VDD2                 │
│                                 │                                │
│                                 ├──► VDDQ_DDR_0V6 (0.6V @ 0.5A) │
│                                 │    LPDDR5 VDDQ                 │
│                                 │                                │
│                                 └──► VCC_3V3 (3.3V @ 2A)        │
│                                      Peripherals, GPIO, sensors  │
│                                                                  │
│  VCC_3V3 ──┬──► VCC_SDIO (3.3V switched) ──► MT7922 Wi-Fi 6E   │
│            ├──► VCC_SPI0 (3.3V switched)  ──► RP2350B            │
│            ├──► VCC_SDR (3.3V switched)   ──► LMS7002M VDDIO   │
│            ├──► VCC_NFC (3.3V switched)    ──► ST25R3916        │
│            └──► VCC_SUBGHZ (3.3V switched) ──► CC1101          │
│                                                                  │
│  LMS7002M analog:                                               │
│    VCC_1V8_SDR ──► LMS7002M core (1.8V @ 500mA)                │
│    VCC_1V1_SDR ──► LMS7002M PLL  (1.1V @ 300mA)                │
│    VCC_3V3_RF  ──► LMS7002M PA/LNA (3.3V @ 200mA)               │
│                                                                  │
│  RP2350B:                                                       │
│    VCC_RP2350B ──► 1.1V core (internal LDO from 3.3V)           │
│    VCC_3V3_RP ──► RP2350B I/O (3.3V from VCC_3V3)              │
│                                                                  │
│  USB-C:                                                         │
│    VBUS_5V ──► MT7922 (USB mode) / BC1.2 charging               │
│                                                                  │
│  NVMe (via PCIe):                                               │
│    VCC_3V3_PCIE ──► M.2 2230 slot (3.3V @ 1A switched)          │
│    VCC_12V_PCIE ──► M.2 2230 slot (12V optional, not fitted)    │
└──────────────────────────────────────────────────────────────────┘
```

## Power Sequencing

The RK817 PMIC manages the complete power-on sequence. All timing
requirements are enforced by the PMIC's programmable sequencer.

### Power-On Sequence

```
Time    Rail                Condition                    Notes
────    ────                ─────────                    ─────
 0ms    VCC_SYS (3.4V)      VBAT > 3.0V or USB-C valid  Always-on domain
 2ms    VDD_LOGIC_1V8       After VCC_SYS > 3.3V        RK3576 I/O
 5ms    VDD_CORE_0V9       After VDD_LOGIC_1V8 stable   RK3576 CPU
 8ms    VDD_DDR_1V1        After VDD_CORE_0V9 stable     LPDDR5 VDD2
10ms    VDDQ_DDR_0V6       After VDD_DDR_1V1 stable      LPDDR5 VDDQ
15ms    VCC_3V3             After VDDQ_DDR_0V6 stable     Peripherals
20ms    VCC_SDIO            After VCC_3V3 stable          Wi-Fi module
20ms    VCC_SDR (1V8)       After VCC_3V3 stable          LMS7002M core
25ms    VCC_SDR (1V1)       After VCC_SDR 1V8 stable     LMS7002M PLL
30ms    VCC_SDR (3V3)       After VCC_SDR 1V1 stable     LMS7002M PA/LNA
50ms    VCC_3V3_RP          After VCC_3V3 stable          RP2350B I/O
60ms    MCU_RESET deassert  After VCC_3V3_RP stable       RP2350B boot begins
100ms   VCC_NFC             After RP2350B boot            ST25R3916
100ms   VCC_SUBGHZ          After RP2350B boot            CC1101
120ms   HOST_RDY assert     After MCU signals ready       RK3576 ↔ RP2350B
150ms   PCIe power           After RK3576 boot             NVMe
```

### Power-Off Sequence (Reverse Order)

```
Time    Rail                Notes
────    ────                ─────
 0ms    HOST_RDY deassert   Signal MCU to stop
 5ms    MCU_RESET assert    Hold RP2350B in reset
10ms    VCC_SUBGHZ off      CC1101 power down
10ms    VCC_NFC off         ST25R3916 power down
15ms    VCC_3V3_RP off      RP2350B I/O off
20ms    VCC_SDR (3V3) off   LMS7002M PA/LNA off
25ms    VCC_SDR (1V1) off   LMS7002M PLL off
30ms    VCC_SDR (1V8) off   LMS7002M core off
35ms    VCC_SDIO off        Wi-Fi off
40ms    VCC_3V3 off         Peripherals off
45ms    VDDQ_DDR off         LPDDR5 VDDQ off
50ms    VDD_DDR off          LPDDR5 VDD2 off
55ms    VDD_CORE off         RK3576 CPU off
60ms    VDD_LOGIC off         RK3576 I/O off
65ms    VCC_SYS off          PMIC shutdown complete
```

## Current Budget

| Rail | Voltage | Max Current | Source | Notes |
|------|---------|-------------|--------|-------|
| VCC_SYS | 3.4V | 3A | RK817 buck | System power |
| VDD_LOGIC | 1.8V | 1A | RK817 LDO1 | RK3576 I/O |
| VDD_CORE | 0.9V | 2A | RK817 buck2 | RK3576 CPU |
| VDD_DDR | 1.1V | 1A | RK817 buck3 | LPDDR5 |
| VDDQ_DDR | 0.6V | 500mA | RK817 LDO2 | LPDDR5 VDDQ |
| VCC_3V3 | 3.3V | 2A | RK817 LDO3 | Peripherals |
| VCC_3V3_RP | 3.3V | 300mA | VCC_3V3 | RP2350B I/O |
| VCC_SDR 1V8 | 1.8V | 500mA | VCC_3V3 → LDO | LMS7002M core |
| VCC_SDR 1V1 | 1.1V | 300mA | VCC_3V3 → LDO | LMS7002M PLL |
| VCC_SDR 3V3 | 3.3V | 200mA | VCC_3V3 switched | LMS7002M PA/LNA |
| VCC_SUBGHZ | 3.3V | 50mA | VCC_3V3 switched | CC1101 |
| VCC_NFC | 3.3V | 150mA | VCC_3V3 switched | ST25R3916 |
| VCC_SDIO | 3.3V | 500mA | VCC_3V3 switched | MT7922 SDIO |
| VCC_PCIE | 3.3V | 1A | VCC_3V3 switched | NVMe M.2 |

**Total estimated peak current (all rails):** ~5.5A at 3.4V = ~18.7W  
**Typical operating current (active pentesting):** ~2.8A at 3.4V = ~9.5W  
**Low-power standby:** ~5mA at 3.4V = ~17mW

## Power Modes

| Mode | Rails Active | Current | Use Case |
|------|-------------|---------|----------|
| Active | All | 2.8A | Full pentesting, SDR streaming |
| SDR-only | VCC_SYS, VDD_CORE, VDD_DDR, VCC_3V3, VCC_SDR | 1.5A | SDR capture only |
| Sub-GHz only | VCC_SYS, VDD_CORE, VDD_DDR, VCC_3V3, VCC_SUBGHZ | 300mA | Sub-GHz monitoring |
| NFC-only | VCC_SYS, VDD_CORE, VDD_DDR, VCC_3V3, VCC_NFC | 400mA | NFC read/write |
| Wi-Fi only | VCC_SYS, VDD_CORE, VDD_DDR, VCC_3V3, VCC_SDIO | 600mA | Wi-Fi scanning |
| Standby | VCC_SYS, VDD_LOGIC, VCC_3V3 (partial) | 5mA | Deep sleep, wake on GPIO |
| Off | None | 0 | Battery disconnected |

## ESD Protection

| Interface | ESD Protection | Part | Voltage Rating |
|-----------|---------------|------|---------------|
| USB-C | TPD4E05U06 | 4-channel TVS | 5.5V, 15kV IEC |
| SMA_ANT0 | TPD2E009 | 2-channel TVS | 6V, 15kV IEC |
| SMA_ANT1 | TPD2E009 | 2-channel TVS | 6V, 15kV IEC |
| NFC antenna | TPD2E009 | 2-channel TVS | 6V, 15kV IEC |
| u.FL sub-GHz | TPD1E10B06 | Single TVS | 6V, 30kV IEC |
| SDIO (Wi-Fi) | PRTR5V0U2X | 2-channel TVS | 5.5V |
| SPI0 (bridge) | TPD2E009 | 2-channel TVS | 6V, 15kV IEC |
| Debug UART | PRTR5V0U2X | 2-channel TVS | 5.5V |

## Reset Circuit

```
                    VCC_3V3
                      │
                    ┌─┴─┐
                    │   │ 10kΩ
                    │   │
                    └─┬─┘
                      │
    RK817 nRST ◄─────┤──────► RK3576 RESET (active-low)
                      │
                    ┌─┴─┐
                    │   │ 100Ω
                    │   │
                    └─┬─┘
                      │
                      ├──► RP2350B RUN (active-low, with 100nF cap to GND)
                      │
                    ┌─┴─┐
                    │   │ 10kΩ
                    │   │
                    └─┬─┘
                      │
                     GND

Reset timing:
  - nRST assertion: < 100 µs
  - nRST hold time: 10 ms minimum
  - nRST release to MCU_RUN: 50 ms (PMIC-controlled)
  - MCU_RUN deassert to HOST_RDY: 100 ms (RP2350B boot)
```

## Test Points

| TP# | Net | Purpose | Location |
|-----|-----|---------|----------|
| TP1 | VCC_SYS (3.4V) | System rail voltage | Near RK817 |
| TP2 | VDD_LOGIC (1.8V) | Logic rail | Near RK3576 |
| TP3 | VDD_CORE (0.9V) | CPU core rail | Near RK3576 |
| TP4 | VDD_DDR (1.1V) | DDR rail | Near LPDDR5 |
| TP5 | VDDQ_DDR (0.6V) | DDR VDDQ | Near LPDDR5 |
| TP6 | VCC_3V3 | 3.3V peripheral rail | Center of board |
| TP7 | VCC_3V3_RP | RP2350B I/O | Near RP2350B |
| TP8 | VCC_SDR_1V8 | LMS7002M core | Near LMS7002M |
| TP9 | VCC_SDR_1V1 | LMS7002M PLL | Near LMS7002M |
| TP10 | VCC_SDR_3V3 | LMS7002M PA/LNA | Near LMS7002M |
| TP11 | VCC_NFC | ST25R3916 | Near ST25R3916 |
| TP12 | VCC_SUBGHZ | CC1101 | Near CC1101 |
| TP13 | VBAT | Battery voltage | Near battery connector |
| TP14 | GND | Ground reference | Edge of board |
| TP15 | SPI0_SCK | SPI0 clock | Near RP2350B |
| TP16 | SPI0_MOSI | SPI0 data out | Near RP2350B |
| TP17 | INT_REQ | MCU interrupt | Near RP2350B |
| TP18 | HOST_RDY | Host ready signal | Near RP2350B |
| TP19 | MCU_RESET | MCU reset control | Near RP2350B |
| TP20 | VCC_SDIO | Wi-Fi SDIO power | Near MT7922 |