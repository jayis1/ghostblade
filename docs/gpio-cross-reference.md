<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# GhostBlade GPIO Pin Assignment Cross-Reference

**Project:** GhostBlade (Project NullSpectre)
**Version:** 1.0 — 2026-06-29

This document provides a cross-reference between the RK3576 device tree GPIO
assignments (in `software/dts/ghostblade-rk3576.dts`), the RP2350B firmware pin
definitions (in `firmware/rp2350b/include/board_pins.h`), and the schematic
net names (in `hardware/kicad/ghostblade.net`).

## RK3576 GPIO Assignments

| DTS Reference | GPIO Bank/Pin | Schematic Net | Function | DTS Property |
|---|---|---|---|---|
| GPIO1_A0 | gpio1 0 | NET_SPI_MOSI | SPI0 MOSI (host→MCU) | `spi0_pins` |
| GPIO1_A1 | gpio1 1 | NET_SPI_MISO | SPI0 MISO (MCU→host) | `spi0_pins` |
| GPIO1_A2 | gpio1 2 | NET_SPI_SCK | SPI0 clock | `spi0_pins` |
| GPIO1_A3 | gpio1 3 | NET_SPI_CSn | SPI0 chip select | `spi0_pins` |
| GPIO1_B0 | gpio1 8 | NET_GPIO_INT_REQ | MCU interrupt request | `apex,int-req-gpio` |
| GPIO1_B1 | gpio1 9 | NET_GPIO_HOST_RDY | Host ready signal | `apex,host-rdy-gpio` |
| GPIO1_B2 | gpio1 10 | NET_GPIO_MCU_RESET | MCU reset (active-low) | `apex,mcu-reset-gpio` |
| GPIO1_B4 | gpio1 12 | NET_GPIO_BOOTSEL | RP2350B BOOTSEL button | `button-bootsel` |
| GPIO1_B5 | gpio1 13 | NET_GPIO_USER_BTN | User button | `button-user` |
| GPIO1_B6 | gpio1 14 | NET_GPIO_LED_STATUS | Status LED (heartbeat) | `led-status` |
| GPIO1_B7 | gpio1 15 | NET_GPIO_LED_ACTIVITY | Activity LED (cpu0) | `led-activity` |
| GPIO1_C0 | gpio1 16 | NET_GPIO_LED_SDR_RX | SDR receive LED | `led-sdr-rx` |
| GPIO1_C1 | gpio1 17 | NET_GPIO_LNA_EN | LNA enable / antenna sel | `ant_sel_pins` |
| GPIO1_D4 | gpio1 20 | NET_GPIO_WIFI_IRQ | MT7922 Wi-Fi interrupt | `sdio_irq_pin` |
| GPIO1_D6 | gpio1 22 | NET_GPIO_BT_REG_ON | MT7922 BT power enable | `bluetooth` node |
| GPIO1_D7 | gpio1 23 | NET_GPIO_BT_HOST_WAKE | MT7922 BT host wake | `bluetooth` node |
| GPIO0_D7 | gpio0 31 | NET_GPIO_SD_CD | microSD card detect | `sdmmc0_det_pin` |

## RP2350B GPIO Assignments

