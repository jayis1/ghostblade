<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# PHASE 1: Conceptual Architecture & Requirements

**Device:** GhostBlade  
**Codename:** Project NullSpectre  
**Date:** 2026-06-14  
**Revision:** 1.0  

---

## 1. System Power Consumption Targets

### 1.1 Power Budget by Operating Mode

| Operating Mode | RK3576 Domain | RP2350B Domain | SDR/RF Domain | Total Est. | Notes |
|---|---|---|---|---|---|
| **Deep Sleep** | 15 mW | 2 mW | 0 mW | **17 mW** | RK3576: DDR self-refresh, ARM cores WFI; RP2350B: dormant; all RF disabled |
| **Idle/Standby** | 350 mW | 15 mW | 0 mW | **365 mW** | Linux idle, no active scans; RP2350B monitoring GPIO interrupts |
| **Light Compute** (console, scripts) | 1.2 W | 30 mW | 0 mW | **1.23 W** | 2x A53 active, Wi-Fi off |
| **Active Pentest** (Wi-Fi + NFC) | 2.8 W | 80 mW | 400 mW | **3.28 W** | 4x A53 + Wi-Fi 6E scanning + NFC polling |
| **SDR Active** (sub-GHz sniffing) | 2.5 W | 120 mW | 1.8 W | **4.42 W** | LMS7002M MIMO RX, CC1101 standby |
| **Full SDR MIMO** (TX+RX all bands) | 3.5 W | 150 mW | 3.2 W | **6.85 W** | LMS7002M full TX+RX MIMO, NPU inference on captured IQ |
| **Peak Burst** (NPU + SDR + Wi-Fi inject) | 5.2 W | 180 mW | 3.5 W | **8.88 W** | Octa-core burst + NPU + all RF; thermal throttle begins at ~7W sustained |

### 1.2 Thermal Dissipation Profile

| Parameter | Value | Method |
|---|---|---|
| Maximum sustained TDP | 7.0 W | Without thermal throttle engagement |
| Thermal throttle threshold | T_junction = 85°C | RK3576 internal thermal sensor triggers DVFS downclock |
| Hard shutdown threshold | T_junction = 105°C | PMIC force-cut |
| Thermal resistance (junction-to-case) | 8.5°C/W | 6-layer FR-4 with thermal via matrix + copper coin |
| Thermal resistance (case-to-ambient) | 12°C/W | Natural convection with aluminum mid-frame |
| Ambient operating range | -10°C to +45°C | Extended industrial, battery operation |
| Estimated steady-state skin temp | ≤ 42°C at 5W | Aluminum frame acts as heatsink + EMI shield |

### 1.3 Physical Form-Factor Constraints

The device targets an "oversized smartphone" form factor — pocketable but thicker than a phone to accommodate the RF section and battery.

| Dimension | Target | Constraint Source |
|---|---|---|
| Length | 162 mm | 6.4" display diagonal, 19.5:9 aspect ratio |
| Width | 76 mm | Ergonomic grip width |
| Thickness | 18 mm | RF shielding can height (5mm) + battery (6mm) + PCB stack (1.6mm) + display (2mm) + frame (3.4mm) |
| Mass (target) | 320 g | Battery (80g) + PCB+components (110g) + frame (70g) + display (60g) |
| Display | 6.4" IPS 1080x2400 | 400 nits, Gorilla Glass 5 |
| Battery | 5000 mAh Li-Po (3.85V nominal) | 19.25 Wh; ~4h active SDR, ~12h light compute |
| I/O Ports | USB-C 3.2 Gen1 (OTG + DP alt), μSD card slot | USB-C for charging + host/device; μSD for boot rescue |
| Antenna Connectors | 2x SMA (primary MIMO), 1x u.FL (sub-GHz), internal NFC coil | SMA on top edge; NFC loop antenna integrated in rear case |
| Buttons | Volume rocker, Power, 3x programmable GPIO buttons | GPIO buttons: PWR held 2s = force-off; dual-press = recovery mode |

---

## 2. High-Level Data Flow & Bus Topologies

### 2.1 AMBA AXI4 Interconnect Matrix (RK3576 Internal)

