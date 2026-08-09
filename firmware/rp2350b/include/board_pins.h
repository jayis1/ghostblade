/*
 * board_pins.h — GhostBlade RP2350B Board Pin Definitions
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Pin assignments for the RP2350B coprocessor on the GhostBlade board.
 * All pin numbers refer to the RP2350B physical pin numbering.
 *
 * Reference: Phase 2 Schematic (component-selection-and-schematics.md)
 *           Phase 1 Architecture (architecture-and-requirements.md)
 */

#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ========================================================================
 * SPI0 Slave — Bridge to RK3576
 * SPI Mode 0, up to 50 MHz, framed CRC-64/CRC-32 protocol
 * ======================================================================== */

#define PIN_SPI0_RX        16   /* RP2350B SPI0 RX  (MISO from host perspective) */
#define PIN_SPI0_CSN       17   /* RP2350B SPI0 CSn (active-low chip select) */
#define PIN_SPI0_SCK       18   /* RP2350B SPI0 SCK (clock from RK3576) */
#define PIN_SPI0_TX        19   /* RP2350B SPI0 TX  (MOSI from host perspective) */

/* ========================================================================
 * Interrupt and Control Signals — RK3576 ↔ RP2350B
 * ======================================================================== */

#define PIN_INT_REQ        20   /* MCU interrupt request to RK3576 (active-low) */
#define PIN_HOST_RDY       21   /* Host ready signal from RK3576 (active-low) */
#define PIN_MCU_RUN        24   /* MCU RUN/RESET from RK3576 (active-low reset) */

/* ========================================================================
 * PE42422 Antenna Switch — 2-bit parallel control
 * V1/V2 select: 00=RF1(MIMO TX), 01=RF2(MIMO RX), 10=RF3(Sub-GHz), 11=RF4(terminated)
 * ======================================================================== */

#define PIN_ANT_SEL0       2    /* PE42422 V1 control bit 0 */
#define PIN_ANT_SEL1       3    /* PE42422 V2 control bit 1 */

/* ========================================================================
 * SDR Control — LMS7002M via SPI1
 * SPI Mode 0, up to 50 MHz
 * ======================================================================== */

#define PIN_SDR_SPI_SCK   27   /* LMS7002M SPI clock */
#define PIN_SDR_SPI_TX    28   /* LMS7002M SPI data in (MOSI) */
#define PIN_SDR_SPI_RX    29   /* LMS7002M SPI data out (MISO) */
#define PIN_SDR_SPI_CSN   30   /* LMS7002M SPI chip select (active-low) */
#define PIN_SDR_RESET      31   /* LMS7002M reset (active-low) */
#define PIN_SDR_GPIO0      32   /* LMS7002M GPIO0: TX enable */
#define PIN_SDR_GPIO1      33   /* LMS7002M GPIO1: RX enable */
#define PIN_SDR_LNA_EN    34   /* LMS7002M LNA enable (active-high) */

/* ========================================================================
 * CC1101 Sub-GHz Radio — via SPI2 bus
 * SPI Mode 0, up to 10 MHz
 * ======================================================================== */

#define PIN_CC_SPI_SCK     8   /* CC1101 SPI clock (SPI2 bus) */
#define PIN_CC_SPI_TX      9   /* CC1101 SPI data in (MOSI) */
#define PIN_CC_SPI_RX     12   /* CC1101 SPI data out (MISO) */
#define PIN_CC_SPI_CSN    10   /* CC1101 SPI chip select (active-low) */
#define PIN_CC_GDO0       13   /* CC1101 GDO0: FIFO threshold / sync detect */
#define PIN_CC_GDO2       14   /* CC1101 GDO2: packet received / TX done */

/* ========================================================================
 * ST25R3916 NFC Controller — via SPI2 + I2C1
 * SPI Mode 0, up to 10 MHz; I2C address 0xAC
 * ======================================================================== */

#define PIN_NFC_SPI_SCK   40   /* ST25R3916 SPI clock */
#define PIN_NFC_SPI_TX    41   /* ST25R3916 SPI MOSI */
#define PIN_NFC_SPI_RX    42   /* ST25R3916 SPI MISO */
#define PIN_NFC_SPI_CSN   43   /* ST25R3916 SPI chip select (active-low) */
#define PIN_NFC_IRQ       44   /* ST25R3916 interrupt (active-low) */

/* ========================================================================
 * I2C — Secondary NFC Control Bus
 * 400 kHz Fast-Mode Plus
 * ======================================================================== */

#define PIN_I2C_SDA       46   /* I2C1 SDA to ST25R3916 */
#define PIN_I2C_SCL       47   /* I2C1 SCL to ST25R3916 */

/* ========================================================================
 * ADC Channels
 * 12-bit, 500 kS/s maximum
 * ======================================================================== */

#define PIN_ADC_VBAT       0   /* ADC channel 0: battery voltage (divider) */
#define PIN_ADC_TEMP       4   /* ADC channel 4: internal die temperature */

/* ========================================================================
 * UART0 — Debug Console
 * 115200 8N1, primary debug output
 * ======================================================================== */

#define PIN_UART_TX        0   /* UART0 TX (debug console) */
#define PIN_UART_RX        1   /* UART0 RX (debug console) */

#endif /* BOARD_PINS_H */