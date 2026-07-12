<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade — Glossary

**Project:** GhostBlade (Project NullSpectre)
**Version:** 1.0 — 2026-06-29

This glossary defines project-specific terms, acronyms, and abbreviations used
throughout the GhostBlade documentation.

## Processor and IC Terms

| Term | Definition |
|------|-----------|
| **RK3576** | Rockchip SoC — the main Linux host processor (4× Cortex-A72 + 4× Cortex-A53, ARM Mali-G52 GPU, 6 TOPS NPU) |
| **RP2350B** | Raspberry Pi RP2350B — ARM Cortex-M33F + Hazard3 RISC-V dual-core MCU coprocessor |
| **LMS7002M** | Lime Microsystems LMS7002M — full-duplex SDR transceiver (100 kHz – 3.8 GHz) |
| **CC1101** | Texas Instruments CC1101 — sub-GHz RF transceiver (433/868/915 MHz ISM bands) |
| **ST25R3916** | STMicroelectronics ST25R3916 — NFC reader/writer controller (13.56 MHz, ISO 14443A/B) |
| **MT7922** | MediaTek MT7922 — Wi-Fi 6E + Bluetooth 5.3 combo module |
| **RK817** | Rockchip RK860-2 PMIC — power management IC for the RK3576 |
| **PE42422** | pSemi (Murata) PE42422 — SP4T RF switch for antenna selection |

## Bus and Interface Terms

| Term | Definition |
|------|-----------|
| **SPI0** | The primary SPI bus between RK3576 (master) and RP2350B (slave) — carries the bridge protocol |
| **SPI1** | The RP2350B master SPI bus shared by LMS7002M and CC1101 |
| **SPI2** | The RP2350B master SPI bus for ST25R3916 NFC controller |
| **I2C0** | RP2350B I2C bus for ST25R3916 secondary control (slave address 0x42 when viewed from RK3576) |
| **I2C1** | RK3576 I2C bus for RP2350B telemetry (slave address 0x42) |
| **MIPI-CSI-2** | MIPI Camera Serial Interface v2 — repurposed for SDR IQ data from LMS7002M to RK3576 |
| **UART0** | RK3576 debug console (115200 8N1) |
| **UART1** | RK3576 Bluetooth HCI UART to MT7922 (3 Mbps) |

## Software Terms

| Term | Definition |
|------|-----------|
| **apex_bridge** | Linux kernel driver that provides `/dev/apex_bridge0` and sysfs attributes for the SPI bridge |
| **libapex** | Userspace C library wrapping the apex_bridge ioctl interface |
| **pyapex** | Python bindings for libapex |
| **apex-ctl** | Command-line utility for managing the GhostBlade device |
| **SPI bridge protocol** | The CRC-64/CRC-32 framed protocol used between RK3576 and RP2350B over SPI0 |
| **SG DMA** | Scatter-gather DMA engine in the kernel driver for high-throughput SDR IQ streaming |
| **MCU_READY** | Signal from RP2350B to RK3576 indicating the MCU firmware has completed initialization |
| **INT_REQ** | Active-low interrupt signal from RP2350B to RK3576 (GPIO1_B0) requesting SPI read |
| **HOST_RDY** | Active-low signal from RK3576 to RP2350B (GPIO1_B1) indicating the host driver is ready |

## Power and Hardware Terms

| Term | Definition |
|------|-----------|
| **VDD_CORE** | 0.9V core voltage rail for RK3576 (2A max) |
| **VDD_3V3** | 3.3V system voltage rail (3A max) |
| **VDD_1V8** | 1.8V logic voltage rail (500mA max) |
| **VDD_1V8_SDR** | 1.8V SDR analog voltage rail (LMS7002M AVDD) |
| **VDD_1V2_SDR** | 1.2V SDR digital core voltage rail (LMS7002M DVDD) |
| **VDD_RF** | 3.3V SDR RF voltage rail (LMS7002M LNA/VCO/PA) |
| **VDD_5V_NFC** | 5.0V NFC transmitter supply (ST25R3916 TX driver) |
| **BOOTSEL** | RP2350B boot mode selection button — hold during reset to enter USB bootloader |
| **MCU_RESET** | Active-low reset signal from RK3576 GPIO1_B2 to RP2350B RUN pin |
| **TP** | Test point — plated-through hole for voltage probing and debug |

## SDR and RF Terms

| Term | Definition |
|------|-----------|
| **IQ data** | In-phase and Quadrature sample pairs from the SDR — the fundamental output format |
| **I16Q16** | IQ format: 16-bit signed integer per I and Q sample (4 bytes/sample, default) |
| **I12Q12** | IQ format: 12-bit signed integers packed into 3 bytes/sample |
| **I8Q8** | IQ format: 8-bit signed integers, 2 bytes/sample (compact, reduced dynamic range) |
| **MIMO** | Multiple-Input Multiple-Output — LMS7002M supports 2×2 MIMO |
| **LNA** | Low-Noise Amplifier — the first amplifier stage in an RF receive path |
| **RSSI** | Received Signal Strength Indicator — measured in dBm |
| **Pentesting** | Penetration testing — the primary use case for the GhostBlade device |

## Document Conventions

- All voltages are nominal unless specified with tolerance (e.g., "3.3V ±3%")
- All GPIO numbers use the RK3576 GPIO bank notation (e.g., GPIO1_B2 = bank 1, pin 10)
- RP2350B pins use the physical pin number (e.g., pin 20 = PIN_INT_REQ)
- All timing values are typical unless noted as minimum or maximum
- Register values are hexadecimal unless noted otherwise
- Temperatures are in degrees Celsius (°C)

---

*Glossary version: 1.0 — 2026-06-29*