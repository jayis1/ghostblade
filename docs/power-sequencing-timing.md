# GhostBlade Power Sequencing Timing Charts
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

This document provides detailed timing diagrams for the GhostBlade power sequencing,
including cold boot, warm reset, sleep/wake, and emergency shutdown sequences.

## 1. Cold Boot Timing (Power-On Reset)

```
Time (ms)  Event                          GPIO/Delay        Rail Voltage
─────────  ───────────────────────────   ──────────────    ─────────────
   0       VBAT applied (> 3.0V)          —                 VBAT ramps
   2       PMIC PWRON key press            PMIC internal    —
   5       RK817: VDD_SYS_3V4 stable       —                 3.4V
   7       RK817: VDD_LOGIC 1.8V stable    —                 1.8V
  10       RK817: VDD_CORE 0.9V stable     —                 0.9V
  12       RK3576: PLL lock                 —                 —
  15       RK817: VDD_3V3 stable            —                 3.3V
  17       RK817: VDD_DDR 1.1V stable       —                 1.1V
  18       RK817: VDDQ_DDR 0.6V stable      —                 0.6V
  20       RK817: VCC_SDIO enable           —                 3.3V
            ┌──────────────────────────────────────────────────────────────┐
   20ms     │ RP2350B GPIO28 → HIGH (SDR 1V8)                            │
   25ms     │ RP2350B GPIO29 → HIGH (SDR 1V1)  [delay ≥ 5ms after 1V8] │
   30ms     │ RP2350B GPIO30 → HIGH (SDR 3V3)  [delay ≥ 5ms after 1V1] │
   50ms     │ RP2350B VCC_3V3_RP stable (PMIC ramp)                      │
            └──────────────────────────────────────────────────────────────┘
  60       MCU_RESET deasserted (GPIO1_B2)  RK3576 GPIO      —
            ┌──────────────────────────────────────────────────────────────┐
  60ms      │ RP2350B boot ROM (~300µs)                                   │
  60.3ms    │ RP2350B Stage 2 boot (~2ms)                                 │
  62ms      │ RP2350B firmware entry                                       │
  63ms      │ peripheral_power_init() — GPIO config                        │
  63ms      │ watchdog_init(5000)                                          │
  65ms      │ spi_protocol_init()                                           │
  67ms      │ battery_monitor_init()                                        │
  69ms      │ sdr_dma_init()                                                │
            └──────────────────────────────────────────────────────────────┘
            ┌──────────────────────────────────────────────────────────────┐
 100ms      │ RP2350B GPIO23 → HIGH (CC1101 VCC)  [≥50ms after SDR]     │
 100ms      │ RP2350B GPIO22 → HIGH (NFC VCC)     [≥50ms after SDR]      │
            └──────────────────────────────────────────────────────────────┘
            ┌──────────────────────────────────────────────────────────────┐
 100ms      │ CC1101 SRES strobe (chip reset)                              │
 110ms      │ CC1101 register configuration write                           │
 120ms      │ ST25R3916 SET_DEFAULT command                                 │
 130ms      │ ST25R3916 oscillator calibration                              │
            └──────────────────────────────────────────────────────────────┘
            ┌──────────────────────────────────────────────────────────────┐
 140ms      │ LMS7002M initialization via SPI1                               │
 150ms      │ LMS7002M PLL calibration                                     │
            └──────────────────────────────────────────────────────────────┘
 160       RP2350B: HOST_RDY assert (GPIO1_B1)  RP2350B GPIO
            ┌──────────────────────────────────────────────────────────────┐
 200ms      │ RK3576: Linux kernel boots                                    │
 300ms+     │ apex_bridge driver probes SPI0                                 │
            │ SPI handshake: HOST_RDY + INT_REQ                              │
            │ Telemetry flow begins                                          │
            └──────────────────────────────────────────────────────────────┘
 500ms     System fully operational

Total cold boot time: ~500ms from VBAT to operational
```

### Voltage Rail Ramp Times