The RK3576 implements a multi-layer AMBA AXI4 interconnect providing concurrent high-bandwidth paths. The following diagram shows the logical topology:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        RK3576 AXI4 INTERCONNECT                        │
│                                                                         │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐            │
│  │  A72 x4  │   │  A53 x4  │   │  NPU 6T  │   │  Mali    │            │
│  │  (perf)  │   │  (eff)   │   │  (infer) │   │  G52     │            │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └────┬─────┘            │
│       │              │              │              │                    │
│  ═════╪══════════════╪══════════════╪══════════════╪═════════           │
│       │         AXI4 INTERCONNECT (NIC-400)       │                    │
│  ═════╪══════════════╪══════════════╪══════════════╪═════════           │
│       │              │              │              │                    │
│  ┌────┴─────┐   ┌────┴─────┐   ┌────┴─────┐  ┌────┴─────┐            │
│  │ LPDDR5   │   │  PCIe    │   │  USB3    │  │  MIPI    │            │
│  │  DDR-    │   │  Gen3x2  │   │  Host/   │  │  CSI/    │            │
│  │  Ctrl    │   │  NVMe    │   │  Device  │  │  DSI     │            │
│  │  3200    │   │  Ctrl    │   │  Ctrl    │  │  Ctrl    │            │
│  └──────────┘   └──────────┘   └──────────┘  └──────────┘            │
│                                                                         │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                           │
│  │  SPI0    │   │  I2C     │   │  DMA     │                           │
│  │  (bridge │   │  (periph │   │  (2ch    │                           │
│  │   to     │   │   bus)   │   │   cross- │                           │
│  │  RP2350B)│   │          │   │   req)   │                           │
│  └──────────┘   └──────────┘   └──────────┘                           │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 AXI4 Bandwidth Allocation

The AXI4 interconnect uses a NIC-400 network-on-chip with QoS arbitration. The following table defines the bandwidth allocation and QoS priorities:

| Master | Target Slave | Protocol | Peak BW | QoS Priority | Notes |
|---|---|---|---|---|---|
| A72 Cluster (L2) | LPDDR5 Controller | AXI4 | 25.6 GB/s (read) / 12.8 GB/s (write) | Highest (7) | Cache-line burst, 128-bit wide |
| A53 Cluster (L2) | LPDDR5 Controller | AXI4 | 12.8 GB/s | High (5) | Instruction fetch priority |
| NPU (6 TOPS) | LPDDR5 Controller | AXI4 | 12.8 GB/s | Medium-High (6) | Weight streaming, line buffers |
| Mali G52 GPU | LPDDR5 Controller | AXI4 | 12.8 GB/s | Medium (4) | Framebuffer + texture fetch |
| PCIe Gen3x2 Controller | LPDDR5 Controller | AXI4 | ~2.0 GB/s | Medium (4) | DMA transfers for NVMe |
| SPI0 Controller | RP2350B Bridge | APB→AXI | 50 MB/s | Low (2) | Low-latency command path |
| USB3.2 Gen1 | LPDDR5 Controller | AXI4 | ~400 MB/s | Medium (3) | Bulk transfers |
| MIPI DSI/CSI | LPDDR5 Controller | AXI4 | ~2.0 GB/s | High (5) | IQ data streaming from SDR |

### 2.3 Concurrent Bus Operation

The key design constraint is that the LPDDR5 controller, PCIe controller, and MIPI-CSI path must operate concurrently without head-of-line blocking:

1. **LPDDR5 Access:** The DDR controller uses a 32-bit wide bus at 3200 MHz DDR, providing 25.6 GB/s of raw bandwidth. With 8 banks and 16-bit prefetch, the controller can interleave refresh cycles with accesses from multiple AXI masters. The NIC-400 schedules round-robin among same-priority masters and strict priority across QoS levels.

2. **PCIe Gen3 x2 NVMe:** The M.2 2230 NVMe drive uses 2 lanes of PCIe Gen3 (2.0 GB/s raw, ~1.8 GB/s effective after 128b/130b encoding). The PCIe controller's DMA engine reads/writes directly to LPDDR5 via AXI, using posted write completions to minimize latency. The NVMe is used for PCAP storage and rainbow tables, requiring sustained ~500 MB/s sequential writes during packet capture.

