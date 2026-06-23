# GhostBlade — System Architecture Overview

**Project:** GhostBlade (Project NullSpectre)
**Revision:** 1.0
**Date:** 2026-06-23
**License:** CC-BY-SA 4.0

---

## Table of Contents

1. [Introduction](#introduction)
2. [System Architecture Diagram](#system-architecture-diagram)
3. [Processor Architecture](#processor-architecture)
4. [Inter-Processor Communication](#inter-processor-communication)
5. [Memory Map](#memory-map)
6. [Peripheral Bus Map](#peripheral-bus-map)
7. [Power Domain Architecture](#power-domain-architecture)
8. [Boot Sequence](#boot-sequence)
9. [Data Flow Paths](#data-flow-paths)
10. [Security Model](#security-model)

---

## Introduction

This document provides a high-level architectural overview of the GhostBlade dual-processor pentesting device. It connects the information spread across the schematic netlist (`GhostBlade.mf`), device tree (`software/dts/`), firmware headers (`firmware/rp2350b/include/`), and kernel driver (`software/linux-drivers/`) into a single reference.

For detailed pin assignments, see [Pin Assignments](pin-assignments.md).
For power rail specifications, see [Power Tree](power-tree.md).
For SPI protocol details, see [SPI Protocol & Timing](spi-protocol-timing.md).

---

## System Architecture Diagram

```mermaid
graph TB
    subgraph RK3576_SoC["RK3576 — Linux Host"]
        A72["4× Cortex-A72 @ 1.8 GHz"]
        A53["4× Cortex-A53 @ 1.4 GHz"]
        NPU["6 TOPS NPU"]
        GPU["Mali-G52 GPU"]
        SPI0_M["SPI0 Master Controller"]
        MIPI_CSI2["MIPI-CSI-2 RX"]
        I2C1_M["I2C1 Master"]
        PCIe_RC["PCIe Gen3 ×2 RC"]
        eMMC_C["eMMC 5.1 Controller"]
        SDMMC["SDMMC0"]
        SDIO_H["SDIO Host"]
        USB_OTG["USB 3.0 OTG"]
        UART0_DBG["UART0 Debug"]
        DSI_TX["MIPI-DSI TX"]
    end

    subgraph RP2350B_MCU["RP2350B — Real-Time Coprocessor"]
        M33["2× Cortex-M33 / Hazard3 @ 150 MHz"]
        SPI0_S["SPI0 Slave"]
        SPI1_MCU["SPI1 Master"]
        SPI2_MCU["SPI2 Master"]
        I2C0_S["I2C0 Slave"]
        I2C1_MCU["I2C1 Master"]
        ADC_MCU["ADC (12-bit)"]
        PIO["2× PIO Blocks"]
        GPIO_BANK["GPIO Bank 0–1"]
    end

    subgraph RF_Frontend["RF Frontend"]
        LMS["LMS7002M SDR<br/>100 kHz–3.8 GHz<br/>2×2 MIMO, 12-bit"]
        CC["CC1101 Sub-GHz<br/>300–928 MHz<br/>OOK/FSK/GFSK"]
        NFC["ST25R3916 NFC<br/>ISO 14443/15693"]
        ANT_SW["PE42422 SP4T<br/>Antenna Switch"]
    end

    subgraph Wireless["Wireless"]
        WiFi["MT7922<br/>Wi-Fi 6E / BT 5.4"]
    end

    subgraph Storage_Memory["Storage & Memory"]
        DDR["8 GB LPDDR5<br/>3200 MT/s"]
        EMMC["32 GB eMMC 5.1"]
        NVMe["M.2 2230 NVMe<br/>PCIe Gen3 ×2"]
    end

    subgraph Power["Power Management"]
        PMIC["RK817 PMIC<br/>4× BUCK + LDOs"]
        BAT["5000 mAh Li-Po<br/>3.7 V nominal"]
    end

    %% RK3576 ↔ RP2350B
    RK3576_SoC -- "SPI0 @ 50 MHz<br/>CRC-64/CRC-32 framed" --> RP2350B_MCU
    RK3576_SoC -- "INT_REQ / HOST_RDY / MCU_RESET" --> RP2350B_MCU
    RK3576_SoC -- "I2C1 (telemetry)" --> RP2350B_MCU

    %% RK3576 ↔ Peripherals
    RK3576_SoC -- "MIPI-CSI-2 (IQ data)" --> LMS
    RK3576_SoC -- "PCIe Gen3 ×2" --> NVMe
    RK3576_SoC -- "eMMC 5.1 8-bit" --> EMMC
    RK3576_SoC -- "LPDDR5 3200" --> DDR
    RK3576_SoC -- "SDIO 4-bit" --> WiFi
    RK3576_SoC -- "USB 3.0 OTG" --> USB_OTG

    %% RP2350B ↔ Peripherals
    RP2350B_MCU -- "SPI1 (control + config)" --> LMS
    RP2350B_MCU -- "GPIO (LNA_EN, RESET)" --> LMS
    RP2350B_MCU -- "SPI1 shared (CC1101)" --> CC
    RP2350B_MCU -- "GPIO (GDO0, GDO2)" --> CC
    RP2350B_MCU -- "SPI2 (control)" --> NFC
    RP2350B_MCU -- "I2C1 (aux)" --> NFC
    RP2350B_MCU -- "GPIO (ANT_SEL0/1)" --> ANT_SW

    %% Power
    BAT --> PMIC
    PMIC --> RK3576_SoC
    PMIC --> RP2350B_MCU
    PMIC --> LMS
    PMIC --> NFC
```

---

## Processor Architecture

### RK3576 — Linux Host Processor

| Feature | Specification |
|---------|--------------|
| CPU | 4× Cortex-A72 @ 1.8 GHz + 4× Cortex-A53 @ 1.4 GHz (big.LITTLE) |
| NPU | 6 TOPS (INT8) |
| GPU | Mali-G52 MP2 |
| RAM | 8 GB LPDDR5 @ 3200 MT/s (2× 4 GB Samsung K3LKBKB0BM-MGCJ) |
| Storage | 32 GB eMMC 5.1 (Kioxia THGBMJG6C1LBAB7) |
| Expansion | M.2 2230 NVMe (PCIe Gen3 ×2) |
| OS | Linux kernel 6.6+ (Buildroot / Yocto) |
| Driver | `apex_bridge` SPI character device driver |

### RP2350B — Real-Time Coprocessor

| Feature | Specification |
|---------|--------------|
| CPU | 2× Cortex-M33 + 2× Hazard3 RISC-V @ 150 MHz |
| SRAM | 520 KB (4 banks × 130 KB) |
| PSRAM | Optional off-chip PSRAM via QSPI |
| SPI0 | Slave interface to RK3576 (up to 50 MHz) |
| SPI1 | Master to LMS7002M SDR + CC1101 sub-GHz |
| SPI2 | Master to ST25R3916 NFC |
| I2C0 | Slave to RK3576 (telemetry/monitoring) |
| I2C1 | Master to ST25R3916 (auxiliary control) |
| ADC | 12-bit, 4 channels (battery, temperature, VBAT, VDD) |
| PIO | 2× programmable I/O blocks for antenna switching |
| Watchdog | Hardware watchdog timer (5 s timeout) |

---

## Inter-Processor Communication

The RK3576 and RP2350B communicate over two buses:

### Primary: SPI0 (Command/Data Path)

```mermaid
sequenceDiagram
    participant H as RK3576 (Host)
    participant M as RP2350B (MCU)

    Note over H,M: SPI Mode 0, 50 MHz, MSB-first
    H->>M: CSn LOW → TX frame (SYNC + CMD + LEN + RESERVED + HDR_CRC + PAYLOAD + CRC32)
    H->>M: CSn HIGH

    Note over M: MCU validates CRC-64 (header) + CRC-32 (payload)

    alt Response Required
        M->>H: INT_REQ LOW (data ready)
        H->>M: CSn LOW → NOP frame → RX response
        H->>M: CSn HIGH
        M->>H: INT_REQ HIGH (deassert)
    else No Response (e.g., SDR_STREAM start)
        Note over H,M: No response phase needed
    end
```

| Signal | Direction | RK3576 GPIO | RP2350B Pin | Function |
|--------|-----------|-------------|-------------|----------|
| SPI0_SCK | RK3576 → RP2350B | GPIO1_A2 | PIN_18 | SPI clock |
| SPI0_MOSI | RK3576 → RP2350B | GPIO1_A0 | PIN_19 | Host → MCU data |
| SPI0_MISO | RP2350B → RK3576 | GPIO1_A1 | PIN_16 | MCU → Host data |
| SPI0_CSn | RK3576 → RP2350B | GPIO1_A3 | PIN_17 | Chip select (active low) |
| INT_REQ | RP2350B → RK3576 | GPIO1_B0 | PIN_20 | MCU interrupt request |
| HOST_RDY | RK3576 → RP2350B | GPIO1_B1 | PIN_21 | Host ready signal |
| MCU_RESET | RK3576 → RP2350B | GPIO1_B2 | PIN_24 | MCU reset (active low) |

### Secondary: I2C1 (Telemetry/Debug)

| Signal | Direction | RK3576 GPIO | RP2350B Pin | Function |
|--------|-----------|-------------|-------------|----------|
| I2C1_SDA | Bidirectional | I2C1_SDA | PIN_25 | Data line |
| I2C1_SCL | RK3576 → RP2350B | I2C1_SCL | PIN_26 | Clock line |

I2C address: `0x42` (RP2350B slave address for telemetry polling).

---

## Memory Map

### RP2350B SRAM Layout

| Section | Address Range | Size | Purpose |
|---------|--------------|------|---------|
| `.text` | `0x20000000` – `0x2003FFFF` | 256 KB | Code + rodata |
| `.data` / `.bss` | `0x20040000` – `0x2004FFFF` | 64 KB | Initialized/uninitialized data |
| `.dma.sdr_rx` | `0x20050000` – `0x2005FFFF` | 64 KB | SDR DMA RX ring buffer |
| `.dma.sdr_tx` | `0x20060000` – `0x2006FFFF` | 64 KB | SDR DMA TX buffer |
| SPI0 TX/RX | `0x20070000` – `0x2007FFFF` | 64 KB | SPI0 shared TX/RX buffers |
| Heap | `0x20080000` – `0x2007FFFF` | ~108 KB | Dynamic allocation |

See `firmware/rp2350b/rp2350b_memmap.ld` for the exact linker script.

### RK3576 Address Space (Linux)

| Device | Physical Address | Size | Description |
|--------|-----------------|------|-------------|
| SPI0 | `0xFE610000` | 4 KB | SPI master controller |
| I2C1 | `0xFE640000` | 4 KB | I2C master controller |
| MIPI-CSI-2 | `0xFE580000` | 64 KB | MIPI-CSI-2 receiver |
| PCIe | `0xFE150000` | 64 KB | PCIe Gen3 controller |
| eMMC | `0xFE330000` | 64 KB | eMMC 5.1 controller |
| SDIO | `0xFE2C0000` | 4 KB | SDIO host controller |
| UART0 | `0xFEB50000` | 4 KB | Debug serial console |
| GPIO1 | `0xFDC20000` | 256 B | GPIO bank 1 |
| TSADC | `0xFE730000` | 4 KB | Thermal sensor ADC |

---

## Peripheral Bus Map

```mermaid
graph LR
    subgraph RK3576_Buses["RK3576 Internal Buses"]
        AMLB["AMBA AXI4<br/>(High-Speed)"]
        APB["APB<br/>(Low-Speed Peripherals)"]

        AMLB --> SPI0_C["SPI0 @ 50 MHz"]
        AMLB --> MIPI_CSI["MIPI-CSI-2"]
        AMLB --> PCIe_C["PCIe Gen3 ×2"]
        AMLB --> EMMC_C["eMMC 5.1"]
        AMLB --> DDR_C["LPDDR5 3200"]
        AMLB --> USB_C["USB 3.0"]
        AMLB --> SDMMC_C["SDMMC0"]
        AMLB --> SDIO_C["SDIO (Wi-Fi)"]

        APB --> I2C1_C["I2C1 @ 400 kHz"]
        APB --> UART0_C["UART0 @ 1.5Mbaud"]
        APB --> GPIO_C["GPIO1"]
        APB --> PWM_C["PWM0 (Backlight)"]
        APB --> TSADC_C["TSADC (Thermal)"]
    end

    subgraph RP2350B_Buses["RP2350B Internal Buses"]
        AHB_MCU["AHB-Lite<br/>(Cortex-M33 Bus)"]

        AHB_MCU --> SPI0_S_C["SPI0 Slave"]
        AHB_MCU --> SPI1_C["SPI1 Master"]
        AHB_MCU --> SPI2_C["SPI2 Master"]
        AHB_MCU --> I2C0_S_C["I2C0 Slave"]
        AHB_MCU --> I2C1_M_C["I2C1 Master"]
        AHB_MCU --> ADC_C["ADC"]
        AHB_MCU --> PIO_C["PIO (Antenna Switch)"]
    end

    SPI0_C <-->|"CRC-framed<br/>50 MHz"| SPI0_S_C
    I2C1_C <-->|"Telemetry<br/>400 kHz"| I2C0_S_C
    SPI1_C -->|"SDR control"| LMS_C["LMS7002M"]
    SPI1_C -->|"Sub-GHz"| CC_C["CC1101"]
    SPI2_C -->|"NFC control"| NFC_C["ST25R3916"]
    I2C1_M_C -->|"NFC aux"| NFC_C
```

---

## Power Domain Architecture

```mermaid
graph TD
    BAT["Li-Po Battery<br/>3.7 V Nominal<br/>4.2 V Full"]

    subgraph PMIC_RK817["RK817 PMIC"]
        BUCK1["BUCK1 → VDD_CORE<br/>0.9 V / 3 A"]
        BUCK2["BUCK2 → VDD_DDR<br/>1.1 V / 2 A"]
        BUCK3["BUCK3 → VDD_LOGIC<br/>1.8 V / 2 A"]
        BUCK4["BUCK4 → VDD_3V3<br/>3.3 V / 2 A"]
        LDO1["LDO1 → SDR_1V2<br/>1.2 V / 300 mA"]
        LDO2["LDO2 → SDR_1V8<br/>1.8 V / 300 mA"]
        LDO3["LDO3 → SDR_PLL<br/>1.8 V / 300 mA"]
        BOOST["BOOST → VBAT_BOOST<br/>5.0 V / 1 A"]
    end

    subgraph Secondary_Regulators["Secondary Regulators"]
        TLV3V3["TLV75533 → RP2350B_3V3<br/>3.3 V / 500 mA"]
        TLV1V8["TLV75518 → RP2350B_1V8<br/>1.8 V / 300 mA"]
        SY8120["SY8120B → SDR_CORE<br/>1.2 V / 2 A"]
    end

    BAT --> PMIC_RK817

    BUCK1 --> RK3576_CORE["RK3576 VDD_CORE"]
    BUCK2 --> RK3576_DDR["LPDDR5 VDD_DDR"]
    BUCK3 --> RK3576_IO["RK3576 + RP2350B VDD_LOGIC<br/>(I/O voltage)"]
    BUCK4 --> PERIPH_3V3["3.3 V Peripheral Rail<br/>(CC1101, ST25R3916, Antenna Switch)"]
    LDO1 --> SDR_VDD1["LMS7002M VDD_1V2"]
    LDO2 --> SDR_VDD2["LMS7002M VDD_1V8 (Digital)"]
    LDO3 --> SDR_VDD3["LMS7002M VDD_1V8 (PLL)"]
    BOOST --> NFC_TX["ST25R3916 TX Supply<br/>5.0 V Antenna Driver"]

    BUCK4 --> TLV3V3
    BUCK4 --> TLV1V8
    BUCK3 --> SY8120

    TLV3V3 --> RP2350B_VDD["RP2350B VDD (Core + I/O)"]
    TLV1V8 --> RP2350B_AUX["RP2350B Auxiliary 1.8 V"]
    SY8120 --> SDR_CORE_REG["LMS7002M Core 1.2 V"]

    style BAT fill:#ff9,stroke:#333
    style PMIC_RK817 fill:#9cf,stroke:#333
    style Secondary_Regulators fill:#cfc,stroke:#333
```

Power sequencing (from [Power Tree](power-tree.md)):

1. VBAT_ALWAYS → PMIC power-on reset
2. BUCK1 → VDD_CORE (0.9 V, < 5 ms from PWR_ON)
3. BUCK2 → VDD_DDR (1.1 V, < 2 ms after BUCK1)
4. BUCK3 → VDD_LOGIC (1.8 V, < 2 ms after BUCK2)
5. BUCK4 → VDD_3V3 (3.3 V, < 2 ms after BUCK3)
6. LDO1–3 → SDR supplies (< 10 ms after BUCK4 reaches 90%)
7. Total power-up sequence: < 50 ms

---

## Boot Sequence

```mermaid
sequenceDiagram
    participant PWR as Power-On
    participant PMIC as RK817 PMIC
    participant SOC as RK3576
    participant MCU as RP2350B
    participant SDR as LMS7002M
    participant NFC as ST25R3916
    participant CC as CC1101
    participant DRV as apex_bridge Driver

    PWR->>PMIC: PWRON key pressed
    PMIC->>PMIC: Power-on reset
    PMIC->>SOC: VDD_CORE 0.9V (BUCK1)
    PMIC->>SOC: VDD_DDR 1.1V (BUCK2)
    PMIC->>SOC: VDD_LOGIC 1.8V (BUCK3)
    PMIC->>SOC: VDD_3V3 (BUCK4)
    PMIC->>SDR: SDR_1V2, SDR_1V8 (LDO1–3)
    PMIC->>NFC: VDD_NFC_TX 5V (BOOST)

    Note over SOC: ROM boot (24 KB internal)
    SOC->>SOC: SPL loads from eMMC boot0
    SOC->>SOC: LPDDR5 init @ 3200 MT/s
    SOC->>SOC: U-Boot loads from eMMC

    Note over SOC: U-Boot 2024.01
    SOC->>SOC: Device tree load
    SOC->>MCU: MCU_RESET assert (LOW)

    Note over SOC: Linux kernel 6.6+
    SOC->>SOC: Kernel boot
    SOC->>MCU: MCU_RESET release (HIGH)

    Note over MCU: RP2350B firmware starts
    MCU->>MCU: rp2350b_init() — clocks, GPIO, SPI, PIO, ADC
    MCU->>MCU: spi_protocol_init()
    MCU->>MCU: watchdog_init(5000)
    MCU->>SDR: LMS7002M reset + SPI1 config
    MCU->>NFC: ST25R3916 SPI2 + I2C1 config
    MCU->>CC: CC1101 SPI1 config + register write
    MCU->>SOC: HOST_RDY assert (LOW)

    SOC->>DRV: apex_bridge probe on SPI0
    DRV->>MCU: SPI0 NOP (keepalive)
    MCU->>DRV: NOP response

    Note over SOC,CC: System operational
```

---

## Data Flow Paths

### SDR IQ Data Path

```mermaid
graph LR
    ANT["SMA Antenna<br/>(J3 or J4)"]
    LNA["LNA + Filter"]
    LMS["LMS7002M<br/>ADC 12-bit<br/>Up to 61.44 MSPS"]
    DMA_MCU["RP2350B<br/>DMA → Ring Buffer<br/>8 × 512 bytes"]
    SPI["SPI0 Bridge<br/>50 MHz"]
    KDRV["apex_bridge<br/>Kernel Driver"]
    KFIFO["kfifo<br/>4 KB Ring Buffer"]
    USER["Userspace<br/>/dev/apex_bridge0"]

    ANT --> LNA --> LMS
    LMS -->|"MIPI-CSI-2<br/>(IQ samples)"| DMA_MCU
    DMA_MCU -->|"CRC-framed<br/>SPI frames"| SPI
    SPI --> KDRV
    KDRV --> KFIFO
    KFIFO --> USER

    style ANT fill:#f96,stroke:#333
    style LMS fill:#9cf,stroke:#333
    style DMA_MCU fill:#9f9,stroke:#333
    style KDRV fill:#ff9,stroke:#333
```

### NFC Transaction Path

```mermaid
graph LR
    TAG["NFC Tag<br/>(ISO 14443)"]
    NFC_IC["ST25R3916"]
    MCU_SPI["RP2350B<br/>SPI2 + I2C1"]
    BRIDGE["SPI0 Bridge"]
    KDRV["apex_bridge"]
    USER["apex-ctl<br/>/dev/apex_bridge0"]

    TAG -->|"13.56 MHz<br/>RF field"| NFC_IC
    NFC_IC -->|"SPI2<br/>+ IRQ"| MCU_SPI
    MCU_SPI -->|"CMD_NFC_TRANSACT<br/>frame"| BRIDGE
    BRIDGE --> KDRV
    KDRV --> USER
```

### Sub-GHz Radio Path

```mermaid
graph LR
    ANT2["SMA Antenna<br/>(J4 Sub-GHz)"]
    CC["CC1101<br/>300–928 MHz"]
    MCU2["RP2350B<br/>SPI1 shared"]
    BRIDGE2["SPI0 Bridge"]
    KDRV2["apex_bridge"]
    USER2["apex-ctl"]

    ANT2 <-->|"OOK/FSK/GFSK"| CC
    CC <-->|"SPI1 + GDO0/GDO2"| MCU2
    MCU2 <-->|"CMD_CC1101_CFG<br/>frame"| BRIDGE2
    BRIDGE2 <--> KDRV2
    KDRV2 <--> USER2
```

---

## Security Model

```mermaid
graph TB
    subgraph Hardware_Security["Hardware Security"]
        WDG["Hardware Watchdog<br/>5 s timeout"]
        BROWN["Brownout Detector<br/>VDD < 2.8 V → reset"]
        CRC_HW["CRC-64 (header)<br/>CRC-32 (payload)"]
        JTAG_LOCK["JTAG/SWD Lock<br/>(RP2350B BOOTSEL)"]
    end

    subgraph Firmware_Security["Firmware Security"]
        FRAME_VAL["SPI Frame Validation<br/>sync byte + CRC"]
        STATE_M["State Machine<br/>IDLE → HEADER → PAYLOAD"]
        WDT_KICK["Watchdog Kick<br/>every 2.5 s"]
    end

    subgraph Linux_Security["Linux Security"]
        CHAR_DEV["Character Device<br/>/dev/apex_bridge0"]
        SYSFS_RO["Sysfs Read-Only<br/>telemetry attributes"]
        IOCTL["ioctl Commands<br/>validated parameters"]
    end

    CRC_HW --> FRAME_VAL
    FRAME_VAL --> CHAR_DEV
    WDG --> WDT_KICK
    BROWN --> WDG
    STATE_M --> IOCTL
    SYSFS_RO --> CHAR_DEV
```

**Planned future security enhancements:**
- AES-128-CTR encryption on the SPI bridge (firmware roadmap)
- Secure boot for RP2350B (signature verification)
- RP2350B JTAG/SWD lock after production programming
- RK3576 secure boot chain (eFUSE key provisioning)

---

## Related Documents

- [SPI Protocol & Timing](spi-protocol-timing.md) — Frame format, timing diagrams, CRC specification
- [Power Tree](power-tree.md) — Detailed power rail specifications and sequencing
- [Pin Assignments](pin-assignments.md) — Cross-reference: schematic net → DTS GPIO → firmware pin
- [Sysfs Attributes](sysfs-attributes.md) — Driver telemetry attributes and usage
- [Flashing Guide](flashing-guide.md) — How to flash firmware and load drivers
- [Hardware Test Procedures](hardware-test-procedures.md) — Manufacturing test plan
- [Getting Started](getting-started.md) — Development environment setup