```
                    ┌─────┐
VDD_CORE (0.9V)     │     ├───────────────────────────────────────
                    │     │
0V ─────────────────┘     5ms
                    ├─────┤
                    ramp   stable

                    ┌─────────────┐
VDD_3V3             │             ├─────────────────────────────────
                    │             │
0V ─────────────────┘             15ms
                    ├─────────────┤
                    ramp          stable

                    ┌───┐
SDR 1V8             │   ├───────────────────────────────────────────
                    │   │
0V ─────────────────┘   20ms
                    ├───┤
                    ramp stable

                    ┌─┐
SDR 1V1             │ ├─────────────────────────────────────────────
                    │ │
0V ─────────────────┘  25ms
                    ├─┤
                    ramp stable (requires SDR 1V8 stable ≥5ms)

                    ┌─┐
SDR 3V3 (RF)        │ ├─────────────────────────────────────────────
                    │ │
0V ─────────────────┘  30ms
                    ├─┤
                    ramp stable (requires SDR 1V1 stable ≥5ms)

                    ┌────────┐
NFC 5V              │        ├───────────────────────────────────────
                    │        │
0V ─────────────────┘        100ms
                    ├────────┤
                    ramp     stable (≥50ms after SDR 3V3)
```

## 2. Warm Reset Timing (MCU Reset Only)

The RK3576 can reset the RP2350B without cycling the power rails. This is
used for firmware updates and error recovery.

```
Time (µs)  Event                              GPIO/Signal
─────────  ──────────────────────────────     ───────────
   0       MCU_RESET asserted (LOW)            GPIO1_B2 → LOW
  10       RP2350B enters hardware reset        —
  50       RP2350B I/O pins go high-impedance   —
  100      MCU_RESET deasserted (HIGH)          GPIO1_B2 → HIGH
  300       RP2350B crystal oscillator starts    —
  500       RP2350B boot ROM begins             —
  2000      RP2350B firmware entry              —
  3000      peripheral_power_init()              GPIO28-31 configured
  5000      CC1101 re-initialized               —
 10000      ST25R3916 re-initialized            —
 15000      LMS7002M re-initialized             —
 20000      HOST_RDY asserted                   GPIO1_B1 → HIGH
 25000      SPI handshake complete              —
 30000      Full operation resumed              —

Notes:
- SDR/NFC/Sub-GHz power rails remain ON during warm reset
- DMA engine is paused during reset and restarts cleanly
- The RK3576 should flush any pending SPI transactions before
  asserting MCU_RESET
- Minimum reset pulse width: 10µs (RP2350B datasheet)
- HOST_RDY must be deasserted before MCU_RESET is asserted
```

## 3. Sleep/Wake Timing (Low-Power Mode)

The RP2350B enters sleep mode when the RK3576 signals no active SDR stream.

```
SLEEP ENTRY:

Time (ms)  Event                              Notes
─────────  ──────────────────────────────     ──────────────────────
   0       RK3576 sends CMD_SDR_STREAM(0)     Stop streaming
   1       RP2350B: sdr_dma_stop()            Flush DMA buffers
   2       RP2350B: CC1101 SPWD strobe         Enter sleep mode
   3       RP2350B: ST25R3916 GOTO_SLEEP      Low-power mode
   5       RP2350B: LMS7002M LNA disable       Reduce current
  10       RP2350B: clock_configure(48MHz)     Lower clock
  15       RP2350B: sleep_and_wake()           Enter WFI
            ┌──────────────────────────────────────────────────────┐
            │ SLEEP STATE: ~2mA typical                           │
            │ Watchdog disabled (or 30s timeout)                  │
            │ GPIO28-31 remain HIGH (power rails stay on)         │
            │ GPIO44 (NFC_IRQ) edge-wakeup enabled                │
            │ HOST_RDY remains HIGH                               │
            │ INT_REQ deasserted                                  │
            └──────────────────────────────────────────────────────┘

WAKE ENTRY:

Time (ms)  Event                              Trigger
─────────  ──────────────────────────────     ──────────────────────
   0       INT_REQ asserted OR GPIO44 edge    External event
   1       RP2350B exits WFI                  —
   2       RP2350B: clock_configure(150MHz)   Full speed
   5       RP2350B: LMS7002M LNA enable        Resume SDR
  10       RP2350B: CC1101 SRX strobe          Resume RX
  15       RP2350B: ST25R3916 WAKEUP           Resume NFC
  20       RP2350B: telemetry update           Send state to host
  25       System fully resumed                —
```

## 4. Emergency Shutdown (Low Battery / Watchdog)

Triggered when VBAT drops below 3.0V (brownout threshold) or watchdog
expires without being kicked.

