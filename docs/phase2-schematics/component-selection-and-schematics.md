<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# PHASE 2: Detailed Component Selection & Schematics

**Device:** GhostBlade  
**Codename:** Project NullSpectre  
**Date:** 2026-06-14  
**Revision:** 1.0  

---

## 1. Component Selection & Bill of Materials (Key ICs)

### 1.1 Primary Processor — Rockchip RK3576

| Parameter | Value |
|---|---|
| Package | FCBGA 732 (0.65mm pitch, 23mm × 23mm) |
| Process | 8nm LP |
| CPU Complex | 4× Cortex-A72 @ 2.2 GHz + 4× Cortex-A53 @ 1.8 GHz |
| NPU | 6 TOPS (INT8), 3 TOPS (INT16) |
| GPU | Mali G52 MC2, 2× MP2 cores |
| Memory Interface | 2× 16-bit LPDDR5 channels, up to 3200 MT/s |
| Storage | eMMC 5.1 (8-bit), SDIO 3.0 |
| PCIe | 1× Gen3 x2 (2 lanes) |
| USB | USB 3.2 Gen 1 (5 Gbps) OTG, 2× USB 2.0 Host |
| Display | MIPI-DSI 4-lane, up to 2560×1600 |
| Camera | MIPI-CSI-2 4-lane (repurposed for SDR IQ input) |
| Peripherals | 5× SPI, 6× I2C, 10× UART, PWM, I2S, S/PDIF |
| GPIO Banks | GPIO0–GPIO4, 5 banks, 32 GPIOs per bank |
| Operating Voltage | Core: 0.9V, VIO: 1.8V/3.3V |
| Max Power (perf) | ~5.2W (octa-core + NPU + GPU all active) |

### 1.2 Coprocessor — Raspberry Pi RP2350B

| Parameter | Value |
|---|---|
| Package | QFN-60 (0.4mm pitch, 7mm × 7mm) |
| Process | 22nm FDSOI (TSMC N22) |
| CPU | 2× ARM Cortex-M33 @ 150 MHz (dual-core) + Hazard3 RISC-V @ 150 MHz |
| SRAM | 520 KB (6 × 64KB banks + 4 × 4KB scratch) |
| Flash | External QSPI (via QSPI flash bank, up to 64 MB) |
| PIO | 2× PIO blocks (4 state machines each, 32 instructions/SM) |
| Peripherals | 2× SPI, 2× I2C, 2× UART, 24× PWM, ADC (4ch 12-bit) |
| GPIO | 48 programmable GPIO pins |
| Operating Voltage | Core: 1.1V (internal LDO from 3.3V), I/O: 1.8V–3.3V |
| Max Power | ~180 mW (both cores + PIOs active) |

### 1.3 SDR Transceiver — Lime Microsystems LMS7002M

| Parameter | Value |
|---|---|
| Package | QFN-64 (0.5mm pitch, 9mm × 9mm) |
| Frequency Range | 100 kHz – 3.8 GHz (continuous) |
| Bandwidth | Up to 160 MHz (2× RX, 2× TX, MIMO 2×2) |
| ADC/DAC | 12-bit, up to 61.44 MSPS |
| TX Power | Up to +10 dBm (max gain) |
| RX Noise Figure | ~4 dB (LNA on, typical) |
| Interface | MIPI-CSI-2 (4-lane, repurposed from RK3576 MIPI-CSI) for IQ data |
| Control Interface | SPI (Mode 0, up to 50 MHz) from RP2350B |
| Supply | 1.2V (core), 1.8V (digital), 3.3V (analog/RF) |
| Max Power | ~3.2W (MIMO TX+RX full bandwidth) |

### 1.4 Sub-GHz Transceiver — Texas Instruments CC1101

| Parameter | Value |
|---|---|
| Package | QFN-20 (0.5mm pitch, 4mm × 4mm) |
| Frequency | 300–348 MHz, 387–464 MHz, 779–928 MHz (configurable) |
| Modulation | 2-FSK, 4-FSK, GFSK, ASK/OOK, MSK |
| Data Rate | 1.2–500 kbps |
| TX Power | Up to +12 dBm (programmable -30 to +12 dBm) |
| RX Sensitivity | -116 dBm (at 1.2 kbps) |
| Interface | SPI (Mode 0, up to 10 MHz) from RP2350B SPI1 |
| Supply | 1.8V–3.6V (3.3V operation) |
| Max Power | ~120 mW (TX at +12 dBm) |

