# GhostBlade Power Sequencing and Timing

This document describes the power-on sequencing, voltage rail dependencies,
and timing constraints for the GhostBlade (Project NullSpectre) hardware.

## Power Architecture Overview

The GhostBlade board uses a multi-rail power architecture with the following
voltage domains:

```
                    ┌──────────────────────────────────────────────┐
                    │              VBAT (3.7–4.2 V LiPo)          │
                    └───────────────────┬──────────────────────────┘
                                        │
                    ┌───────────────────┼──────────────────────────┐
                    │                   │                          │
              ┌─────┴─────┐     ┌───────┴───────┐          ┌──────┴─────┐
              │  VDD_CORE  │     │   VDD_3V3      │          │  VDD_RF     │
              │  0.9V/2A   │     │   3.3V/3A      │          │  3.3V/500mA │
              │  (RK3576)  │     │   (I/O, MCU,   │          │  (LMS7002M, │
              │            │     │    peripherals) │          │   CC1101)   │
              └────────────┘     └───────┬────────┘          └────────────┘
                                        │
                        ┌───────────────┼───────────────┐
                        │               │               │
                  ┌─────┴─────┐  ┌──────┴──────┐  ┌─────┴──────┐
                  │ VDD_LOGIC │  │ VDD_NFC_TX   │  │ VDD_1V8_SDR│
                  │ 1.8V/2A   │  │ 5.0V/300mA   │  │ 1.8V/200mA │
                  │ (SDIO,    │  │ (ST25R3916   │  │ (LMS7002M  │
                  │  eMMC,    │  │  antenna     │  │  DVDD)     │
                  │  DDR5)    │  │  driver)     │  │            │
                  └───────────┘  └──────────────┘  └────────────┘
                                                             │
                                                      ┌──────┴──────┐
                                                      │ VDD_1V2_SDR │
                                                      │ 1.2V/300mA  │
                                                      │ (LMS7002M   │
                                                      │  AVDD)      │
                                                      └─────────────┘
```

## Power-On Sequence

The RK3576 PMIC controls the power-on sequence. All rails must come up
in a specific order to prevent latch-up and ensure proper reset timing.

### Timing Diagram

```
Time (ms)   0     2     5     10    20    30    50    100   200   500
            │     │     │     │     │     │     │     │     │     │
VDD_CORE    ┃████████████████████████████████████████████████████████
(0.9V)      ┃◄─200µs─► stable
            │
VDD_LOGIC         ┃█████████████████████████████████████████████████
(1.8V)            ┃◄─500µs─► stable
                  │
VDD_3V3                 ┃████████████████████████████████████████████
(3.3V)                   ┃◄─1ms─► stable
                         │
VDD_RF                         ┃████████████████████████████████████
(3.3V)                         ┃◄─3ms─► stable
(off-on delay: 5ms min)       │
VDD_1V8_SDR                          ┃████████████████████████████
(1.8V)                                ┃◄─1.5ms─► stable
                                      │
VDD_1V2_SDR                                ┃████████████████████████
(1.2V)                                      ┃◄─2ms─► stable
                                            │
VDD_NFC_TX                                        ┃██████████████████
(5.0V)                                             ┃◄─5ms─► stable
(off-on delay: 10ms min)                          │
                                                   │
MCU_RESET  ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▃████████████████████
(asserted)                                            ┃
                                                      │
                                                      ┃── MCU firmware
                                                          starts (~50ms)
```

### Sequence Details

| Step | Rail          | Voltage | Delay After Previous | Min Ramp Time | Notes                          |
|------|---------------|---------|---------------------|---------------|--------------------------------|
| 1    | VDD_CORE      | 0.9 V   | 0 ms (first)        | 200 µs        | RK3576 core supply             |
| 2    | VDD_LOGIC     | 1.8 V   | 2 ms                | 500 µs        | I/O, eMMC, DDR5, SDIO         |
| 3    | VDD_3V3       | 3.3 V   | 5 ms                | 1 ms          | MCU, peripherals, GPIO         |
| 4    | VDD_RF        | 3.3 V   | 10 ms               | 3 ms          | LMS7002M, CC1101 RF supply     |
| 5    | VDD_1V8_SDR   | 1.8 V   | 20 ms               | 1.5 ms        | LMS7002M digital (DVDD)        |
| 6    | VDD_1V2_SDR   | 1.2 V   | 30 ms               | 2 ms          | LMS7002M analog (AVDD)         |
| 7    | VDD_NFC_TX    | 5.0 V   | 50 ms               | 5 ms          | ST25R3916 antenna driver       |
| 8    | MCU_RESET     | —       | 100 ms              | —             | Release RP2350B from reset     |

