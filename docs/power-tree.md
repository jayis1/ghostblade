# GhostBlade Power Tree Diagram

This document describes the power distribution architecture for the GhostBlade (Project NullSpectre) board.

## Power Tree Overview

```
                                    ┌──────────────────┐
                                    │   DC Input        │
                                    │   USB-C PD 5-20V  │
                                    │   or Li-Po 3.7V   │
                                    └────────┬──────────┘
                                             │
                                    ┌────────┴──────────┐
                                    │   RK817 PMIC      │
                                    │   (Power Mgmt IC) │
                                    └──┬─────┬─────┬────┘
                                       │     │     │
                          ┌────────────┘     │     └────────────┐
                          │                  │                  │
                   ┌──────┴──────┐    ┌──────┴──────┐   ┌──────┴──────┐
                   │ VCC_3V3     │    │ VCC_1V8     │   │ VCC_5V      │
                   │ (Main 3.3V) │    │ (Core 1.8V) │   │ (USB/NFC)    │
                   │ 3.3V @ 3A   │    │ 1.8V @ 2A   │   │ 5V @ 2A      │
                   └──┬───┬───┬──┘    └──────┬──────┘   └──────┬──────┘
                      │   │   │              │                  │
          ┌──────────┘   │   └──────┐       │         ┌────────┘
          │              │          │       │         │
   ┌──────┴──────┐  ┌────┴────┐  ┌──┴──────┐│  ┌──────┴──────┐
   │VCC_3V3_RP   │  │VCC_SDIO │  │VCC_SDR  ││  │VCC_5V_NFC   │
   │(RP2350B I/O)│  │(MT7922  │  │1V8      ││  │(ST25R3916)  │
   │GPIO 3.3V    │  │Wi-Fi)   │  │(LMS7002M││  │via level     │
   │@ 500mA      │  │@ 500mA  │  │ core)   ││  │shifter)      │
   └──────┬──────┘  └────┬────┘  │@ 500mA  ││  │@ 150mA       │
          │              │       └────┬─────┘│  └──────┬──────┘
          │              │            │      │         │
   ┌──────┴──────┐      │     ┌──────┴──┐   │  ┌──────┴──────┐
   │ RP2350B     │      │     │VCC_SDR  │   │  │ST25R3916    │
   │ (MCU Core)  │      │     │1V1      │   │  │(NFC Reader) │
   │ 3.3V I/O    │      │     │(LMS7002M│   │  │5V antenna   │
   │ 1.8V core   │      │     │ PLL)    │   │  │driver        │
   └──────┬──────┘      │     │@ 300mA  │   │  └──────────────┘
          │             │     └────┬─────┘   │
   ┌──────┴──────┐      │          │        │
   │VCC_SDR 3V3  │      │     ┌────┴─────┐  │
   │(LMS7002M    │      │     │VCC_SDR   │  │
   │ I/O, PA)    │      │     │1V1       │  │
   │@ 200mA      │      │     │(LMS7002M │  │
   └──────┬──────┘      │     │ PLL)     │  │
          │             │     │@ 300mA   │  │
   ┌──────┴──────┐      │     └──────────┘  │
   │LMS7002M     │      │                    │
   │(SDR)        │      │                    │
   │3.3V I/O     │      │                    │
   └─────────────┘      │                    │
                         │                    │
   ┌─────────────────────┴────────────────────┘
   │
   │    ┌──────────────────┐
   │    │VCC_SUBGHZ        │
   │    │(CC1101 Sub-GHz)  │
   │    │3.3V @ 50mA       │
   │    └──────┬───────────┘
   │           │
   │    ┌──────┴───────────┐
   │    │CC1101             │
   │    │(Sub-GHz Radio)   │
   │    └──────────────────┘
   │
   └──────────────────────────────────────────
```

## Power Rail Summary

| Rail | Voltage | Max Current | Controlled By | Enable GPIO | Purpose |
|------|---------|-------------|---------------|-------------|---------|
| VCC_3V3 | 3.3V | 3A | RK817 PMIC | Always-on | Main system power |
| VCC_1V8 | 1.8V | 2A | RK817 PMIC | Always-on | RK3576 core, RP2350B core |
| VCC_5V | 5.0V | 2A | RK817 PMIC | Always-on | USB, NFC antenna driver |
| VCC_3V3_RP | 3.3V | 500mA | RP2350B GPIO 6 | PWR_GPIO_SDR_1V8* | RP2350B I/O domain |
| VCC_SDR 1V8 | 1.8V | 500mA | RP2350B GPIO 6 | PWR_GPIO_SDR_1V8 | LMS7002M core |
| VCC_SDR 1V1 | 1.1V | 300mA | RP2350B GPIO 7 | PWR_GPIO_SDR_1V1 | LMS7002M PLL |
| VCC_SDR 3V3 | 3.3V | 200mA | RP2350B GPIO 9 | PWR_GPIO_SDR_3V3 | LMS7002M I/O, PA |
| VCC_5V_NFC | 5.0V | 150mA | RP2350B GPIO 11 | PWR_GPIO_NFC | ST25R3916 antenna driver |
| VCC_SUBGHZ | 3.3V | 50mA | RP2350B GPIO 22 | PWR_GPIO_SUBGHZ | CC1101 sub-GHz radio |
| VCC_SDIO | 3.3V | 500mA | RP2350B GPIO 25 | PWR_GPIO_SDIO | MT7922 Wi-Fi 6E |

\* Note: VCC_3V3_RP is powered from VCC_3V3 through a load switch controlled by RP2350B GPIO.

## Power Sequencing

### Power-On Sequence