### 1.5 NFC Frontend — STMicroelectronics ST25R3916

| Parameter | Value |
|---|---|
| Package | QFN-32 (0.5mm pitch, 5mm × 5mm) |
| Standards | ISO/IEC 18092, 14443A/B, 15693, FeliCa, NFC Forum Type 1–5 |
| TX Output | Up to 1.0 W (differential, external matching network) |
| RX Sensitivity | Down to 1 mV input |
| Antenna Tuning | Automatic antenna tuning (AAT) |
| Interface | SPI (Mode 0, up to 10 MHz) + I2C from RP2350B |
| Supply | 3.3V (digital), 5.0V (TX driver from boost converter) |
| Max Power | ~2.5W (active TX at 1W output) |

### 1.6 Wi-Fi/BT Module — MediaTek MT7922

| Parameter | Value |
|---|---|
| Package | LGA-145 (14.5mm × 13.5mm) |
| Standards | Wi-Fi 6E (802.11ax, 2.4/5/6 GHz), Bluetooth 5.4 |
| MIMO | 2×2 MU-MIMO, 160 MHz channel |
| Max PHY Rate | 2402 Mbps (Wi-Fi 6E, 160 MHz, 2×2) |
| Monitor Mode | Supported (Linux mt76 driver, packet injection via mac80211) |
| Interface | PCIe Gen3 x1 + USB 2.0 (for Bluetooth) to RK3576 |
| Supply | 3.3V (main), 1.8V (I/O) |
| Max Power | ~1.8W (Wi-Fi TX at 23 dBm + BT active) |

### 1.7 Antenna Switch — Peregrine PE42422

| Parameter | Value |
|---|---|
| Package | QFN-16 (3mm × 3mm) |
| Frequency | 50 MHz – 6 GHz |
| Topology | SP4T (4:1 MUX) — 4 antenna ports, 1 common |
| Insertion Loss | 0.4 dB @ 3.8 GHz |
| Isolation | >30 dB @ 3.8 GHz |
| Switching Time | <5 μs |
| Control | 2-bit parallel (V1, V2) from RP2350B GPIO |
| Supply | 3.3V (single supply) |

### 1.8 PMIC — Rockchip RK817

| Parameter | Value |
|---|---|
| Package | QFN-48 (6mm × 6mm) |
| Regulators | 4× BUCK (DC-DC), 7× LDO, 1× boost |
| BUCK1 | 0.9V / 5A (SoC core) |
| BUCK2 | 1.8V / 3A (SoC VIO) |
| BUCK3 | 1.1V / 4A (LPDDR5) |
| BUCK4 | 3.3V / 3A (peripherals) |
| Boost | 5.0V / 1.5A (NFC driver, antenna bias) |
| Charger | Li-Po linear charger, 3A max, USB-C input |
| Fuel Gauge | Integrated Coulomb counter |

---

## 2. Inter-Processor Bridge — Schematic Netlist

### 2.1 SPI Bridge Netlist (RK3576 ↔ RP2350B)

The SPI0 interface operates in Mode 0 (CPOL=0, CPHA=0) at up to 50 MHz, with the RK3576 as SPI master and RP2350B as SPI slave.