```
Time (ms)  Event                              Notes
─────────  ──────────────────────────────     ──────────────────────
   0       Brownout detected (VBAT < 3.0V)   ADC channel 0 threshold
   0       OR watchdog timeout (5s)           Hardware watchdog
   1       RP2350B: watchdog scratch magic    0xDEADBEEF → scratch reg
   2       RP2350B: HOST_RDY deassert        Pin 21 → LOW
   3       RP2350B: LMS7002M TX disable        Prevent TX on low V
   5       RP2350B: CC1101 SPWD strobe         Sleep mode
   8       RP2350B: ST25R3916 GOTO_SLEEP        Sleep mode
  10       RP2350B: GPIO 9 → LOW (SDR 3V3 off)  PWR_GPIO_SDR_3V3
  15       RP2350B: GPIO 7 → LOW (SDR 1V1 off)  PWR_GPIO_SDR_1V1
  20       RP2350B: GPIO 6 → LOW (SDR 1V8 off)  PWR_GPIO_SDR_1V8
  25       RP2350B: GPIO 22 → LOW (Sub-GHz off) PWR_GPIO_SUBGHZ
  30       RP2350B: GPIO 11 → LOW (NFC off)     PWR_GPIO_NFC
  35       RP2350B: GPIO 25 → LOW (SDIO off)    PWR_GPIO_SDIO
  40       RK3576: receives INT_REQ (MCU state)    —
  50       RP2350B: watchdog_system_reset()        Full MCU reset
            ┌──────────────────────────────────────────────────────┐
            │ After watchdog reset:                                │
            │ - RP2350B firmware reboots from scratch               │
            │ - All peripherals re-initialized                      │
            │ - Watchdog scratch magic checked in main()            │
            │ - If 0xDEADBEEF: brownout recovery path               │
            │   → Send BROWNOUT flag in next telemetry              │
            │   → RK3576 logs brownout event                        │
            └──────────────────────────────────────────────────────┘
```

## 5. SPI Protocol Timing (Host ↔ MCU)

### 5.1 SPI Transaction Timing

```
                    ┌───┐ ┌───┐ ┌───┐ ┌───┐
SPI_CLK (50 MHz)    │   │ │   │ │   │ │   │
                ┌───┘   └─┘   └─┘   └─┘   └───
                │                                 │
CSn (active low) ┘                                 └───
                │  Header (16B)  │  Payload (N)   │  CRC (4B)  │
                │                │                │             │

MOSI:  [SYNC|CMD|LEN_L|LEN_H|RSVD×4|HDR_CRC64(8B)] [PAYLOAD×N] [CRC32(4B)]
MISO:  [SYNC|STAT|LEN_L|LEN_H|RSVD×4|HDR_CRC64(8B)] [RESPONSE×N] [CRC32(4B)]

Timing Parameters:
  SPI Clock:      50 MHz max (21.4 MHz default)
  CSn setup:      ≥ 10 ns before first clock edge
  CSn hold:       ≥ 10 ns after last clock edge
  Inter-byte:     0 ns (continuous clock)
  Inter-frame:    ≥ 1 µs (CSn deasserted between frames)

Total frame time for 256-byte payload:
  = (16 + 256 + 4) × 8 / 21.4MHz = 103 µs

Total frame time for 4092-byte payload (max):
  = (16 + 4092 + 4) × 8 / 21.4MHz = 1.54 ms
```

### 5.2 Interrupt-Driven Transaction Sequence

```
  RK3576 (Host)                          RP2350B (MCU)
  ───────────────                          ──────────────
                                          [Data ready in TX buffer]
                                             │
                                             ├──── INT_REQ ──────────►
                                             │
  ◄─────────────────────────────────────── INT_REQ (falling edge)
  │
  ├─ HOST_RDY ──────────────────────────────────────────────►
  │
  │                                         [MCU prepares SPI DMA]
  │
  ◄──── SPI Transaction (full-duplex) ──────────────────────────►
  │     Host sends command frame              MCU sends response
  │
  ├─ HOST_RDY (deassert) ──────────────────────────────────────►
  │
  │                                         [Process response]
  │                                         [INT_REQ deassert if
  │                                          no more data]
  │
  Total latency: ~5 µs (INT_REQ) + ~1 µs (HOST_RDY) + frame time
```

### 5.3 Telemetry Update Timing

```
  RP2350B telemetry timer fires every 100ms:

  t=0ms    INT_REQ asserted
  t=0.1ms  HOST_RDY received
  t=0.2ms  SPI transaction: CMD_TELEMETRY response
           20-byte telemetry payload:
             rssi_dbm_x10  (2B)
             temp_c_x10    (2B)
             vbat_mv       (2B)
             cc1101_rssi_x10 (2B)
             nfc_field_mv  (2B)
             flags         (2B)
             uptime_ms     (4B)
             reserved      (4B)
  t=0.3ms  INT_REQ deasserted
  ...
  t=100ms  Next telemetry update
```

