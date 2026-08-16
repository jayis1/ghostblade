# GhostBlade Reset Circuit Design
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

This document describes the reset circuits for all processors and peripherals
on the GhostBlade board, including power-on reset (POR), hardware reset,
software reset, and watchdog reset paths.

## 1. Reset Architecture Overview

```
                                    ┌──────────────────┐
                                    │                  │
                         ┌─────────┤   RK3576 SoC     │
                         │         │                  │
                    PORn │         │  RESETn ◄────────┤──── RK3576 RESET button
                         │         │                  │
                         │         │  GPIO1_B2 ───────┤──── MCU_RESET (to RP2350B)
                         │         │                  │
                         │         │  PMIC_INT ───────┤──── RK817 interrupt
                         │         │                  │
                         │         └──────────────────┘
                         │
                    ┌────┴─────┐
                    │  RK817   │
                    │  PMIC    │
                    │          │
   VBAT ────────────┤ VIN      │
                    │          │        ┌──────────────────┐
   PWRON_KEY ───────┤ PWRON    │        │                  │
                    │          ├─ VCC ──┤   RP2350B        │
                    │ RUN pin  │        │                  │
                    └──────────┘   3V3 ─┤  RUN ◄───────────── MCU_RESET
                                        │                  │
                                        │  WATCHDOG ───────┤──── Internal WDOG
                                        │                  │
                                        │  GPIO44 ◄───────┤──── ST25R3916 IRQ
                                        │                  │
                                        │  BOOTSEL ───────┤──── BOOTSEL button
                                        │                  │
                                        │  GPIO1_B1 ──────┤──── HOST_RDY
                                        │                  │
                                        └──────────────────┘
```

## 2. RK3576 Reset Circuit

### 2.1 Power-On Reset (POR)

The RK3576 power-on reset is managed by the RK817 PMIC:

```
  VBAT ──────┬───── RK817 VIN
             │
  PWRON_KEY ┤───── RK817 PWRON (active-low, 6s force-off)
             │
             └───── RK817 PORn ────── RK3576 PORn
                                          │
                                     Internal pull-up: 100kΩ to VDD_LOGIC
                                     External capacitor: 100nF to GND
                                     (ensures >10ms reset pulse)
```

**Reset timing:**
- PORn is held low until VDD_LOGIC (1.8V) and VDD_CORE (0.9V) are stable
- Minimum PORn pulse width: 10 ms
- PMIC holds PORn low for: ~50 ms after all rails stable

### 2.2 Hardware Reset Button

A dedicated RESET button (SW2) on the board:

```
  VDD_LOGIC (1.8V)
       │
    [10kΩ] R5
       │
       ├──── RK3576 RESETn ───────────── RK3576
       │
    [SW2] RESET button (active-low)
       │
      GND

  ESD protection: TPD4E05U06 (clamp to 5.5V)
  Debounce: 100nF capacitor C12 across SW2
  Pull-up: 10kΩ R5 to VDD_LOGIC
```

### 2.3 Software Reset Sources

| Source | Signal | Active | Duration | Effect |
|--------|--------|--------|----------|--------|
| PMIC PWRON | RK817 PWRON | LOW | >6s = force-off | Full power cycle |
| PMIC PWRON | RK817 PWRON | LOW | >1s = reset | SoC reset |
| WDT (RK3576) | Internal | — | 10s timeout | SoC reset |
| Software reset | CRU registers | — | — | SoC reset |

## 3. RP2350B Reset Circuit

### 3.1 MCU_RESET from RK3576

The RK3576 controls the RP2350B reset via GPIO1_B2:

```
  VDD_LOGIC (1.8V)
       │
    [10kΩ] R12
       │
       ├──── RP2350B RUN pin
       │
  RK3576 GPIO1_B2 ────── 74LVC1G04 (inverting buffer)
                              │
                          ┌───┴───┐
                          │       │
                          │  BSS138│  (level shifter, optional)
                          │       │
                          └───┬───┘
                              │
                          RP2350B RUN pin (3.3V domain)

  Signal name: MCU_RESET
  Active: LOW (asserted = RP2350B in reset)
  Minimum pulse width: 1 µs (RP2350B spec)
  Typical pulse width: 10 µs (firmware update path)
```

**Level shifting**: GPIO1_B2 is 1.8V logic from RK3576. The RP2350B RUN pin
is 3.3V logic. A 74LVC1G04 inverting buffer with 3.3V VCC provides both
level shifting and inversion (RUN pin is active-high, GPIO1_B2 is
programmed as active-low). A BSS138 N-MOSFET alternative is also viable.

### 3.2 RP2350B Bootsel