| Net Name | Source Pin | Series Component | Destination Pin | Notes |
|---|---|---|---|---|
| NET_SPI_CLK | RK3576.GPIO1_A2 (SPI0_SCK) | R1: 33Ω ±1% | RP2350B.PIN_18 (SPI0_SCK) | 33Ω series for impedance matching; 100Ω diff not required (single-ended SPI) |
| NET_SPI_MOSI | RK3576.GPIO1_A0 (SPI0_TX) | R2: 33Ω ±1% | RP2350B.PIN_19 (SPI0_RX) | MOSI from host perspective; RP2350B receives on SPI0 RX |
| NET_SPI_MISO | RK3576.GPIO1_A1 (SPI0_RX) | R3: 33Ω ±1% | RP2350B.PIN_16 (SPI0_TX) | MISO from host perspective; RP2350B transmits on SPI0 TX |
| NET_SPI_CS | RK3576.GPIO1_A3 (SPI0_CSn) | R4: 33Ω ±1% | RP2350B.PIN_17 (SPI0_CSn) | Active-low chip select; 10KΩ pull-up to VDD_3V3 on RP2350B side |
| NET_INT_REQ | RP2350B.PIN_20 | R5: 100Ω ±1% | RK3576.GPIO1_B0 (IRQ input) | RP2350B drives LOW to signal interrupt; 100Ω series + 100KΩ pull-up to VDD_1V8 on RK3576 side |
| NET_HOST_READY | RK3576.GPIO1_B1 | R6: 100Ω ±1% | RP2350B.PIN_21 (HOST_RDY) | Active-low host ready signal; 100KΩ pull-up to VDD_3V3 on RP2350B side |
| NET_MCU_RESET | RK3576.GPIO1_B2 | R7: 0Ω (jumpable) | RP2350B.PIN_24 (RUN/RESET) | Active-low reset; 10KΩ pull-up to VDD_3V3; 100nF cap to GND for glitch filter |
| SPI_GND | Both | — | Both | Shared ground plane (Layer 2) |

#### Pull-up/Pull-down Summary

| Component | Value | Net | Position | Purpose |
|---|---|---|---|---|
| R10 | 10KΩ | NET_SPI_CS | RP2350B side, to VDD_3V3 | Ensures CS stays HIGH when host not driving |
| R11 | 100KΩ | NET_INT_REQ | RK3576 side, to VDD_1V8 | Idle-high interrupt line |
| R12 | 100KΩ | NET_HOST_READY | RP2350B side, to VDD_3V3 | Idle-high ready line |
| R13 | 10KΩ | NET_MCU_RESET | RP2350B side, to VDD_3V3 | Keep MCU in RUN state unless actively reset |
| C10 | 100nF | NET_MCU_RESET | RP2350B side, to GND | Glitch filter, RC = 1ms time constant with R13 |
| C11 | 10μF | VDD_3V3 | RP2350B decoupling | Bulk decoupling at VDD pin |
| C12 | 100nF | VDD_3V3 | RP2350B decoupling | High-frequency decoupling at VDD pin |

### 2.2 I2C Bridge Netlist (RK3576 ↔ RP2350B)

A secondary I2C bus provides out-of-band telemetry and configuration when the SPI bus is streaming SDR data.

| Net Name | Source Pin | Series Component | Destination Pin | Notes |
|---|---|---|---|---|
| NET_I2C1_SDA | RK3576.I2C1_SDA | R20: 0Ω | RP2350B.PIN_25 (I2C0_SDA) | Bidirectional open-drain; 4.7KΩ pull-up to VDD_3V3 on bus |
| NET_I2C1_SCL | RK3576.I2C1_SCL | R21: 0Ω | RP2350B.PIN_26 (I2C0_SCL) | Bidirectional open-drain; 4.7KΩ pull-up to VDD_3V3 on bus |

#### Pull-up Summary

| Component | Value | Net | Position | Purpose |
|---|---|---|---|---|
| R22 | 4.7KΩ | NET_I2C1_SDA | Bus, to VDD_3V3 | I2C pull-up (400 kHz Fast-Mode+) |
| R23 | 4.7KΩ | NET_I2C1_SCL | Bus, to VDD_3V3 | I2C pull-up |

---

## 3. RF Frontend — Schematic Netlist

### 3.1 LMS7002M SDR Transceiver Connections