| board_pins.h Define | Pin Number | Schematic Net | Function | Connected To |
|---|---|---|---|---|
| PIN_SPI0_RX | 16 | NET_SPI_MISO | SPI0 RX (slave) | RK3576 GPIO1_A1 |
| PIN_SPI0_CSN | 17 | NET_SPI_CSn | SPI0 chip select | RK3576 GPIO1_A3 |
| PIN_SPI0_SCK | 18 | NET_SPI_SCK | SPI0 clock | RK3576 GPIO1_A2 |
| PIN_SPI0_TX | 19 | NET_SPI_MOSI | SPI0 TX (slave) | RK3576 GPIO1_A0 |
| PIN_INT_REQ | 20 | NET_GPIO_INT_REQ | Interrupt to host | RK3576 GPIO1_B0 |
| PIN_HOST_RDY | 21 | NET_GPIO_HOST_RDY | Host ready signal | RK3576 GPIO1_B1 |
| PIN_MCU_RUN | 24 | NET_GPIO_MCU_RESET | MCU RUN/reset | RK3576 GPIO1_B2 |
| PIN_ANT_SEL0 | 2 | NET_ANT_SEL0 | Antenna switch bit 0 | PE42422 V1 |
| PIN_ANT_SEL1 | 3 | NET_ANT_SEL1 | Antenna switch bit 1 | PE42422 V2 |
| PIN_SDR_SPI_SCK | 27 | NET_SDR_SPI_SCK | LMS7002M SPI clock | LMS7002M pin |
| PIN_SDR_SPI_TX | 28 | NET_SDR_SPI_MOSI | LMS7002M SPI data in | LMS7002M pin |
| PIN_SDR_SPI_RX | 29 | NET_SDR_SPI_MISO | LMS7002M SPI data out | LMS7002M pin |
| PIN_SDR_SPI_CSN | 30 | NET_SDR_SPI_CSn | LMS7002M SPI chip select | LMS7002M pin |
| PIN_SDR_RESET | 31 | NET_SDR_RESET | LMS7002M reset (active-low) | LMS7002M pin |
| PIN_SDR_GPIO0 | 32 | NET_SDR_GPIO0 | LMS7002M TX enable | LMS7002M pin |
| PIN_SDR_GPIO1 | 33 | NET_SDR_GPIO1 | LMS7002M RX enable | LMS7002M pin |
| PIN_SDR_LNA_EN | 34 | NET_SDR_LNA_EN | LMS7002M LNA enable | LNA power control |
| PIN_CC_SPI_SCK | 8 | NET_CC_SPI_SCK | CC1101 SPI clock (shared SPI1) | CC1101 pin |
| PIN_CC_SPI_TX | 9 | NET_CC_SPI_MOSI | CC1101 SPI data in (shared SPI1) | CC1101 pin |
| PIN_CC_SPI_RX | 12 | NET_CC_SPI_MISO | CC1101 SPI data out (shared SPI1) | CC1101 pin |
| PIN_CC_SPI_CSN | 10 | NET_CC_SPI_CSn | CC1101 SPI chip select | CC1101 pin |
| PIN_CC_GDO0 | 13 | NET_CC_GDO0 | CC1101 GDO0 interrupt | CC1101 pin |
| PIN_CC_GDO2 | 14 | NET_CC_GDO2 | CC1101 GDO2 interrupt | CC1101 pin |
| PIN_NFC_SPI_SCK | 40 | NET_NFC_SPI_SCK | ST25R3916 SPI clock | ST25R3916 pin |
| PIN_NFC_SPI_TX | 41 | NET_NFC_SPI_MOSI | ST25R3916 SPI data in | ST25R3916 pin |
| PIN_NFC_SPI_RX | 42 | NET_NFC_SPI_MISO | ST25R3916 SPI data out | ST25R3916 pin |
| PIN_NFC_SPI_CSN | 43 | NET_NFC_SPI_CSn | ST25R3916 SPI chip select | ST25R3916 pin |
| PIN_NFC_IRQ | 44 | NET_NFC_IRQ | ST25R3916 interrupt | ST25R3916 pin |
| PIN_I2C_SDA | 46 | NET_I2C_SDA | I2C1 SDA to ST25R3916 | ST25R3916 pin |
| PIN_I2C_SCL | 47 | NET_I2C_SCL | I2C1 SCL to ST25R3916 | ST25R3916 pin |
| PIN_ADC_VBAT | 0 (ADC0) | NET_ADC_VBAT | Battery voltage (divider) | Voltage divider |
| PIN_ADC_TEMP | 4 (ADC4) | — | Internal die temperature | RP2350B internal |
| PIN_UART_TX | 0 | NET_UART_TX | Debug UART TX | TP23 |
| PIN_UART_RX | 1 | NET_UART_RX | Debug UART RX | TP24 |

## Antenna Switch Truth Table (PE42422 SP4T)

| PIN_ANT_SEL0 | PIN_ANT_SEL1 | RF Path | Description |
|---|---|---|---|
| 0 | 0 | RF1 (SMA_ANT0) | MIMO TX path |
| 1 | 0 | RF2 (SMA_ANT1) | MIMO RX path |
| 0 | 1 | RF3 (u.FL) | Sub-GHz CC1101 path |
| 1 | 1 | RF4 (terminated) | 50Ω terminated (noise calibration) |

## SPI Bus Arbitration

The RP2350B manages two SPI buses:

| SPI Bus | Master/Slave | Devices | Max Clock | Protocol |
|---|---|---|---|---|
| SPI0 | Slave (RK3576 master) | RK3576 ↔ RP2350B | 50 MHz | CRC-64/CRC-32 framed |
| SPI1 | Master (RP2350B master) | LMS7002M, CC1101 | 50/10 MHz | Standard SPI Mode 0 |
| SPI2 | Master (RP2350B master) | ST25R3916 | 10 MHz | Standard SPI Mode 0 |

SPI1 bus sharing between LMS7002M and CC1101:
- LMS7002M CSn (PIN_SDR_SPI_CSN, pin 30) and CC1101 CSn (PIN_CC_SPI_CSN, pin 10) are independent
- Only one device is addressed at a time via its respective CSn
- The RP2350B firmware arbitrates access; no bus contention is possible

## Power Rail Enable GPIO Mapping

| RP2350B GPIO Pin | Net Name | Function | Enable Delay |
|---|---|---|---|
| 28 (GPIO28) | NET_SDR_1V8_EN | LMS7002M 1V8 rail enable | 0 ms (first SDR rail) |
| 29 (GPIO29) | NET_SDR_1V1_EN | LMS7002M 1V1 rail enable | ≥ 5 ms after 1V8 |
| 30 (GPIO30) | NET_SDR_3V3_EN | LMS7002M 3V3 RF rail enable | ≥ 5 ms after 1V1 |
| 31 (GPIO31) | NET_CC_3V3_EN | CC1101 3V3 rail enable | ≥ 50 ms after SDR rails |

Note: GPIO28-GPIO31 in the RP2350B pin mux correspond to power enable pins,
not the same as board_pins.h signal pins (PIN_SDR_SPI_CSN=30, PIN_SDR_RESET=31).

---

*Cross-reference version: 1.0 — 2026-06-29*