```
  VDD_3V3 (3.3V)
       │
    [10kΩ] R13
       │
       ├──── RP2350B BOOTSEL pin (GPIO0)
       │
  [SW3] BOOTSEL button (active-low)
       │
      GND

  Debounce: 100nF capacitor C15 across SW3
  Pull-up: 10kΩ R13 to VDD_3V3
  Function: When held at boot, forces USB boot mode
```

### 3.3 RP2350B Watchdog Reset

The RP2350B has an internal hardware watchdog (PSL watchdog):

```
  Watchdog configuration:
    - Timeout: 5000 ms (5 seconds)
    - Action: System reset (not just core reset)
    - Kick interval: Every main loop iteration (~1ms)
    - Scratch register: Used to pass reset reason to firmware

  Reset reason detection (in firmware main.c):
    - Watchdog scratch == 0xDEADBEEF: Brownout recovery
    - Watchdog scratch == 0xCAFE0001: Software-initiated reboot
    - Watchdog scratch == 0x00000000: Normal watchdog timeout
```

### 3.4 RP2350B Reset Sequence from RK3576

```
  Step  Action                                GPIO State
  ────  ───────────────────────────────────  ──────────────
  1     HOST_RDY deasserted                   GPIO1_B1 → LOW
  2     MCU_RESET asserted (LOW)              GPIO1_B2 → LOW
  3     Wait ≥ 10 µs                          —
  4     MCU_RESET deasserted (HIGH)           GPIO1_B2 → HIGH
  5     Wait ~200ms for RP2350B boot          —
  6     HOST_RDY asserted                     GPIO1_B1 → HIGH
  7     SPI handshake complete                —
```

## 4. LMS7002M SDR Reset

```
  VDD_3V3 (3.3V)
       │
    [10kΩ] R20
       │
       ├──── LMS7002M RESETn pin
       │
  RP2350B GPIO21 (SDR_RESET) ────── LMS7002M RESETn
                                        │
                                    [100nF C30] to GND (debounce)

  Active: LOW (asserted = LMS7002M in reset)
  Minimum pulse width: 1 µs
  Reset release: SDR_3V3 must be stable before RESETn is deasserted
```

**Reset timing constraints:**
1. All three SDR rails (1V8, 1V1, 3V3) must be stable before RESETn is released
2. After RESETn deassertion, wait ≥ 100 µs before first SPI access
3. LMS7002M requires full register reconfiguration after hardware reset

## 5. CC1101 Sub-GHz Radio Reset

The CC1101 uses an SPI command strobe (SRES) for software reset rather
than a hardware reset pin:

```
  CC1101 reset sequence (RP2350B firmware):
    1. Assert CC1101 power rail (GPIO23 → HIGH)
    2. Wait ≥ 50 ms for crystal oscillator startup
    3. Send SRES strobe via SPI1
    4. Wait ≥ 50 µs for calibration
    5. Verify CHIPSTATUS register = 0x01 (idle)
    6. Write configuration registers

  No hardware reset pin — the CC1101 GDO0 pin is used for:
    - FIFO threshold signaling
    - Packet transmission/reception status
    - Clear channel assessment
```

## 6. ST25R3916 NFC Controller Reset

The ST25R3916 uses power-on reset (VDD_NFC ramp) combined with the
SPI `SET_DEFAULT` command for soft reset. There is no dedicated reset
GPIO from the RP2350B — the NFC power rail (POWER_RAIL_NFC, GPIO 11)
controls the power cycle.

```
  VDD_NFC (5V, switched via POWER_RAIL_NFC GPIO 11)
       │
    [10kΩ] R25
       │
       ├──── ST25R3916 RSTn pin (tied high via pull-up)
       │
  [100nF C35] to GND (debounce)

  Power-on reset: VDD_NFC ramp triggers internal POR
  Soft reset:     SPI SET_DEFAULT command (0x18) clears all registers
  Power cycle:    RP2350B toggles POWER_RAIL_NFC (GPIO 11) off/on
                  with 50ms ramp delay (POWER_DELAY_NFC_MS)

  Reset release: VDD_NFC must be stable before SPI communication
  Minimum power-off time for full reset: 10 ms
```

**ST25R3916 reset sequence (RP2350B firmware):**
```
  Step  Action                                Timing
  ────  ───────────────────────────────────  ──────────
  1     NFC_RESETn asserted (LOW)            t=0
  2     Wait ≥ 10 µs                          t=10µs
  3     NFC_RESETn deasserted (HIGH)          t=10µs+
  4     Wait for oscillator stable             t=5ms
  5     Send SET_DEFAULT command               t=5ms+
  6     Configure registers                    t=6ms+
  7     Enable interrupt mask                  t=10ms+
  8     Start polling                           t=15ms+
```

## 7. MT7922 Wi-Fi Reset