3. **MIPI-CSI SDR IQ Path:** The LMS7002M transceiver streams IQ sample pairs via a MIPI-CSI-2 virtual channel to the RK3576. At the maximum sample rate of 61.44 MSPS with 12-bit I/Q, this requires ~1.47 Gbps of raw data, well within the MIPI-CSI-2 4-lane Gen1 capacity (8 Gbps). The RK3576's MIPI-CSI-2 receiver feeds a DMA ring buffer in LPDDR5, which the NPU or ARM cores can process.

### 2.4 SPI-Bridge Inter-Processor Communication

The RP2350B acts as a real-time coprocessor managing:
- **RF Frontend Control** — antenna switching (PE42422), SDR frequency tuning, CC1101 configuration
- **NFC/RFID** — ST25R3916 register programming and interrupt handling
- **Low-Latency Telemetry** — forwarding hardware interrupts to the RK3576

The SPI bridge uses a command-response protocol over the hardware SPI0 interface:

```
┌─────────────────┐                          ┌─────────────────┐
│     RK3576      │      SPI0 (50 MHz)        │    RP2350B      │
│   (Primary)     │◄────────────────────────►│  (Secondary)    │
│                 │                            │                 │
│  SPI0_SCK ──────┼── NET_SPI_CLK ──────────►│ PIN_18 (SCK)    │
│  SPI0_TX  ──────┼── NET_SPI_MOSI ─────────►│ PIN_19 (RX)     │
│  SPI0_RX  ◄─────┼── NET_SPI_MISO ──────────┤ PIN_16 (TX)     │
│  SPI0_CSn ──────┼── NET_SPI_CS   ─────────►│ PIN_17 (CSn)    │
│                 │                            │                 │
│  GPIO1_B0 ◄────┼── NET_INT_REQ  ───────────┤ PIN_20 (IRQ)    │
│                 │                            │                 │
│                 │      I2C1 (400 kHz)       │                 │
│  I2C1_SDA ◄────┼────────────────────────────►│ PIN_25 (SDA)   │
│  I2C1_SCL ──────┼────────────────────────────►│ PIN_26 (SCL)   │
└─────────────────┘                          └─────────────────┘
```

#### SPI Protocol Frame Format

Each SPI transaction consists of a 16-byte command frame, with optional data payload:

| Byte Offset | Field | Width | Description |
|---|---|---|---|
| 0 | SYNC | 1 | 0xAA (magic sync byte) |
| 1 | CMD | 1 | Command opcode (see table below) |
| 2-3 | LEN | 2 | Payload length (0–4092, big-endian) |
| 4-7 | RESERVED | 4 | Reserved for future use (set to 0) |
| 8-15 | HEADER_CRC | 8 | CRC-64 over bytes 0-7 |
| 16+ | PAYLOAD | 0-4092 | Variable-length data |
| 16+LEN | PAYLOAD_CRC | 4 | CRC-32 over payload |

#### Command Opcodes

| Opcode | Direction | Name | Payload | Description |
|---|---|---|---|---|
| 0x01 | Host→MCU | `CMD_SDR_TUNE` | 8 bytes: freq(4) + bw(2) + gain(2) | Set LMS7002M frequency, bandwidth, gain |
| 0x02 | Host→MCU | `CMD_SDR_STREAM` | 1 byte: enable(1) | Start/stop SDR IQ stream |
| 0x03 | Host→MCU | `CMD_ANT_SELECT` | 1 byte: ant_id(1) | Switch PE42422 antenna path |
| 0x04 | Host→MCU | `CMD_CC1101_CFG` | variable | Write CC1101 register block |
| 0x05 | Host→MCU | `CMD_NFC_TRANSACT` | variable | NFC read/write command |
| 0x81 | MCU→Host | `CMD_TELEMETRY` | 16 bytes | Periodic status: RSSI, temp, VBAT, flags |
| 0x82 | MCU→Host | `CMD_SDR_IQ_CHUNK` | variable | SDR IQ sample block (12-bit packed) |
| 0xFF | Both | `CMD_NOP` | 0 | Keepalive / SPI bus test |

### 2.5 Complete System Block Diagram