```
Time (ms)  0     5     10    15    20    55    105   125   135
           │     │     │     │     │     │     │     │     │
VCC_3V3    ├─────┤████████████████████████████████████████████│
           ramp  stable
VCC_1V8          ├─────┤████████████████████████████████████████│
                 ramp  stable
VCC_SDR 1V8           ├─────┤████████████████████████████│
                       ramp  stable
VCC_SDR 1V1                 ├─────┤████████████████████│
                             ramp  stable
VCC_SDR 3V3                       ├─────┤██████████████│
                                   ramp  stable
VCC_5V_NFC                              ├──────────┤████│
                                        ramp (50ms) stable
VCC_SUBGHZ                              ├──────────┤████│
                                        ramp (50ms) stable
VCC_SDIO                                       ├──┤████│
                                               ramp stable
```

### Power-Off Sequence (Reverse Order)

```
1. VCC_SDIO off           → 5ms delay
2. VCC_SUBGHZ off         → 5ms delay
3. VCC_5V_NFC off         → 5ms delay
4. VCC_SDR 3V3 off        → 5ms delay
5. VCC_SDR 1V1 off        → 5ms delay
6. VCC_SDR 1V8 off        → no delay (last rail)
```

## Reset Circuit

```
                    VCC_3V3
                      │
                    ┌─┴─┐
                    │10kΩ│ R1 (pull-up)
                    └─┬─┘
                      │
          ┌───────────┤
          │           │
     ┌────┴────┐   ┌──┴──┐
     │RK817    │   │100nF│ C1 (filter cap)
     │nRSTO    │   └──┬──┘
     │(open-dr)│      │
     └────┬────┘      │
          │        ┌──┴──┐
          │        │GND  │
          │        └─────┘
          │
     ┌────┴─────────────────────────────────┐
     │                                      │
     │  RK3576 nRESET ◄────────────────────┤
     │                                      │
     │  RP2350B RUN (PIN_MCU_RUN) ◄───────┤──── From RK3576 GPIO1_B2
     │                                      │    (active-low reset)
     │  LMS7002M RESET ◄──────────────────┤──── From RP2350B PIN_SDR_RESET
     │                                      │    (active-low reset)
     │  CC1101 RESET ◄─────────────────────┤──── Via SPI command (SRES strobe)
     │                                      │
     │  ST25R3916 RESET ◄──────────────────┤──── Via SPI command (SET_DEFAULT)
     │                                      │
     └──────────────────────────────────────┘
```

### Reset Timing

| Signal | Active | Min Pulse | Deassert to Ready |
|--------|--------|-----------|-------------------|
| RK3576 nRESET | Low | 10 ms | ~500 ms (boot) |
| RP2350B RUN | Low | 1 µs | ~5 ms (boot) |
| LMS7002M RESET | Low | 10 µs | ~1 ms (PLL lock) |
| CC1101 SRES | N/A | N/A | ~1.5 ms (chip reset) |
| ST25R3916 SET_DEFAULT | N/A | N/A | ~5 ms (oscillator) |

## ESD Protection

All external-facing signals have ESD protection:

| Interface | ESD Device | Part Number | Voltage Rating |
|-----------|------------|-------------|----------------|
| USB-C PD | TVS diode array | TPD4E05U06 | ±8 kV HBM |
| SMA antennas | ESD diode | PRTR5V0U2X | ±8 kV HBM |
| u.FL antenna | ESD diode | PRTR5V0U2X | ±8 kV HBM |
| NFC antenna | TVS array | TPD4E05U06 | ±8 kV HBM |
| GPIO headers | TVS array | TPD4E05U06 | ±8 kV HBM |

## Test Points

The GhostBlade PCB includes the following test points for debugging and manufacturing verification:

| Test Point | Net | Location | Purpose |
|-----------|-----|----------|---------|
| TP1 | VCC_3V3 | Near PMIC | Verify 3.3V rail voltage |
| TP2 | VCC_1V8 | Near PMIC | Verify 1.8V rail voltage |
| TP3 | VCC_5V | Near PMIC | Verify 5V rail voltage |
| TP4 | VCC_SDR_1V8 | Near LMS7002M | Verify SDR 1.8V rail |
| TP5 | VCC_SDR_1V1 | Near LMS7002M | Verify SDR 1.1V PLL |
| TP6 | VCC_SDR_3V3 | Near LMS7002M | Verify SDR 3.3V I/O |
| TP7 | VCC_5V_NFC | Near ST25R3916 | Verify NFC 5V supply |
| TP8 | VCC_SUBGHZ | Near CC1101 | Verify sub-GHz 3.3V |
| TP9 | GND | Board corner | Ground reference |
| TP10 | SPI0_SCK | Near RP2350B | SPI0 clock monitoring |
| TP11 | SPI0_CSn | Near RP2350B | SPI0 chip select |
| TP12 | SPI0_MOSI | Near RP2350B | SPI0 MOSI data |
| TP13 | SPI0_MISO | Near RP2350B | SPI0 MISO data |
| TP14 | INT_REQ | Near RP2350B | MCU interrupt line |
| TP15 | MCU_RUN | Near RP2350B | MCU reset control |
| TP16 | SDR_RESET | Near LMS7002M | SDR reset line |
| TP17 | ANT_SEL0 | Near PE42422 | Antenna switch bit 0 |
| TP18 | ANT_SEL1 | Near PE42422 | Antenna switch bit 1 |
| TP19 | VBAT_SENSE | Near RP2350B | Battery voltage sense |
| TP20 | CC1101_GDO0 | Near CC1101 | CC1101 GPIO0 status |
| TP21 | NFC_IRQ | Near ST25R3916 | NFC interrupt line |

---

*See `firmware/rp2350b/src/peripheral_power.c` for the power sequencing implementation.*