The MT7922 Wi-Fi module is reset via the SDIO bus (software reset through
the MMC subsystem). There is no dedicated hardware reset GPIO:

```
  MT7922 reset sequence (RK3576 Linux kernel):
    1. Send SDIO CMD52 reset command
    2. Wait 100 ms
    3. Re-initialize SDIO bus
    4. Load firmware (mt7922.bin)
    5. Wait for firmware ready interrupt
```

## 8. Reset Priority and Interaction

When multiple resets occur simultaneously, the following priority order
applies:

| Priority | Reset Source | Scope | Recovery |
|----------|-------------|-------|----------|
| 1 (highest) | VBAT brownout (< 2.8V) | All | Full power cycle required |
| 2 | PMIC PWRON (6s hold) | All | Full power cycle |
| 3 | RK3576 watchdog (10s) | RK3576 | Kernel panic + reboot |
| 4 | RK3576 software reset | RK3576 | Clean reboot |
| 5 | MCU_RESET (from RK3576) | RP2350B | Firmware reboot |
| 6 | RP2350B watchdog (5s) | RP2350B | Firmware reboot |
| 7 | LMS7002M RESETn | LMS7002M | Register reconfiguration |
| 8 | ST25R3916 RSTn | ST25R3916 | Register reconfiguration |
| 9 | CC1101 SRES | CC1101 | Register reconfiguration |
| 10 (lowest) | MT7922 SDIO reset | MT7922 | Firmware reload |

## 9. Reset State Machine

```
                  ┌─────────────────────────────────┐
                  │           POWER OFF              │
                  │  (VBAT < 2.8V or PMIC disabled)  │
                  └──────────────┬───────────────────┘
                                 │ VBAT > 2.8V or USB-C connected
                                 │
                  ┌──────────────▼───────────────────┐
                  │           POWER ON               │
                  │  (PMIC rails ramping)             │
                  └──────────────┬───────────────────┘
                                 │ All rails stable
                                 │
                  ┌──────────────▼───────────────────┐
                  │           BOOTING                │
                  │  RK3576: SPL → U-Boot → Linux     │
                  │  RP2350B: boot ROM → firmware     │
                  └──────┬──────────┬────────────────┘
                         │          │
          ┌──────────────┘    ┌─────┘
          │                    │
  ┌───────▼────────┐   ┌──────▼───────┐
  │  RK3576 READY  │   │ RP2350B READY│
  │  (Linux booted)│   │ (firmware up)│
  └───────┬────────┘   └──────┬───────┘
          │                    │
          └───────┬────────────┘
                  │ SPI handshake
                  │
  ┌───────────────▼───────────────────┐
  │         OPERATIONAL               │
  │  (all subsystems running)         │
  └───┬──────────┬──────────┬────────┘
      │          │          │
      │    MCU_RESET       RK3576 WDT
      │   (from host)     (10s timeout)
      │          │          │
      │    ┌─────▼────┐  ┌──▼─────────┐
      │    │RP2350B    │  │RK3576      │
      │    │REBOOT     │  │REBOOT      │
      │    │(watchdog  │  │(kernel     │
      │    │ or host)  │  │ panic)     │
      │    └─────┬────┘  └──┬─────────┘
      │          │          │
      │    ┌─────▼──────────▼─────┐
      │    │  OPERATIONAL          │
      │    │  (restored)           │
      │    └───────────────────────┘
      │
  BROWNOUT (< 2.8V)
      │
  ┌───▼────────────┐
  │ EMERGENCY OFF  │
  │ (all rails off) │
  └────────────────┘
```

## 10. ESD Protection on Reset Lines

All reset lines have ESD protection and proper termination:

| Signal | ESD Device | Series Resistor | Pull-up | Capacitor |
|--------|-----------|----------------|---------|-----------|
| RK3576 RESETn | TPD4E05U06 | 100Ω (R4) | 10kΩ (R5) | 100nF (C12) |
| MCU_RESET | TPD4E05U06 | 100Ω (R10) | 10kΩ (R12) | 100nF (C18) |
| SDR_RESETn | TPD4E05U06 | 100Ω (R19) | 10kΩ (R20) | 100nF (C30) |
| NFC_RESETn | TPD4E05U06 | 100Ω (R24) | 10kΩ (R25) | 100nF (C35) |
| BOOTSEL | TPD4E05U06 | 100Ω (R14) | 10kΩ (R13) | 100nF (C15) |

Series resistors:
- Limit ESD current through the protection device
- Provide RC filtering with the capacitor for switch debouncing
- 100Ω is sufficient to limit current while not significantly slowing edges

Pull-up resistors:
- Ensure reset lines are in the inactive (HIGH) state during power-up
- 10kΩ is strong enough to hold the line high but weak enough to allow
  active driving by the controlling GPIO