## 6. CC1101 Sub-GHz Radio Timing

### 6.1 CC1101 Configuration Timing

```
  RP2350B → CC1101 via SPI1:

  SRES (reset):          2 µs strobe + 50 µs calibration
  Configuration write:   41 registers × (1 + 1) bytes = ~4 µs @ 10 MHz SPI
  SRX (enter RX):        2 µs strobe + 250 µs PLL calibration
  SPWD (sleep):          2 µs strobe

  Total CC1101 init:     ~350 µs
  Total CC1101 config:   ~8 µs
  Total RX entry:        ~260 µs
```

### 6.2 CC1101 State Machine Transitions

```
                    ┌─────────┐  SRES   ┌─────────┐
                    │  IDLE   │◄────────┤  RESET  │
                    └────┬────┘         └─────────┘
                         │
              ┌──────────┼──────────┐
              │          │          │
         SFSTXON┗   SXOFF ┗   SRX ┗
              │          │          │
        ┌─────┴────┐  ┌──┴───┐  ┌──┴──────┐
        │  FSTXON   │  │ XOFF │  │  RX     │
        └─────┬────┘  └──────┘  └──┬──────┘
              │                     │
         STX ┗              ┌───────┤
              │             │       │
        ┌─────┴────┐  ┌────┴──┐ ┌──┴──────┐
        │  TX      │  │RXFIFO │ │  CALIB  │
        └──────────┘  │ OVF   │ └─────────┘
                       └───────┘

Transition times:
  IDLE → RX:        250 µs (includes PLL cal)
  IDLE → TX:        250 µs
  RX → TX:          250 µs (includes frequency hop)
  TX → RX:          250 µs
  RX → SLEEP:       0.5 µs (SPWD strobe)
  SLEEP → IDLE:     250 µs (crystal startup + cal)
```

## 7. ST25R3916 NFC Controller Timing

```
  RP2350B → ST25R3916 via SPI2 (control) + I2C1 (telemetry):

  SET_DEFAULT:           5 ms (full register reset)
  Oscillator startup:    5 ms (crystal stabilization)
  TX field on:           1 ms (antenna matching network settle)
  ISO 14443A REQA:       5 ms (anti-collision + SELECT)
  Polling cycle:         10-50 ms (depends on card count)
  GOTO_SLEEP:            1 ms

  Total ST25R3916 init:  ~15 ms
  Typical polling loop:  100 ms interval (configurable via DTS)
```

## 8. LMS7002M SDR Timing

```
  RP2350B → LMS7002M via SPI1 (control) + MIPI-CSI-2 (data):

  Power-on sequence:     10 ms (three rails with 5ms delays)
  Reset:                  1 µs (GPIO LOW ≥1µs, then HIGH)
  SPI init:               5 ms (register writes + verification)
  PLL calibration:      10 ms (tune to target frequency)
  RX enable:              1 ms (LNA + mixer + filter settle)
  TX enable:              2 ms (PA + filter + power ramp)
  Stream start:         500 µs (MIPI-CSI-2 link training)
  Stream stop:          200 µs (flush FIFOs, disable link)

  Total LMS7002M init:  ~35 ms (from rails-on to RX ready)
  Total SDR start:      ~50 ms (including DMA + SPI bridge)
  SDR latency (RX→data): ~1 ms (from stream start to first IQ sample)
```

## 9. Total Boot Budget Summary

| Phase                    | Duration  | Cumulative |
|--------------------------|-----------|------------|
| PMIC rails stable        | 20 ms     | 20 ms      |
| SDR rails on + ramp      | 15 ms     | 35 ms      |
| RP2350B VCC stable       | 15 ms     | 50 ms      |
| MCU_RESET deassert       | —         | 60 ms      |
| RP2350B boot + init      | 100 ms    | 160 ms     |
| CC1101 init              | 10 ms     | 170 ms    |
| ST25R3916 init           | 15 ms     | 185 ms    |
| LMS7002M init            | 35 ms     | 220 ms    |
| HOST_RDY asserted        | —         | 220 ms    |
| Linux kernel boot        | 200 ms    | 420 ms    |
| apex_bridge driver probe | 50 ms     | 470 ms    |
| SPI handshake            | 10 ms     | 480 ms    |
| **Full system ready**    | —         | **~500 ms** |