| Net Name | Source Pin | Passive Network | Destination Pin | Notes |
|---|---|---|---|---|
| NET_SDR_IQ_I_P | LMS7002M.TX_I_P | AC coupling: C30=100nF + R30=50Ω | RK3576.MIPI_CSI_RX0_P | High-speed differential pair; AC-coupled, 100Ω diff termination |
| NET_SDR_IQ_I_N | LMS7002M.TX_I_N | AC coupling: C31=100nF + R31=50Ω | RK3576.MIPI_CSI_RX0_N | Complement of above |
| NET_SDR_IQ_Q_P | LMS7002M.TX_Q_P | AC coupling: C32=100nF + R32=50Ω | RK3576.MIPI_CSI_RX1_P | Second virtual channel |
| NET_SDR_IQ_Q_N | LMS7002M.TX_Q_N | AC coupling: C33=100nF + R34=50Ω | RK3576.MIPI_CSI_RX1_N | Complement |
| NET_SDR_SPI_SCK | RP2350B.PIN_27 (SPI1_SCK) | R34: 33Ω | LMS7002M.PIN_SCLK | SPI control from MCU |
| NET_SDR_SPI_MOSI | RP2350B.PIN_28 (SPI1_TX) | R35: 33Ω | LMS7002M.PIN_SDI | MCU → SDR data |
| NET_SDR_SPI_MISO | LMS7002M.PIN_SDO | R36: 33Ω | RP2350B.PIN_29 (SPI1_RX) | SDR → MCU data |
| NET_SDR_SPI_CS | RP2350B.PIN_30 (SPI1_CSn) | R37: 33Ω | LMS7002M.PIN_CSn | Active-low chip select |
| NET_SDR_RESET | RP2350B.PIN_31 (GPIO) | R38: 0Ω (jumpable) | LMS7002M.PIN_RESETn | Active-low reset; 10KΩ pull-up to VDD_1V8 |
| NET_SDR_GPIO0 | LMS7002M.PIN_GPIO0 | R39: 100Ω | RP2350B.PIN_32 (GPIO) | General-purpose: TX enable |
| NET_SDR_GPIO1 | LMS7002M.PIN_GPIO1 | R40: 100Ω | RP2350B.PIN_33 (GPIO) | General-purpose: RX enable |
| NET_SDR_LNA_EN | RP2350B.PIN_34 (GPIO) | R41: 10KΩ pull-up to VDD_RF | LMS7002M.PIN_LNA_EN | LNA enable (active-high) |

**Note on MIPI-CSI-2 repurposing:** The RK3576's MIPI-CSI-2 receiver (4-lane, 4.5 Gbps/lane) is configured via device tree to accept raw IQ sample data instead of camera frames. The LMS7002M outputs its digital IQ samples over a high-speed serial interface compatible with MIPI-CSI-2 virtual channel framing. The RK3576 DMA engine captures these frames into a ring buffer in LPDDR5.

#### LMS7002M Decoupling Network

| Component | Value | Voltage Rail | Placement | Purpose |
|---|---|---|---|---|
| C40 | 100μF | VDD_1V2_CORE | Adjacent to pin 1 (VDD) | Bulk decoupling |
| C41 | 10μF | VDD_1V2_CORE | Adjacent to pin 1 | Mid-frequency decoupling |
| C42 | 100nF | VDD_1V2_CORE | Adjacent to pin 1 | High-frequency decoupling |
| C43 | 1μF | VDD_1V8_DIG | Adjacent to pin 45 | Digital decoupling |
| C44 | 100nF | VDD_1V8_DIG | Adjacent to pin 45 | High-frequency digital decoupling |
| C45 | 10μF | VDD_3V3_RF | Adjacent to pin 30 | RF supply decoupling |
| C46 | 100nF | VDD_3V3_RF | Adjacent to pin 30 | High-frequency RF decoupling |
| C47 | 1nF | VDD_3V3_RF | Adjacent to pin 30 | Ultra-high-frequency noise (parasitic resonance suppression) |

**Decoupling diad pattern:** Each power pin on the LMS7002M gets a (100nF || 1μF) diad. The RF section additionally gets 1nF triads: (1nF || 100nF || 10μF) to suppress noise across 1 MHz to 3 GHz.

### 3.2 PE42422 Antenna Switch Connections