```
                    ┌──────────────────────────────────────────────────┐
                    │                   APEX ONE BOARD                    │
                    │                                                    │
 ┌─────────────┐    │  ┌─────────────────────────────────────────────┐  │
 │  SMA ANT 0  ├────┤  │                 RK3576 SoC                   │  │
 │  (MIMO TX)  │    │  │  ┌───────┐ ┌───────┐ ┌──────┐ ┌────────┐ │  │
 └─────────────┘    │  │  │4x A72 │ │4x A53 │ │NPU 6T│ │Mali G52│ │  │
 ┌─────────────┐    │  │  └───┬───┘ └───┬───┘ └──┬───┘ └───┬────┘ │  │
 │  SMA ANT 1  ├────┤  │      └─────────┴─────────┴─────────┘      │  │
 │  (MIMO RX)  │    │  │              AXI4 INTERCONNECT              │  │
 └──────┬──────┘    │  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────────┐ │  │
        │           │  │  │DDR5  │ │PCIe  │ │USB3  │ │MIPI-CSI  │ │  │
 ┌──────┴──────┐    │  │  │Ctrl  │ │Gen3  │ │2.0   │ │RX (SDR)  │ │  │
 │  LMS7002M   ├────┤  │  └──────┘ └──────┘ └──────┘ └────┬─────┘ │  │
 │  SDR XCVR   │    │  │                                    │       │  │
 │  (100kHz-   │    │  └──────────────────┬─────────────────┘       │  │
 │   3.8GHz)   │    │                     │ SPI0                    │  │
 └──────┬──────┘    │                     │                         │  │
        │           │  ┌──────────────────┴──────────────────────┐  │  │
 ┌──────┴──────┐    │  │            RP2350B Coprocessor           │  │  │
 │  PE42422    ├────┤  │  ┌─────────┐ ┌─────────────────────────┐ │  │  │
 │  ANT Switch │    │  │  │2x M33/  │ │ PIO0: SDR antenna ctrl  │ │  │  │
 └──────┬──────┘    │  │  │Hazard3  │ │ PIO1: CC1101 bit-bang   │ │  │  │
        │           │  │  └─────────┘ │ PIO2: ST25R3916 SPI     │ │  │  │
 ┌──────┴──────┐    │  │             │ SPI1: CC1101 config      │ │  │  │
 │  CC1101     ├────┤  │             │ I2C0: ST25R3916 control  │ │  │  │
 │  (sub-GHz)  │    │  │             │ UART0: debug console     │ │  │  │
 └─────────────┘    │  └─────────────┴─────────────────────────┴─┘  │  │
 ┌─────────────┐    │                                              │  │  │
 │  ST25R3916  ├────┤                                              │  │
 │  (NFC/RFID) │    │  ┌──────────┐  ┌──────────┐  ┌───────────┐ │  │
 └─────────────┘    │  │ 8GB      │  │ 32GB     │  │ M.2 2230  │ │  │
 ┌─────────────┐    │  │ LPDDR5   │  │ eMMC 5.1 │  │ NVMe SSD  │ │  │
 │  MT7922     ├────┤  │ (2x4Gb)  │  │ (OS Root)│  │ (PCAP/RT) │ │  │
 │  (Wi-Fi 6E/ │    │  └──────────┘  └──────────┘  └───────────┘ │  │
 │   BT 5.4)   │    │                                              │  │
 └─────────────┘    │  ┌──────────┐  ┌──────────┐                │  │
                    │  │PMIC:     │  │ 6.4"    │                │  │
                    │  │RK817     │  │ IPS LCD │                │  │
                    │  │(power    │  │1080x2400│                │  │
                    │  │ mgmt)    │  └──────────┘                │  │
                    │  └──────────┘                               │  │
                    │                                             │  │
                    │  ┌─────────────────────────────────────────┐ │  │
                    │  │ 5000 mAh Li-Po Battery                  │ │  │
                    │  │ (3.85V nominal, 19.25 Wh)              │ │  │
                    │  └─────────────────────────────────────────┘ │  │
                    └──────────────────────────────────────────────────┘
```

### 2.6 Power Domain Architecture

