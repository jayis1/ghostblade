# Memory Map & Register Reference — GhostBlade

This document provides a comprehensive reference for the memory-mapped I/O
registers, SPI bridge protocol structures, and ioctl interfaces used in the
GhostBlade system. It consolidates information from the kernel driver headers
and firmware source into a single reference.

## Table of Contents

- [RK3576 SPI0 Controller (0xFE610000)](#rk3576-spi0-controller-0xfe610000)
- [RK3576 GPIO1 Bank (0xFD510000)](#rk3576-gpio1-bank-0xfd510000)
- [SPI Bridge GPIO Pin Assignments](#spi-bridge-gpio-pin-assignments)
- [SPI Bridge Protocol Constants](#spi-bridge-protocol-constants)
- [SPI Frame Format](#spi-frame-format)
- [Command Opcodes](#command-opcodes)
- [Telemetry Data Structure](#telemetry-data-structure)
- [DMA Scatter-Gather Engine](#dma-scatter-gather-engine)
- [ioctl Interface (/dev/apex_bridge0)](#ioctl-interface-devapex_bridge0)
- [Antenna Path Selection](#antenna-path-selection)
- [Cross-Reference: Register ↔ DTS ↔ Firmware](#cross-reference-register--dts--firmware)

---

## RK3576 SPI0 Controller (0xFE610000)

The RK3576 SPI0 controller is the master side of the SPI bridge to the
RP2350B. All registers are 32-bit, little-endian.

### Control Register 0 (CR0) — Offset 0x0000

| Bits | Field | Description |
|------|-------|-------------|
| [7:0] | CLK_DIV | Clock divider: `f_spi = f_peri / (2 × (CLK_DIV + 1))` |
| 8 | SPH | Clock Phase (CPHA=1 for Mode 0/2) |
| 9 | SPO | Clock Polarity (CPOL=1 for Mode 2/3) |
| 10 | CSM_KEEP | CS keep after transfer (1 = hold CS low between frames) |
| [12:11] | CSM | CS inactive time: 0=1clk, 1=2clk, 2=3clk |
| [15:14] | DFS | Data frame size: 0=8bit, 1=16bit, 2=32bit |
| 16 | BHT_8BIT | Byte-hold time for 8-bit frames |
| [19:18] | XFM | Transfer mode: 0=TX, 1=RX, 2=TXRX (full duplex) |
| 20 | OPM_MASTER | Master mode enable |
| 21 | MBM_DMA | DMA multi-bit mode enable |
| [23:22] | RSD | Read stall delay: 0=1clk, 1=2clk |
| 24 | SSD | Slave select polarity: 0=active-low, 1=active-high |
| 25 | SSE_ENABLE | SPI enable bit |
| 26 | BEN_MSB | Bit order: 1=MSB first |

### Predefined Clock Dividers

| Divider | Peripheral Clock | SPI Clock | Use Case |
|---------|-----------------|-----------|----------|
| 6 | 300 MHz | ~21.4 MHz | Default SPI bridge clock |
| 12 | 300 MHz | ~11.5 MHz | Debug / reduced speed |

### Control Register 1 (CR1) — Offset 0x0004

| Bits | Field | Description |
|------|-------|-------------|
| [15:0] | NDF | Number of data frames for RX-only transfers |

### Slave Enable Register (SER) — Offset 0x0008

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | CS0 | Chip select 0 enable |
| 1 | CS1 | Chip select 1 enable |

### Status Register (SR) — Offset 0x000C

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | DCOL | Data collision error |
| 1 | TXE | TX error |
| 2 | RXO | RX overrun |
| 3 | TXO | TX overrun |
| 4 | BUSY | SPI busy (transfer in progress) |
| 5 | TFE | TX FIFO empty |
| 6 | TFNF | TX FIFO not full |
| 7 | RFNE | RX FIFO not empty |
| 8 | RFF | RX FIFO full |

### Interrupt Registers

| Register | Offset | Description |
|----------|--------|-------------|
| IMR | 0x0010 | Interrupt mask register |
| ISR | 0x0014 | Interrupt status register |

| Bit | IMR/ISR Field | Description |
|-----|---------------|-------------|
| 0 | TXEIM/TXEIS | TX empty interrupt |
| 1 | TXOIM/TXOIS | TX overrun interrupt |
| 2 | RXUIM/RXUIS | RX underrun interrupt |
| 3 | RXOIM/RXOIS | RX overrun interrupt |
| 4 | MIM/MIS | Multi-master interrupt |
| 5 | RIM/RIS | RX full interrupt |

### Data and FIFO Registers

| Register | Offset | Description |
|----------|--------|-------------|
| DR | 0x0060 | Data register (read/write, 32-bit) |
| TXFLR | 0x0074 | TX FIFO level register |
| RXFLR | 0x0078 | RX FIFO level register |

### DMA Registers

| Register | Offset | Description |
|----------|--------|-------------|
| RXDMA | 0x0100 | RX DMA control (bit 0: enable) |
| TXDMA | 0x0104 | TX DMA control (bit 0: enable) |
| DMARDLR | 0x0118 | RX DMA request data level |
| DMATDLR | 0x011C | TX DMA request data level |

---

## RK3576 GPIO1 Bank (0xFD510000)

The GPIO1 bank is used for SPI bridge control signals.

| Register | Offset | Description |
|----------|--------|-------------|
| SWPORTA_DR | 0x0000 | Port A data register |
| SWPORTA_DDR | 0x0004 | Port A data direction |
| SWPORTB_DR | 0x000C | Port B data register |
| SWPORTB_DDR | 0x0010 | Port B data direction |
| INTEN | 0x0030 | Interrupt enable |
| INTMASK | 0x0034 | Interrupt mask |
| INTTYPE | 0x0038 | Interrupt type |
| INT_POLARITY | 0x003C | Interrupt polarity |
| INT_STATUS | 0x0040 | Interrupt status |
| INT_RAW_STATUS | 0x0044 | Raw interrupt status |
| DEBOUNCE | 0x0048 | Debounce enable |
| EOI | 0x004C | End of interrupt |
| PORTA_EOI | 0x0050 | Port A end of interrupt |

---

## SPI Bridge GPIO Pin Assignments

### GPIO1 Port A — SPI0 Data Signals

| Bit | Pin | Direction | Function |
|-----|-----|-----------|----------|
| 0 | GPIO1_A0 | Output | SPI0_MOSI (host → MCU) |
| 1 | GPIO1_A1 | Input | SPI0_MISO (MCU → host) |
| 2 | GPIO1_A2 | Output | SPI0_SCK (clock) |
| 3 | GPIO1_A3 | Output | SPI0_CSn (chip select, active-low) |

### GPIO1 Port B — Bridge Control Signals

| Bit | Pin | Direction | Function |
|-----|-----|-----------|----------|
| 8 | GPIO1_B0 | Input | INT_REQ (MCU interrupt request, active-low) |
| 9 | GPIO1_B1 | Output | HOST_RDY (host ready, active-low) |
| 10 | GPIO1_B2 | Output | MCU_RESET (MCU reset, active-low) |

---

## SPI Bridge Protocol Constants

| Constant | Value | Description |
|----------|-------|-------------|
| SYNC_BYTE | 0xAA | Frame sync byte |
| HDR_SIZE | 16 | Header size in bytes |
| MAX_PAYLOAD | 4092 | Maximum payload size in bytes |
| CRC32_SIZE | 4 | CRC-32 trailer size in bytes |
| FRAME_SIZE_MAX | 4112 | Maximum total frame size (16 + 4092 + 4) |
| MIN_FRAME_SIZE | 20 | Minimum frame size (16 + 4, no payload) |

---

## SPI Frame Format

```
Offset  Size  Field
0x00    1     SYNC (0xAA)
0x01    1     CMD (command opcode)
0x02    2     LEN_LO (payload length, low byte, little-endian)
0x04    4     RESERVED (0x00000000)
0x08    8     HDR_CRC (CRC-64/ECMA-182 reflected over bytes 0–7)
0x10    N     PAYLOAD (0 to 4092 bytes)
0x10+N  4     CRC32 (CRC-32/ISO-3309 over payload, little-endian)
```

### CRC Algorithms

| Algorithm | Usage | Polynomial | Init | Final XOR |
|-----------|-------|-----------|------|-----------|
| CRC-64 | Header integrity | 0x42F0E1EBA9EA3693 (ECMA-182, reflected) | 0xFFFFFFFFFFFFFFFF | 0xFFFFFFFFFFFFFFFF |
| CRC-32 | Payload integrity | 0xEDB88320 (ISO 3309, reflected) | 0xFFFFFFFF | 0xFFFFFFFF |

> **Note:** The CRC-64 implementation uses a reflected (LSB-first) computation.
> The check value for "123456789" with this reflected variant is
> `0xB86883E6FA710A9F`, not the standard ECMA-182 MSB-first value
> `0x6C40DF5F0B497347`.

---

## Command Opcodes

### Host → MCU Commands

| Opcode | Name | Payload Size | Description |
|--------|------|-------------|-------------|
| 0xFF | NOP | 0 | No operation (keepalive / ping) |
| 0x01 | SDR_TUNE | 8 | Configure SDR frequency, bandwidth, gain |
| 0x02 | SDR_STREAM | 1 | Start (1) or stop (0) SDR IQ streaming |
| 0x03 | ANT_SELECT | 1 | Select antenna path (0–3) |
| 0x04 | CC1101_CFG | 2+ | Configure CC1101 sub-GHz radio |
| 0x05 | NFC_TRANSACT | 4+ | Execute NFC transaction |
| 0x06 | TELEMETRY_REQ | 0 | Request telemetry data from MCU |

### MCU → Host Commands

| Opcode | Name | Payload Size | Description |
|--------|------|-------------|-------------|
| 0x81 | TELEMETRY | 16 | Telemetry response (RSSI, temp, battery, flags) |
| 0x82 | SDR_IQ_CHUNK | variable | IQ sample data chunk |

MCU → Host commands have bit 7 set (`0x80`). Host → MCU commands have bit 7
clear (except NOP = `0xFF`).

---

## Telemetry Data Structure

The `TELEMETRY` (0x81) response carries a 16-byte payload:

| Offset | Size | Field | Unit | Description |
|--------|------|-------|------|-------------|
| 0x00 | 2 | rssi_dbm_x10 | 0.1 dBm | SDR RSSI (−120.0 to 0.0 dBm) |
| 0x02 | 2 | temp_c_x10 | 0.1 °C | MCU die temperature (−40.0 to 85.0 °C) |
| 0x04 | 2 | vbat_mv | mV | Battery voltage (2800–4200 mV) |
| 0x06 | 2 | cc1101_rssi_x10 | 0.1 dBm | CC1101 RSSI (−120.0 to 0.0 dBm) |
| 0x08 | 2 | nfc_field_mv | mV | NFC 13.56 MHz field strength |
| 0x0A | 2 | flags | bitmap | MCU status flags |
| 0x0C | 4 | uptime_ms | ms | MCU uptime (wraps at ~49.7 days) |

### Telemetry Flags Bitmap

| Bit | Name | Description |
|-----|------|-------------|
| 0 | SDR_RX_ACTIVE | SDR receiver is active |
| 1 | SDR_TX_ACTIVE | SDR transmitter is active |
| 2 | CC1101_RX | CC1101 is receiving |
| 3 | CC1101_TX | CC1101 is transmitting |
| 4 | NFC_ACTIVE | NFC field is on |
| 5 | NFC_TAG_PRESENT | NFC tag detected in field |
| 6 | OVERTEMP | MCU temperature > 85 °C |
| 7 | LOW_BATTERY | Battery voltage < 3.3 V |
| 8 | SPI_ERR | SPI protocol error detected |
| 9 | DMA_ERR | DMA transfer error detected |

---

## DMA Scatter-Gather Engine

The SG DMA engine enables high-throughput SDR IQ streaming without
intermediate copies. See `docs/sysfs-attributes.md` for runtime statistics.

### Configuration Structure (apex_sg_config)

| Field | Size | Description |
|-------|------|-------------|
| buf_count | 4 bytes | Number of SG buffers (2–64) |
| buf_size | 4 bytes | Size of each buffer in bytes (4 KiB–256 KiB, 4-byte aligned) |
| timeout_ms | 4 bytes | DMA completion timeout (0 = infinite) |
| spi_speed_hz | 4 bytes | SPI clock for SG transfers (0 = use default) |
| continuous | 1 byte | 0 = single batch, 1 = continuous streaming |
| reserved | 3 bytes | Must be zero |

### Buffer States

| State | Value | Description |
|-------|-------|-------------|
| FREE | 0 | Available for DMA |
| DMA_ACTIVE | 1 | Currently being filled by DMA |
| READY | 2 | Filled, waiting for userspace read |
| USERSPACE | 3 | Currently mapped by userspace |

### Engine States

| State | Value | Description |
|-------|-------|-------------|
| IDLE | 0 | Not streaming |
| RUNNING | 1 | Actively streaming IQ data |
| ERROR | 2 | Error state, needs reset |

---

## ioctl Interface (/dev/apex_bridge0)

The `apex_bridge` kernel driver exposes the following ioctl commands:

| ioctl | Direction | Payload | Description |
|-------|-----------|---------|-------------|
| `APEX_IOC_SDR_TUNE` | IOW | `apex_sdr_tune_cmd` (8 bytes) | Configure SDR frequency/BW/gain |
| `APEX_IOC_SDR_STREAM` | IOW | `__u8` (1 byte) | Start (1) or stop (0) SDR stream |
| `APEX_IOC_ANT_SELECT` | IOW | `__u8` (1 byte) | Select antenna path (0–3) |
| `APEX_IOC_CC1101_CFG` | IOW | `apex_cc1101_cfg` | Configure CC1101 registers |
| `APEX_IOC_NFC_TRANSACT` | IOW | `apex_nfc_transact` | Execute NFC transaction |
| `APEX_IOC_GET_TELEMETRY` | IOR | `apex_telemetry` (16 bytes) | Read MCU telemetry |
| `APEX_IOC_MCU_RESET` | IOW | `__u8` (1 byte) | Assert (1) or deassert (0) MCU reset |
| `APEX_IOC_GET_STATUS` | IOR | `__u32` (4 bytes) | Read driver status flags |
| `APEX_IOC_SG_START` | IOW | `apex_sg_config` | Start SG DMA engine |
| `APEX_IOC_SG_STOP` | IO | — | Stop SG DMA engine |
| `APEX_IOC_SG_GET_STATUS` | IOR | `apex_sg_status` | Read SG engine status |

### SDR Tune Command (apex_sdr_tune_cmd)

| Field | Size | Description |
|-------|------|-------------|
| freq_hz | 4 bytes | Frequency in Hz (100 kHz – 3.8 GHz) |
| bw_khz | 2 bytes | Bandwidth in kHz |
| gain_db_x10 | 2 bytes | LNA gain in dB × 10 |

### CC1101 Configuration (apex_cc1101_cfg)

| Field | Size | Description |
|-------|------|-------------|
| reg_addr | 1 byte | CC1101 register start address |
| reg_len | 1 byte | Number of consecutive registers |
| data | variable | Register data (1–64 bytes) |

### NFC Transaction (apex_nfc_transact)

| Field | Size | Description |
|-------|------|-------------|
| cmd | 1 byte | NFC command (ISO 14443 A/B, etc.) |
| flags | 1 byte | Transaction flags |
| data_len | 2 bytes | TX data length |
| data | variable | TX data + RX buffer hint (1–256 bytes) |

---

## Antenna Path Selection

The `APEX_IOC_ANT_SELECT` ioctl selects the PE42422 antenna switch path:

| Value | Path | Connected To | Description |
|-------|------|---------------|-------------|
| 0 | RF1 | SMA_ANT0 | MIMO TX (LMS7002M TX path) |
| 1 | RF2 | SMA_ANT1 | MIMO RX (LMS7002M RX path) |
| 2 | RF3 | u.FL | Sub-GHz (CC1101 via RF port) |
| 3 | RF4 | 50Ω terminated | Load / calibration |

The RP2350B firmware controls the PE42422 via GPIO pins `PIN_ANT_SEL0` (PIN_2)
and `PIN_ANT_SEL1` (PIN_3).

---

## Cross-Reference: Register ↔ DTS ↔ Firmware

| Register / Constant | DTS Node/Property | Firmware Macro | Source File |
|---------------------|-------------------|----------------|-------------|
| `RK3576_SPI0_BASE` | `spi0` node | — | `apex_bridge_regs.h` |
| `RK3576_GPIO1_BASE` | `gpio1` node | — | `apex_bridge_regs.h` |
| `APEX_GPIO_SPI_CS` | `spi0_pins` CS | `PIN_SPI0_CSN` | `apex_bridge_regs.h`, `board_pins.h` |
| `APEX_GPIO_INT_REQ` | `bridge_gpio_pins` | `PIN_INT_REQ` | `apex_bridge_regs.h`, `board_pins.h` |
| `APEX_GPIO_HOST_READY` | `bridge_gpio_pins` | `PIN_HOST_RDY` | `apex_bridge_regs.h`, `board_pins.h` |
| `APEX_GPIO_MCU_RESET` | `bridge_gpio_pins` | `PIN_MCU_RUN` | `apex_bridge_regs.h`, `board_pins.h` |
| SDR SPI1 pins | `spi1_pins` | `PIN_SDR_SPI_*` | `ghostblade-rk3576.dts`, `board_pins.h` |
| CC1101 SPI pins | (shared SPI1) | `PIN_CC_SPI_*` | `ghostblade-sdr-overlay.dts`, `board_pins.h` |
| NFC SPI2 pins | `spi2_pins` | `PIN_NFC_SPI_*` | `ghostblade-nfc-overlay.dts`, `board_pins.h` |
| Antenna select | `ant_sel_pins` | `PIN_ANT_SEL0/1` | `ghostblade-rk3576.dts`, `board_pins.h` |

For a complete pin-by-pin cross-reference, see [Pin Assignments](pin-assignments.md).

---

*Last updated: 2026-06-24. Derived from `apex_bridge_regs.h`, `board_pins.h`, `ghostblade-rk3576.dts`, and `spi_protocol.h`.*