| Net Name | Source Pin | Series Component | Destination Pin | Notes |
|---|---|---|---|---|
| NET_ANT_SEL0 | RP2350B.PIN_2 | R50: 100Ω | PE42422.V1 | Antenna select bit 0 |
| NET_ANT_SEL1 | RP2350B.PIN_3 | R51: 100Ω | PE42422.V2 | Antenna select bit 1 |
| NET_ANT_COMMON | PE42422.RFC | Impedance match: L50=2.7nH + C50=0.8pF | LMS7002M.TX/RX port | 50Ω microstrip + matching network |
| NET_ANT_PORT0 | PE42422.RF1 | — | SMA_ANT0 (MIMO TX) | Direct 50Ω microstrip to SMA |
| NET_ANT_PORT1 | PE42422.RF2 | — | SMA_ANT1 (MIMO RX) | Direct 50Ω microstrip to SMA |
| NET_ANT_PORT2 | PE42422.RF3 | — | CC1101.ANT (sub-GHz) | 50Ω microstrip + π-network matching |
| NET_ANT_PORT3 | PE42422.RF4 | 50Ω termination | GND (unused) | Terminated with 50Ω resistor to prevent reflections |

#### PE42422 Antenna Selection Truth Table

| V2 (PIN_3) | V1 (PIN_2) | Selected Port | Function |
|---|---|---|---|
| 0 | 0 | RF1 | MIMO TX (SMA_ANT0) |
| 0 | 1 | RF2 | MIMO RX (SMA_ANT1) |
| 1 | 0 | RF3 | Sub-GHz (CC1101 via matching) |
| 1 | 1 | RF4 | Terminated / test port |

### 3.3 CC1101 Sub-GHz Transceiver Connections

| Net Name | Source Pin | Series Component | Destination Pin | Notes |
|---|---|---|---|---|
| NET_CC_SPI_SCK | RP2350B.PIN_8 (SPI1_SCK) | R60: 33Ω | CC1101.PIN_SCLK | SPI Mode 0, up to 10 MHz |
| NET_CC_SPI_MOSI | RP2350B.PIN_9 (SPI1_TX) | R61: 33Ω | CC1101.PIN_SI | MCU → CC1101 |
| NET_CC_SPI_MISO | CC1101.PIN_SO | R62: 33Ω | RP2350B.PIN_12 (SPI1_RX) | CC1101 → MCU |
| NET_CC_SPI_CS | RP2350B.PIN_10 (GPIO) | R63: 33Ω | CC1101.PIN_CSn | Active-low chip select; 10KΩ pull-up to VDD_3V3 |
| NET_CC_GDO0 | CC1101.PIN_GDO0 | R64: 100Ω | RP2350B.PIN_13 (GPIO) | FIFO threshold / sync word detect interrupt |
| NET_CC_GDO2 | CC1101.PIN_GDO2 | R65: 100Ω | RP2350B.PIN_14 (GPIO) | Packet received / TX done interrupt |

#### CC1101 Decoupling

| Component | Value | Rail | Placement | Purpose |
|---|---|---|---|---|
| C60 | 10μF | VDD_3V3 | Pin 15 (VDD) | Bulk decoupling |
| C61 | 100nF | VDD_3V3 | Pin 15 | High-frequency decoupling |
| C62 | 1nF | VDD_3V3 | Pin 15 | Parasitic suppression |
| C63 | 1μF | VDD_3V3 | Pin 4 (VDD) | Second decoupling point |
| C64 | 100nF | VDD_3V3 | Pin 4 | Second HF decoupling |
| L60 | 10nH | Pin 20 (RF_N) | Antenna matching network | Part of π-network for 868/915 MHz |
| C65 | 3.3pF | Pin 20 (RF_N) to GND | Antenna matching | Part of π-network |
| C66 | 3.3pF | Pin 20 (RF_N) to ANT | Antenna matching | Part of π-network |

### 3.4 ST25R3916 NFC Frontend Connections