| Domain | Voltage | Source | Rail | Consumers |
|---|---|---|---|---|
| VDD_CORE (SoC) | 0.9V | RK817 BUCK1 | High-current | RK3576 A72/A53/NPU/GPU cores |
| VDD_LOGIC | 1.8V | RK817 BUCK2 | Medium-current | SoC VIO, eMMC, GPIO banks |
| VDD_DDR | 1.1V | RK817 BUCK3 | High-current | LPDDR5 VDD/VDDQ |
| VDD_3V3 | 3.3V | RK817 BUCK4 + LDO | Medium-current | RP2350B, CC1101, ST25R3916, SPI/I2C pullups |
| VDD_1V2_SDR | 1.2V | Dedicated LDO from RK817 | Low-current | LMS7002M core (LNA, mixer, PLL) |
| VDD_1V8_SDR | 1.8V | LDO from VDD_3V3 | Low-current | LMS7002M digital I/O |
| VDD_RF | 3.3V | Filtered LDO from VDD_3V3 | Ultra-low-noise | LMS7002M RF front-end, PE42422 |
| VDD_ANT_BIAS | 5.0V | Boost converter from VBAT | Low-current | Antenna bias-tee, external LNA power |
| VDD_NFC | 5.0V | Boost from VBAT | Medium-current | ST25R3916 antenna driver |
| VBAT_ALWAYS | 3.7-4.2V | Direct from battery | — | PMIC input, USB VBUS charging |

### 2.7 Boot Flow Overview

```
Power-On / Reset
      │
      ▼
┌──────────────┐
│  RK3576 Boot │   Internal ROM (24KB) loads SPL from eMMC boot0
│  ROM (Stage1)│   → verifies RSA-2048 signature of SPL
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  U-Boot SPL  │   Minimal bootloader: initializes LPDDR5, eMMC,
│  (Stage 2)   │   loads full U-Boot from eMMC boot1
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  U-Boot      │   Full U-Boot: USB recovery, network boot, DTS fixups
│  (Stage 3)   │   loads Linux kernel + initramfs from eMMC rootfs
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  Linux Kernel│   Device tree loaded, drivers initialized
│  (Stage 4)   │   apex_bridge SPI driver probes RP2350B
└──────┬───────┘   SDR/NFC/Wi-Fi drivers loaded
       │
       ▼
┌──────────────┐
│  Userspace   │   systemd starts: sdr_service, nfc_daemon,
│  (Stage 5)   │   wifi_monitor, web_ui, pentest framework
└──────────────┘
```

### 2.8 System Interrupt Architecture

| Priority | Source | IRQ | Destination | Description |
|---|---|---|---|---|
| Highest | RK3576 Secure Watchdog | SPI 0 | A72 Core 0 | System hang recovery |
| High | RK3576 GPU IRQ | SPI 48 | A72 Cluster | GPU job completion |
| High | RK3576 NPU IRQ | SPI 52 | A72 Core 0 | NPU inference done |
| High | RP2350B INT_REQ | GPIO1_B0 | A72 Core 0 | MCU requests attention |
| Medium | MIPI-CSI-2 (SDR IQ) | SPI 68 | A53 Cluster | SDR sample buffer ready |
| Medium | PCIe NVMe | SPI 72 | A53 Cluster | NVMe completion |
| Medium | USB3.2 OTG | SPI 80 | A53 Cluster | USB event |
| Low | I2C peripherals | SPI 88 | A53 Core 0 | ST25R3916, sensors |
| Low | UART debug | SPI 92 | A53 Core 0 | Console input |

---

## 3. Security Threat Model

Given this is a pentesting device, the device itself must be resilient:

| Threat | Mitigation |
|---|---|
| Boot chain tampering | Signed bootloader (RSA-2048), secure boot fuses in RK3576 eFUSE |
| SPI bus sniffing | Encrypted SPI frames (AES-128-CTR with session key exchanged at boot) |
| SDR unauthorized TX | Hardware TX-enable GPIO from RP2350B, requires explicit host command + confirmation |
| NFC relay attack | RP2350B enforces per-transaction timeouts, no persistent keys in MCU RAM |
| Physical extraction | eMMC and NVMe both support hardware encryption (AES-XTS), keys derived from user passphrase |
| Wi-Fi injection misuse | MT7922 monitor mode requires root + CAP_NET_ADMIN; RFKILL switch via GPIO |
| Cold boot attack | LPDDR5 self-refresh disabled on suspend; memory scrubbed on secure lock |