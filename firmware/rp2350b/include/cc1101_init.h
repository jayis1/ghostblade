/*
 * cc1101_init.h — CC1101 Sub-GHz Radio Initialization and Control API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Provides initialization, configuration, and runtime control for the
 * CC1101 sub-GHz transceiver on the GhostBlade board. The CC1101 is
 * connected via SPI1 (shared with LMS7002M, separate chip select) and
 * supports 300–928 MHz operation with GFSK/2-FSK/ASK/MSK modulation.
 *
 * Default configuration: 868 MHz ISM, GFSK, 250 kbps, 0 dBm TX power.
 */

#ifndef CC1101_INIT_H
#define CC1101_INIT_H

#include <stdint.h>

/* ── CC1101 register addresses (configuration registers) ─────────────────── */

#define CC1101_IOCFG2       0x00    /**< GDO2 output configuration */
#define CC1101_IOCFG1       0x01    /**< GDO1 output configuration */
#define CC1101_IOCFG0       0x02    /**< GDO0 output configuration */
#define CC1101_FIFOTHR      0x03    /**< RX/TX FIFO threshold */
#define CC1101_SYNC0        0x04    /**< Sync word, low byte */
#define CC1101_SYNC1        0x05    /**< Sync word, high byte */
#define CC1101_PKTLEN       0x06    /**< Packet length */
#define CC1101_PKTCTRL0     0x07    /**< Packet automation control */
#define CC1101_PKTCTRL1     0x08    /**< Packet automation control */
#define CC1101_ADDR         0x09    /**< Device address */
#define CC1101_CHANNR       0x0A    /**< Channel number */
#define CC1101_FSCTRL1      0x0B    /**< Frequency synthesizer control */
#define CC1101_FREQ2        0x0C    /**< Frequency control word, high byte */
#define CC1101_FREQ1        0x0D    /**< Frequency control word, middle byte */
#define CC1101_FREQ0        0x0E    /**< Frequency control word, low byte */
#define CC1101_MDMCFG4      0x10    /**< Modem configuration */
#define CC1101_MDMCFG3      0x11    /**< Modem configuration */
#define CC1101_MDMCFG2      0x12    /**< Modem configuration */
#define CC1101_MDMCFG1      0x13    /**< Modem configuration */
#define CC1101_MDMCFG0      0x14    /**< Modem configuration */
#define CC1101_DEVIATN      0x15    /**< Modem deviation setting */
#define CC1101_MCSM1        0x17    /**< Main radio control state machine */
#define CC1101_MCSM0        0x18    /**< Main radio control state machine */
#define CC1101_FOCCFG       0x19    /**< Frequency offset compensation */
#define CC1101_BSCFG        0x1A    /**< Bit synchronization config */
#define CC1101_AGCCTRL2     0x1B    /**< AGC control */
#define CC1101_AGCCTRL1     0x1C    /**< AGC control */
#define CC1101_AGCCTRL0     0x1D    /**< AGC control */
#define CC1101_FREND1       0x1E    /**< Front end TX config */
#define CC1101_FREND0       0x1F    /**< Front end TX config */
#define CC1101_FSCAL3       0x20    /**< Frequency synthesizer calibration */
#define CC1101_FSCAL2       0x21    /**< Frequency synthesizer calibration */
#define CC1101_FSCAL1       0x22    /**< Frequency synthesizer calibration */
#define CC1101_FSCAL0       0x23    /**< Frequency synthesizer calibration */
#define CC1101_TEST2        0x24    /**< Test configuration */
#define CC1101_TEST1        0x25    /**< Test configuration */
#define CC1101_TEST0        0x26    /**< Test configuration */
#define CC1101_PATABLE      0x3E    /**< PA power table */
#define CC1101_TXBYTES      0x7D    /**< TX FIFO byte count (status) */
#define CC1101_RXBYTES      0x7E    /**< RX FIFO byte count (status) */
#define CC1101_RSSI         0x7F    /**< RSSI value (status, signed) */

/* ── CC1101 command strobes ─────────────────────────────────────────────── */

#define CC1101_SRES         0x30    /**< Reset chip */
#define CC1101_SFSTXON     0x31    /**< Enable/calibrate freq synth */
#define CC1101_SXOFF       0x32    /**< Turn off crystal oscillator */
#define CC1101_SCAL         0x33    /**< Calibrate freq synth and turn off */
#define CC1101_SRX          0x34    /**< Enable RX */
#define CC1101_STX          0x35    /**< Enable TX */
#define CC1101_SIDLE        0x36    /**< Enter IDLE state */
#define CC1101_SAFC         0x37    /**< AFC adjustment */
#define CC1101_SWOR         0x38    /**< Start WOR timer */
#define CC1101_SPWD         0x39    /**< Enter power-down mode */
#define CC1101_SFRX         0x3A    /**< Flush RX FIFO */
#define CC1101_SFTX         0x3B    /**< Flush TX FIFO */
#define CC1101_SWORRST      0x3C    /**< Reset WOR timer */
#define CC1101_SNOP         