| Net Name | Source Pin | Series Component | Destination Pin | Notes |
|---|---|---|---|---|
| NET_NFC_SPI_SCK | RP2350B.PIN_40 (SPI2_SCK) | R70: 33Ω | ST25R3916.PIN_SCK | SPI Mode 0, up to 10 MHz |
| NET_NFC_SPI_MOSI | RP2350B.PIN_41 (SPI2_TX) | R71: 33Ω | ST25R3916.PIN_MOSI | MCU → NFC |
| NET_NFC_SPI_MISO | ST25R3916.PIN_MISO | R72: 33Ω | RP2350B.PIN_42 (SPI2_RX) | NFC → MCU |
| NET_NFC_SPI_CS | RP2350B.PIN_43 (GPIO) | R73: 33Ω | ST25R3916.PIN_CSn | Active-low chip select |
| NET_NFC_IRQ | ST25R3916.PIN_IRQ | R74: 100Ω | RP2350B.PIN_44 (GPIO, interrupt) | Active-low interrupt; 10KΩ pull-up to VDD_3V3 |
| NET_NFC_I2C_SDA | RP2350B.PIN_46 (I2C1_SDA) | R75: 0Ω | ST25R3916.PIN_SDA | Secondary control bus (I2C address 0xAC) |
| NET_NFC_I2C_SCL | RP2350B.PIN_47 (I2C1_SCL) | R76: 0Ω | ST25R3916.PIN_SCL | Secondary control bus |
| NET_NFC_ANT_P | ST25R3916.PIN_ANT1 | L80=56nH, C80=27pF | NFC antenna coil (+) | Impedance matching network |
| NET_NFC_ANT_N | ST25R3916.PIN_ANT2 | L81=56nH, C81=27pF | NFC antenna coil (-) | Differential drive to coil |

#### NFC Antenna Matching Network

The NFC antenna is a rectangular loop coil (40mm × 30mm) embedded in the rear case with the following parameters:

- Inductance: ~1.2 μH (measured)
- Q factor: ~25 at 13.56 MHz
- EMI filter: 4-element low-pass (2× 220pF + 2× 1μH) between ST25R3916 and antenna
- Matching: Parallel resonant circuit tuned to 13.56 MHz with 27pF + 56nH (L-type network)

#### ST25R3916 Decoupling

| Component | Value | Rail | Placement | Purpose |
|---|---|---|---|---|
| C90 | 100μF | VDD_5V_TX | Pin 22 (VDD_TX) | Bulk for TX driver |
| C91 | 10μF | VDD_5V_TX | Pin 22 | Mid-frequency TX decoupling |
| C92 | 100nF | VDD_5V_TX | Pin 22 | High-frequency TX decoupling |
| C93 | 10μF | VDD_3V3 | Pin 8 (VDD) | Digital decoupling |
| C94 | 100nF | VDD_3V3 | Pin 8 | High-frequency digital decoupling |
| C95 | 1μF | VDD_1V8 | Pin 12 (VDD_CORE) | Core decoupling |
| C96 | 100nF | VDD_1V8 | Pin 12 | High-frequency core decoupling |

---

## 4. Memory Subsystem — LPDDR5 Interface

### 4.1 LPDDR5 Schematic Specifications

Two 4GB LPDDR5 die (Samsung K3LKBKB0BM-MGCJ or equivalent) in a 2-channel, 16-bit-per-channel configuration:

| Parameter | Value |
|---|---|
| Total Capacity | 8 GB (2 × 4 GB) |
| Data Rate | 3200 MT/s (LPDDR5) |
| Bus Width | 2 × 16-bit = 32-bit effective |
| Burst Length | BL16 (LPDDR5 mode) |
| Operating Voltage | VDD = 1.1V, VDDQ = 1.1V, VDD2 = 0.5V |
| Refresh | 8192 refresh cycles / 32 ms (3.9 μs average interval) |
| Package | FBGA-200 (0.4mm pitch, 12mm × 12mm per die) |

### 4.2 LPDDR5 Pin Group Netlist

| Net Group | RK3576 Pins | Series Term | LPDDR5 Pins | Notes |
|---|---|---|---|---|
| CA[5:0] | DDR_CA0–DDR_CA5 | 33Ω series | CA0–CA5 | Command/Address bus; ODT to VDDQ = 240Ω |
| CK_t/CK_c | DDR_CK_P/N | 33Ω series | CK_t/CK_c | Differential clock; 100Ω diff term at DRAM |
| DQ[15:0]_CH0 | DDR_DQ0–DDR_DQ15 | 22Ω series | DQ0–DQ15 (CH0) | Data bus channel 0; 40Ω pull-down on each byte lane for write leveling |
| DQ[15:0]_CH1 | DDR_DQ16–DDR_DQ31 | 22Ω series | DQ0–DQ15 (CH1) | Data bus channel 1 |
| DQS_t/DQS_c_CH0 | DDR_DQS0_P/N | 22Ω series | DQS_t/DQS_c (CH0) | Data strobe channel 0; 100Ω diff term |
| DQS_t/DQS_c_CH1 | DDR_DQS1_P/N | 22Ω series | DQS_t/DQS_c (CH1) | Data strobe channel 1 |
| RESET_n | DDR_RESETn | 10KΩ pull-up to VDD_1V1 | RESET_n | Active-low reset |

