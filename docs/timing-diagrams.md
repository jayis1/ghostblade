# GhostBlade — Timing Diagrams & Sequence Charts

<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

This document provides Mermaid timing diagrams and sequence charts for key GhostBlade
operations. These diagrams complement the ASCII timing diagrams in
[power-sequencing-timing.md](power-sequencing-timing.md) and the protocol specifications
in [spi-protocol-timing.md](spi-protocol-timing.md).

---

## Table of Contents

1. [Cold Boot Power Sequencing](#1-cold-boot-power-sequencing)
2. [Warm Reset (MCU Only)](#2-warm-reset-mcu-only)
3. [Power-Down Sequence](#3-power-down-sequence)
4. [SPI Bridge Command Transaction](#4-spi-bridge-command-transaction)
5. [SDR DMA Streaming Flow](#5-sdr-dma-streaming-flow)
6. [NFC Transaction Flow](#6-nfc-transaction-flow)
7. [CC1101 Sub-GHz TX/RX Flow](#7-cc1101-sub-ghz-txrx-flow)
8. [Sleep/Wake State Machine](#8-sleepwake-state-machine)
9. [Watchdog Timeout Recovery](#9-watchdog-timeout-recovery)
10. [Brownout Detection & Recovery](#10-brownout-detection--recovery)

---

## 1. Cold Boot Power Sequencing

```mermaid
sequenceDiagram
    participant PWR as Power Button
    participant PMIC as RK817 PMIC
    participant SOC as RK3576
    participant MCU as RP2350B
    participant SDR as LMS7002M
    participant CC as CC1101
    participant NFC as ST25R3916

    PWR->>PMIC: PWRON asserted
    Note over PMIC: t=0ms — Start power sequence

    PMIC->>SOC: VDD_CORE 0.9V (BUCK1)
    Note over SOC: t=5ms — Core rail stable

    PMIC->>SOC: VDD_LOGIC 1.8V (BUCK2) + VDD_DDR
    Note over SOC: t=10ms — Logic + DDR rails stable

    PMIC->>SOC: VDD_3V3 (BUCK4 + LDOs)
    Note over SOC: t=20ms — 3.3V rail stable

    SOC->>MCU: MCU_RESET deasserted (GPIO1_B2 → HIGH)
    Note over MCU: t=25ms — MCU reset release

    PMIC->>MCU: VDD_3V3_MCU
    Note over MCU: t=30ms — MCU rail stable, boot begins

    MCU->>MCU: RP2350B boot ROM → firmware init
    Note over MCU: t=30-140ms — Peripheral initialization

    MCU->>SDR: GPIO28 HIGH (VDD_1V8_SDR enable)
    Note over SDR: t=30ms — SDR 1.8V rail on

    MCU->>SDR: GPIO29 HIGH (VDD_1V2_SDR enable)
    Note over SDR: t=35ms — SDR 1.2V rail on (5ms after 1.8V)

    MCU->>SDR: GPIO30 HIGH (VDD_RF enable)
    Note over SDR: t=40ms — SDR RF rail on (5ms after 1.2V)

    MCU->>CC: CC1101 SRES (SPI1 command)
    Note over CC: t=100ms — CC1101 chip reset

    MCU->>NFC: NFC_RSTn deasserted (GPIO42)
    Note over NFC: t=120ms — NFC reset release

    MCU->>SDR: LMS7002M RESETn deasserted (GPIO21)
    Note over SDR: t=140ms — SDR reset release, SPI1 config begins

    MCU->>SOC: HOST_RDY asserted (GPIO1_B1 → LOW)
    Note over SOC: t=150ms — MCU signals readiness

    SOC->>MCU: SPI0 probe → apex_bridge driver loads
    Note over SOC,MCU: t=200ms+ — System operational
```

### Timing Constraints

| Parameter | Min | Typical | Max | Unit |
|-----------|-----|---------|-----|------|
| PMIC PWRON to VDD_CORE stable | 3 | 5 | 10 | ms |
| VDD_CORE to VDD_LOGIC stable | 3 | 5 | 10 | ms |
| VDD_LOGIC to VDD_3V3 stable | 5 | 10 | 15 | ms |
| VDD_3V3 to MCU_RESET release | 0 | 5 | 10 | ms |
| SDR 1.8V to SDR 1.2V delay | 5 | 5 | — | ms |
| SDR 1.2V to SDR RF delay | 5 | 5 | — | ms |
| CC1101 reset (SRES) duration | 300 | 300 | — | µs |
| NFC RSTn minimum pulse | 10 | 10 | — | µs |
| LMS7002M RESETn to SPI ready | 10 | 10 | — | ms |
| MCU_RESET release to HOST_RDY | 50 | 120 | 200 | ms |

---

## 2. Warm Reset (MCU Only)

```mermaid
sequenceDiagram
    participant SOC as RK3576
    participant MCU as RP2350B
    participant SDR as LMS7002M
    participant CC as CC1101
    participant NFC as ST25R3916

    Note over SOC: Warm reset triggered by driver or watchdog

    SOC->>MCU: MCU_RESET assert (GPIO1_B2 → LOW)
    Note over MCU: t=0µs — MCU enters reset

    Note over MCU: t=10µs — MCU halted

    SOC->>MCU: MCU_RESET deassert (GPIO1_B2 → HIGH)
    Note over MCU: t=100µs — Reset released

    Note over MCU: t=300µs — Crystal oscillator starts
    Note over MCU: t=500µs — Boot ROM executes

    Note over MCU: t=2ms — Firmware init begins

    MCU->>MCU: rp2350b_init() — clocks, GPIO, SPI, ADC, PIO

    MCU->>SDR: Re-init SPI1, LMS7002M config
    Note over SDR: t=5-15ms — SDR re-initialized

    MCU->>CC: CC1101 SRES + config
    Note over CC: t=5ms — CC1101 re-initialized

    MCU->>NFC: ST25R3916 reset + config
    Note over NFC: t=10ms — NFC re-initialized

    MCU->>SOC: HOST_RDY asserted
    Note over SOC,MCU: t=20ms — MCU ready, driver re-probes SPI0
```

---

## 3. Power-Down Sequence

```mermaid
sequenceDiagram
    participant DRV as apex_bridge driver
    participant SOC as RK3576
    participant MCU as RP2350B
    participant SDR as LMS7002M
    participant CC as CC1101
    participant NFC as ST25R3916
    participant PMIC as RK817 PMIC

    Note over DRV: Shutdown command received (ioctl or SIGTERM)

    DRV->>MCU: APEX_CMD_SLEEP (via SPI0)
    Note over MCU: t=0ms — Shutdown begins

    MCU->>SDR: LMS7002M TX disable, LNA off
    Note over SDR: t=5ms — SDR TX disabled

    MCU->>CC: CC1101 SIDLE strobe (idle mode)
    MCU->>CC: CC1101 SPWD strobe (sleep mode)
    Note over CC: t=10-20ms — CC1101 in sleep

    MCU->>NFC: ST25R3916 field off, OP_CTRL disable
    MCU->>NFC: ST25R3916 GOTO_SLEEP command
    Note over NFC: t=25-30ms — NFC in sleep

    MCU->>MCU: GPIO30 LOW (VDD_RF disable)
    Note over MCU: t=55ms — SDR RF rail off

    MCU->>MCU: GPIO29 LOW (VDD_1V2_SDR disable)
    Note over MCU: t=60ms — SDR 1.2V rail off

    MCU->>MCU: GPIO28 LOW (VDD_1V8_SDR disable)
    Note over MCU: t=65ms — SDR 1.8V rail off

    MCU->>DRV: HOST_RDY deasserted (HIGH)
    Note over DRV: t=70ms — MCU signals shutdown complete

    MCU->>MCU: Watchdog scratch magic written
    Note over MCU: t=75ms — Brownout marker saved

    MCU->>MCU: Watchdog reboot triggered
    Note over MCU: t=80ms — MCU enters deep sleep

    SOC->>PMIC: PMIC PWRON deasserted
    Note over PMIC: t=500ms — Full power-off (if intentional)
```

---

## 4. SPI Bridge Command Transaction

```mermaid
sequenceDiagram
    participant HOST as RK3576 (Host)
    participant SPI as SPI0 Controller
    participant MCU as RP2350B (MCU)

    Note over HOST,MCU: APEX_CMD_SDR_TUNE Example

    HOST->>SPI: Prepare TX frame<br/>SYNC=0xAA, CMD=0xA001<br/>LEN=8, PAYLOAD=[freq,bw,gain]
    SPI->>MCU: CSn LOW
    SPI->>MCU: TX header (16 bytes)<br/>SYNC + CMD + LEN + RESERVED + HDR_CRC64
    SPI->>MCU: TX payload (8 bytes)<br/>freq_hz(4) + bw_hz(2) + gain_db(2)
    SPI->>MCU: TX CRC-32 (4 bytes)
    SPI->>MCU: CSn HIGH
    Note over MCU: MCU validates HDR_CRC64 and CRC-32

    MCU->>HOST: INT_REQ LOW (response ready)
    Note over MCU: MCU processes SDR tune command

    HOST->>SPI: Prepare NOP frame to read response
    SPI->>MCU: CSn LOW
    SPI->>MCU: TX NOP frame (16-byte header only)
    MCU->>SPI: RX response (status + telemetry)
    SPI->>MCU: CSn HIGH

    MCU->>HOST: INT_REQ HIGH (deassert)
    Note over HOST: Driver parses response:<br/>status=OK, PLL locked, RSSI
```

---

## 5. SDR DMA Streaming Flow

```mermaid
sequenceDiagram
    participant HOST as RK3576 (Host)
    participant DRV as apex_bridge driver
    participant MCU as RP2350B (Core 0)
    participant DMA as RP2350B (Core 1)
    participant SDR as LMS7002M

    HOST->>DRV: ioctl(APEX_CMD_SDR_TUNE, {868MHz, 2MHz, 30dB})
    DRV->>MCU: SPI0: CMD_SDR_TUNE
    MCU->>SDR: SPI1: LMS7002M configure (freq, bw, gain)
    SDR->>MCU: PLL locked, status OK
    MCU->>DRV: SPI0: Response (PLL locked, RSSI)

    HOST->>DRV: ioctl(APEX_CMD_SDR_STREAM, START)
    DRV->>MCU: SPI0: CMD_SDR_STREAM (start)
    MCU->>SDR: SPI1: LMS7002M RX enable
    MCU->>DMA: sdr_dma_start()

    loop Streaming (continuous)
        SDR->>DMA: IQ samples via SPI1 DMA
        DMA->>DMA: DMA ring buffer fill (64KB)
        DMA->>MCU: DMA completion interrupt
        MCU->>DRV: INT_REQ LOW (data ready)
        DRV->>HOST: read() returns IQ samples
        MCU->>DRV: INT_REQ HIGH (deassert)
    end

    HOST->>DRV: ioctl(APEX_CMD_SDR_STREAM, STOP)
    DRV->>MCU: SPI0: CMD_SDR_STREAM (stop)
    MCU->>SDR: SPI1: LMS7002M RX disable
    MCU->>DMA: sdr_dma_stop()
    MCU->>DRV: SPI0: Response (stream stopped)
```

---

## 6. NFC Transaction Flow

```mermaid
sequenceDiagram
    participant HOST as RK3576 (Host)
    participant DRV as apex_bridge driver
    participant MCU as RP2350B
    participant NFC as ST25R3916
    participant TAG as NFC Tag

    HOST->>DRV: ioctl(APEX_CMD_NFC_TRANSACT, {cmd, data})
    DRV->>MCU: SPI0: CMD_NFC_TRANSACT (TX data)
    MCU->>NFC: SPI2: ST25R3916 write TX buffer
    MCU->>NFC: SPI2: ST25R3916 transmit ON

    NFC->>TAG: 13.56 MHz carrier + TX data
    TAG->>NFC: Backscatter response
    NFC->>MCU: IRQ (data received)
    MCU->>NFC: SPI2: ST25R3916 read RX buffer
    MCU->>DRV: SPI0: Response (RX data, status)
    DRV->>HOST: ioctl returns {rx_data, status}

    Note over MCU,NFC: For anticollision with multiple tags:

    MCU->>NFC: SPI2: ST25R3916 ANTICOLLISION
    NFC->>TAG: REQA/WUPA broadcast
    TAG->>NFC: ATQA (all tags respond)
    NFC->>MCU: IRQ (collision detected)
    MCU->>NFC: SPI2: ST25R3916 SELECT (uid[0])
    TAG->>NFC: SELECT response (one tag)
    MCU->>DRV: SPI0: Response (UID, status)
```

---

## 7. CC1101 Sub-GHz TX/RX Flow

```mermaid
sequenceDiagram
    participant HOST as RK3576 (Host)
    participant DRV as apex_bridge driver
    participant MCU as RP2350B
    participant CC as CC1101

    Note over HOST,CC: Sub-GHz RX Mode

    HOST->>DRV: ioctl(APEX_CMD_CC1101_CFG, {433MHz, GFSK, 38.4kbps})
    DRV->>MCU: SPI0: CMD_CC1101_CONFIGURE
    MCU->>CC: SPI1: Write config registers (freq, modulation, data rate)
    MCU->>CC: SPI1: SRX strobe (enter RX mode)
    MCU->>DRV: SPI0: Response (configured, status OK)

    Note over CC: CC1101 receiving...
    CC->>MCU: GDO0 IRQ (packet received)
    MCU->>CC: SPI1: Read RX FIFO + RSSI
    MCU->>DRV: INT_REQ LOW (data ready)
    DRV->>HOST: read() returns CC1101 RX data

    Note over HOST,CC: Sub-GHz TX Mode

    HOST->>DRV: ioctl(APEX_CMD_CC1101_TX, {data, len})
    DRV->>MCU: SPI0: CMD_CC1101_TRANSMIT
    MCU->>CC: SPI1: Write TX FIFO
    MCU->>CC: SPI1: STX strobe (enter TX mode)
    CC->>MCU: GDO0 IRQ (TX complete)
    MCU->>DRV: SPI0: Response (TX complete)
```

---

## 8. Sleep/Wake State Machine

```mermaid
stateDiagram-v2
    [*] --> Active : Power on / reset

    Active --> Idle : No SPI commands<br/>for IDLE_TIMEOUT_MS
    Idle --> Active : SPI command received<br/>(INT_REQ wakes host)

    Idle --> LightSleep : No SPI commands<br/>for SLEEP_TIMEOUT_MS
    LightSleep --> Active : HOST_RDY assertion<br/>or MCU_RESET pulse

    LightSleep --> DeepSleep : Battery voltage<br/>< SLEEP_THRESHOLD_MV
    DeepSleep --> Active : MCU_RESET pulse<br/>(full re-init required)

    Active --> Active : Watchdog kick<br/>(every main loop)
    Active --> Brownout : VBAT < BROWNOUT_MV
    Brownout --> Active : VBAT recovers
    Brownout --> DeepSleep : VBAT stays low

    note right of Active
        All peripherals powered
        SPI0 slave active
        SDR/NFC/CC1101 available
    end note

    note right of LightSleep
        SDR rails off (1V8, 1V2, RF)
        CC1101 in SLEEP mode
        NFC field off
        SPI0 slave active (low-power)
        RP2350B clocks reduced
    end note

    note right of DeepSleep
        All peripheral rails off
        Only RTC and watchdog running
        SPI0 slave disabled
        Wake via MCU_RESET only
    end note
```

---

## 9. Watchdog Timeout Recovery

```mermaid
sequenceDiagram
    participant MAIN as Main Loop
    participant WDT as Watchdog Timer
    participant BARK as Bark Handler
    participant SOC as RK3576

    Note over MAIN,WDT: Normal operation: watchdog kicked every loop

    MAIN->>WDT: watchdog_kick() (every 1ms loop)
    Note over WDT: Timer resets to 5000ms

    Note over MAIN: Main loop hangs (infinite loop,<br/>deadlock, or hardware fault)

    Note over WDT: 5000ms elapse without kick

    WDT->>BARK: Bark interrupt (1 tick before reset)
    BARK->>BARK: Deassert INT_REQ (active-low → HIGH)
    BARK->>BARK: Stop SDR DMA
    BARK->>BARK: Stop NFC polling
    BARK->>BARK: CC1101 → idle
    BARK->>WDT: watchdog_kick() (one more chance)
    Note over BARK: Give system 5s to recover

    alt Main loop recovers
        MAIN->>WDT: watchdog_kick()
        Note over MAIN: System continues normally
    else Main loop still stuck
        Note over WDT: 5000ms more elapse
        WDT->>WDT: Full chip reset
        Note over MAIN: RP2350B reboots from scratch
        MAIN->>SOC: HOST_RDY asserted after boot
        SOC->>MAIN: apex_bridge driver re-probes
    end
```

---

## 10. Brownout Detection & Recovery

```mermaid
sequenceDiagram
    participant ADC as ADC (Battery Monitor)
    participant MAIN as Main Loop
    participant WDT as Watchdog Scratch
    participant SOC as RK3576
    participant PMIC as RK817 PMIC

    Note over ADC: Battery voltage monitored every 1s

    loop Every 1000ms
        ADC->>MAIN: VBAT reading (e.g., 3700 mV)
        MAIN->>MAIN: battery_is_brownout(vbat)
    end

    Note over MAIN: VBAT drops below 2800 mV

    ADC->>MAIN: VBAT = 2750 mV
    MAIN->>MAIN: Brownout detected!
    MAIN->>WDT: watchdog_mark_brownout() — write magic to scratch
    MAIN->>SOC: SPI0: Set brownout flag in telemetry

    alt VBAT recovers above 3000 mV
        MAIN->>MAIN: Clear brownout flag
        Note over MAIN: Continue normal operation
    else VBAT drops further (< 2500 mV)
        PMIC->>PMIC: Undervoltage lockout
        Note over PMIC: PMIC shuts down all rails
        Note over MAIN: Full power-off
    end

    Note over MAIN: After power recovery (charger connected)

    MAIN->>MAIN: Boot from reset
    MAIN->>WDT: Check watchdog scratch register
    alt Scratch magic matches
        MAIN->>MAIN: Log brownout recovery event
        Note over MAIN: Skip full peripheral init,<br/>use cached config
    else Scratch magic not set
        Note over MAIN: Normal cold boot
    end
```

---

*Document version: 1.0 — 2026-07-05*