### Critical Timing Constraints

1. **VDD_CORE must be first** — The RK3576 requires core voltage before
   any I/O voltage to prevent CMOS latch-up.

2. **VDD_3V3 before MCU_RESET release** — The RP2350B I/O runs at 3.3V.
   Releasing MCU_RESET before VDD_3V3 is stable can cause bus contention
   on the SPI0 bridge.

3. **VDD_1V2_SDR after VDD_1V8_SDR** — The LMS7002M requires DVDD (1.8V)
   before AVDD (1.2V) to ensure proper internal bias sequencing.

4. **VDD_RF off-on delay: 5 ms minimum** — The RF LDO has a 5 ms minimum
   off-time before it can be re-enabled. This prevents inrush current
   overshoot on the antenna supply.

5. **VDD_NFC_TX off-on delay: 10 ms minimum** — The ST25R3916 antenna
   driver has a 10 ms minimum off-time due to the bulk output capacitors
   requiring discharge time.

6. **MCU_RESET must remain asserted for ≥100 ms** after VDD_3V3 is stable
   — The RP2350B requires power-on reset time plus crystal oscillator
   startup (~30 ms) before it can respond to SPI transactions.

## Power-Off Sequence

The power-off sequence is the reverse of power-on:

1. MCU_RESET asserted (RP2350B held in reset)
2. Wait 10 ms for SPI transactions to complete
3. VDD_NFC_TX disabled
4. Wait 5 ms (NFC output capacitors discharge)
5. VDD_1V2_SDR disabled
6. VDD_1V8_SDR disabled
7. VDD_RF disabled
8. VDD_3V3 disabled
9. VDD_LOGIC disabled
10. VDD_CORE disabled

### Emergency Shutdown (Brownout)

When the battery monitor detects VBAT < 3.0V (critical threshold):

1. MCU_RESET asserted immediately (halts SPI bridge)
2. All peripheral rails disabled in parallel
3. VDD_CORE retained until RK3576 enters self-refresh
4. After 50 ms, VDD_CORE disabled

## Reset Architecture

### RK3576 Reset Sources

| Reset Source    | Trigger                         | Effect                        |
|-----------------|---------------------------------|-------------------------------|
| Power-on reset  | VDD_CORE ramp above 0.85V      | Full SoC reset                |
| Watchdog reset   | WDT timeout (10 s default)     | SoC warm reset, PMIC retains  |
| Software reset   | `reboot` command               | Graceful shutdown + reset     |
| Thermal reset    | SOC_EMERGENCY trip (105°C)     | Immediate shutdown + reset    |

### RP2350B Reset Sources

| Reset Source      | Trigger                          | Effect                        |
|-------------------|----------------------------------|-------------------------------|
| Power-on reset    | VDD_3V3 ramp above 2.7V         | Full MCU reset                |
| Hardware reset    | MCU_RESET GPIO asserted         | Full MCU reset, watchdog scratch preserved |
| Software reset    | CMD_RESET_MCU (magic 0x52534554)| Watchdog timer reset, scratch[0] = 0x48525354 |
| Watchdog timeout  | 5 s firmware hang                | Full MCU reset, scratch[0] = 0x00000000 |

### Software Reset Flow (CMD_RESET_MCU)

The software reset uses a two-stage magic value validation to prevent
accidental resets from corrupted SPI frames:

```
Host (RK3576)                              MCU (RP2350B)
    │                                            │
    │─── CMD_RESET_MCU ────────────────────────►│
    │    (payload = 0x52534554 "RSET")          │
    │                                            │
    │                                   ┌────────┴────────┐
    │                                   │ Validate magic:   │
    │                                   │ payload == RSET?  │
    │                                   └──┬────────────┬───┘
    │                                      │ Yes        │ No
    │                                      ▼            ▼
    │                               Set scratch[0]    Increment
    │                               = 0x48525354      cmd_unknown_rx
    │                               ("HRST")          stats counter
    │                                      │
    │                               watchdog_reboot()
    │                                      │
    │                               MCU enters reset
    │                                      │
    │◄───── SPI bus idle ──────────────────┘
    │
    │   (~200 ms for MCU reboot + init)
    │
    │◄─── MCU_READY asserted ──────────────┘
    │
```