### 4.3 eMMC 5.1 Interface

| Net Name | RK3576 Pin | Destination | Series | Notes |
|---|---|---|---|---|
| EMMC_CLK | eMMC_CLK | eMMC.PIN_CLK | R100: 33Ω | 200 MHz max |
| EMMC_CMD | eMMC_CMD | eMMC.PIN_CMD | R101: 33Ω | Bidirectional command |
| EMMC_DQ0–DQ7 | eMMC_DQ0–DQ7 | eMMC.PIN_D0–D7 | R102: 33Ω (each) | 8-bit data bus |
| EMMC_RST_n | GPIO0_A0 | eMMC.PIN_RSTn | R103: 10KΩ pull-up | Active-low reset |

### 4.4 M.2 NVMe Interface (PCIe Gen3 x2)

| Net Name | RK3576 Pin | Destination | Series | Notes |
|---|---|---|---|---|
| PCIE_TX0_P/N | PCIe_TX0_P/N | M.2.TX0_P/N | AC coupling: C110/C111 = 100nF | 100Ω differential; AC-coupled per PCIe spec |
| PCIE_TX1_P/N | PCIe_TX1_P/N | M.2.TX1_P/N | AC coupling: C112/C113 = 100nF | Second lane |
| PCIE_RX0_P/N | PCIe_RX0_P/N | M.2.RX0_P/N | AC coupling: C114/C115 = 100nF | Received from NVMe |
| PCIE_RX1_P/N | PCIe_RX1_P/N | M.2.RX1_P/N | AC coupling: C116/C117 = 100nF | Second lane |
| PCIE_CLK_P/N | PCIe_CLK_P/N | M.2.CLK_P/N | C118/C119 = 100nF | 100 MHz ref clock |
| PCIE_RST_n | GPIO0_B0 | M.2.PRSNT#/RST | R120: 10KΩ pull-up | Active-low reset |
| PCIE_WAKE_n | GPIO0_B1 | M.2.WAKE# | R121: 10KΩ pull-up | Active-low wake signal |

---

## 5. Power Supply Schematic — Detailed

### 5.1 PMIC (RK817) Regulator Assignments

| Rail | Regulator | Voltage | Max Current | Load | Inductor/Capacitor |
|---|---|---|---|---|---|
| VDD_CORE | BUCK1 | 0.9V | 5A | RK3576 core (A72+A53+NPU+GPU) | 1μH/25A shielded; 2× 100μF ceramic + 1× 22μF ceramic |
| VDD_LOGIC | BUCK2 | 1.8V | 3A | SoC VIO, eMMC, GPIO banks | 1μH/15A; 2× 47μF ceramic + 1× 10μF ceramic |
| VDD_DDR | BUCK3 | 1.1V | 4A | LPDDR5 VDD/VDDQ | 1μH/20A; 3× 100μF ceramic + 2× 22μF ceramic |
| VDD_3V3 | BUCK4 | 3.3V | 3A | RP2350B, CC1101, SPI/I2C pullups | 1μH/10A; 2× 47μF ceramic + 1× 10μF ceramic |
| VDD_1V2_SDR | LDO1 | 1.2V | 300mA | LMS7002M core | 10μF ceramic output cap |
| VDD_1V8_SDR | LDO2 | 1.8V | 200mA | LMS7002M digital I/O | 10μF ceramic output cap |
| VDD_RF | LDO3 + filter | 3.3V (filtered) | 500mA | LMS7002M RF, PE42422 | LDO3 + π-filter (10μH + 2× 100μF) |
| VDD_NFC_TX | Boost | 5.0V | 1.5A | ST25R3916 TX driver | 2.2μH/3A; 2× 100μF ceramic |
| VDD_1V8_MCU | Internal LDO (RP2350B) | 1.1V | 100mA | RP2350B core | RP2350B internal; 100nF decoupling |

### 5.2 Power Sequencing

The RK817 PMIC controls the power-on sequence through programmable sequencer registers:

```
Time    Event                           Rail
────    ─────                            ────
0 ms    VBAT_ALWAYS stable               Battery connected
1 ms    BUCK1 ramps to 0.9V              VDD_CORE
3 ms    BUCK2 ramps to 1.8V              VDD_LOGIC
5 ms    BUCK3 ramps to 1.1V              VDD_DDR
7 ms    BUCK4 ramps to 3.3V              VDD_3V3
10 ms   LDO1 ramps to 1.2V               VDD_1V2_SDR
12 ms   LDO2 ramps to 1.2V → 1.8V        VDD_1V8_SDR
15 ms   LDO3 ramps to 3.3V               VDD_RF
20 ms   Boost ramps to 5.0V              VDD_NFC_TX
25 ms   RK3576 PWR_ON released           SoC boot begins
30 ms   eMMC power stable                Storage available
50 ms   RK3576 releases RESET_n to RP2350B  MCU boot begins
100 ms  RP2350B asserts HOST_RDY          MCU ready for SPI
200 ms  RK3576 U-Boot SPL complete        Loading kernel
```

---

## 6. Display Interface

| Net Name | RK3576 Pin | Destination | Notes |
|---|---|---|---|
| DSI_D0_P/N | DSI_D0_P/N | Display connector D0_P/N | MIPI-DSI 4-lane data lane 0 |
| DSI_D1_P/N | DSI_D1_P/N | Display connector D1_P/N | MIPI-DSI 4-lane data lane 1 |
| DSI_D2_P/N | DSI_D2_P/N | Display connector D2_P/N | MIPI-DSI 4-lane data lane 2 |
| DSI_D3_P/N | DSI_D3_P/N | Display connector D3_P/N | MIPI-DSI 4-lane data lane 3 |
| DSI_CLK_P/N | DSI_CLK_P/N | Display connector CLK_P/N | MIPI-DSI clock lane |
| DSI_RESET | GPIO0_C0 | Display connector RST | Active-low reset |
| DSI_BL_EN | GPIO0_C1 | Display backlight enable | Active-high |
| DSI_BL_PWM | PWM0 | Display backlight PWM | 10 kHz PWM dimming |

---

## 7. USB-C Interface

| Net Name | RK3576 Pin | Destination | Notes |
|---|---|---|---|
| USB3_TX_P/N | USB3_TX_P/N | USB-C connector TX1_P/N | SuperSpeed transmit pair |
| USB3_RX_P/N | USB3_RX_P/N | USB-C connector RX1_P/N | SuperSpeed receive pair |
| USB2_DP/DM | USB2_DP/DM | USB-C connector D+/D- | USB 2.0 data |
| USB_CC1/CC2 | GPIO + FUSB302 | USB-C connector CC1/CC2 | Type-C controller (FUSB302B) for PD negotiation |
| USB_VBUS | VBUS → RK817 CHG_IN | USB-C connector VBUS | Charging input (5V/3A) |
| USB_ID | GPIO0_D0 | USB-C ID pin | OTG host/device detect |

---

## 8. MicroSD Card Slot

| Net Name | RK3576 Pin | Destination | Notes |
|---|---|---|---|
| SDMMC0_CLK | SDMMC0_CLK | μSD connector CLK | Up to 200 MHz |
| SDMMC0_CMD | SDMMC0_CMD | μSD connector CMD | Bidirectional |
| SDMMC0_D0 | SDMMC0_D0 | μSD connector D0 | Data line 0 (1-bit or 4-bit mode) |
| SDMMC0_D1 | SDMMC0_D1 | μSD connector D1 | Data line 1 |
| SDMMC0_D2 | SDMMC0_D2 | μSD connector D2 | Data line 2 |
| SDMMC0_D3 | SDMMC0_D3 | μSD connector D3 | Data line 3 |
| SDMMC0_DET | GPIO0_D7 | μSD connector CD | Card detect (active-low) |