## Power State Machine

The RP2350B firmware implements a power state machine controlled by the
battery monitor:

```
                         VBAT > 3.7V
        ┌────────────────────────────────────────┐
        │                                        │
        ▼                                        │
   ┌─────────┐   VBAT < 3.3V   ┌──────────┐    │
   │  ACTIVE  │──────────────►│   LOW_BAT  │────┘
   │ (full)   │◄──────────────│  (reduced) │   VBAT > 3.5V
   └─────────┘   VBAT > 3.5V  └──────────┘
        │                           │
        │ VBAT < 3.0V              │ VBAT < 3.0V
        │ (brownout)               │ (brownout)
        ▼                           ▼
   ┌──────────┐               ┌──────────┐
   │ CRITICAL  │               │ CRITICAL  │
   │ (minimal)│               │ (minimal) │
   └──────────┘               └──────────┘
        │                           │
        │ VBAT < 2.7V              │ VBAT < 2.7V
        ▼                           ▼
   ┌──────────┐               ┌──────────┐
   │ SHUTDOWN  │               │ SHUTDOWN  │
   │ (powered │               │ (powered  │
   │  off)    │               │  off)     │
   └──────────┘               └──────────┘
```

| State       | SDR   | CC1101 | NFC   | Wi-Fi  | CPU Freq | Notes                    |
|-------------|-------|--------|-------|--------|----------|--------------------------|
| ACTIVE      | Full  | Full   | Full  | Full   | 150 MHz  | All peripherals active    |
| LOW_BAT     | RX    | RX     | Poll  | Down   | 100 MHz  | Reduced TX, NFC polling   |
| CRITICAL    | Off   | Off    | Off   | Off    | 50 MHz   | SPI bridge only           |
| SHUTDOWN    | Off   | Off    | Off   | Off    | halted   | MCU reset, minimal retention |

## SDR Power Sequencing

The LMS7002M requires a specific power-up sequence to prevent damage:

```
Step 1: VDD_1V8_SDR (DVDD) enabled    ──► LMS7002M digital I/O powered
Step 2: Wait ≥2 ms
Step 3: VDD_1V2_SDR (AVDD) enabled     ──► LMS7002M analog powered
Step 4: Wait ≥5 ms for PLL calibration
Step 5: SPI1 reset command (0x0000)    ──► Confirms SPI link active
Step 6: LMS7002M initialization via RP2350B
Step 7: Enable RX or TX path as needed
```

The SDR power rails must NOT be toggled rapidly. The minimum off-on
delay for VDD_1V2_SDR is 5 ms.

## NFC Power Sequencing

The ST25R3916 antenna driver requires careful power management:

```
Step 1: VDD_3V3 already stable (prerequisite)
Step 2: VDD_NFC_TX (5V) enabled
Step 3: Wait ≥10 ms for output capacitors to charge
Step 4: ST25R3916 I2C reset command via RP2350B
Step 5: ST25R3916 initialization sequence
Step 6: Enable RF field (analog config)
```

The VDD_NFC_TX off-on delay of 10 ms is a hardware constraint imposed
by the bulk output capacitors (4.7 µF + 100 nF) on the antenna driver.

## Thermal Protection

The RK3576 thermal management uses trip points defined in the device tree:

| Trip Point     | Temperature | Action                                    |
|----------------|-------------|-------------------------------------------|
| soc_alert      | 75°C        | Passive cooling (reduce CPU frequency)    |
| soc_crit       | 85°C        | Aggressive throttling                     |
| soc_emergency  | 105°C       | Emergency shutdown                        |

The RP2350B firmware monitors its die temperature and reports it via
the telemetry interface (temp_c_x10 field). The kernel driver's
brownout detection also monitors MCU temperature.

## Related Documentation

- [`spi-protocol-timing.md`](spi-protocol-timing.md) — SPI protocol timing diagrams
- [`architecture.md`](architecture.md) — System architecture overview
- [`sysfs-attributes.md`](sysfs-attributes.md) — Kernel driver sysfs interface
- [`getting-started-contributors.md`](getting-started-contributors.md) — Contributor's guide