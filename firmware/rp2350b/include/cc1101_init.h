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

/* ── CC1101 register addresses (configuration registers, bank 0) ────────── */
/* Per TI CC1101 datasheet (SWRS061H): register addresses 0x00–0x2E           */

#define CC1101_IOCFG2       0x00    /**< GDO2 output configuration */
#define CC1101_IOCFG1       0x01    /**< GDO1 output configuration */
#define CC1101_IOCFG0       0x02    /**< GDO0 output configuration */
#define CC1101_FIFOTHR      0x03    /**< RX/TX FIFO threshold */
#define CC1101_SYNC1        0x04    /**< Sync word, high byte */
#define CC1101_SYNC0        0x05    /**< Sync word, low byte */
#define CC1101_PKTLEN       0x06    /**< Packet length */
#define CC1101_PKTCTRL1     0x07    /**< Packet automation control */
#define CC1101_PKTCTRL0     0x08    /**< Packet automation control */
#define CC1101_ADDR         0x09    /**< Device address */
#define CC1101_CHANNR       0x0A    /**< Channel number */
#define CC1101_FSCTRL1      0x0B    /**< Frequency synthesizer control */
#define CC1101_FSCTRL0      0x0C    /**< Frequency synthesizer control */
#define CC1101_FREQ2        0x0D    /**< Frequency control word, high byte */
#define CC1101_FREQ1        0x0E    /**< Frequency control word, middle byte */
#define CC1101_FREQ0        0x0F    /**< Frequency control word, low byte */
#define CC1101_MDMCFG4      0x10    /**< Modem configuration */
#define CC1101_MDMCFG3      0x11    /**< Modem configuration */
#define CC1101_MDMCFG2      0x12    /**< Modem configuration */
#define CC1101_MDMCFG1      0x13    /**< Modem configuration */
#define CC1101_MDMCFG0      0x14    /**< Modem configuration */
#define CC1101_DEVIATN      0x15    /**< Modem deviation setting */
#define CC1101_MCSM2        0x16    /**< Main radio control state machine */
#define CC1101_MCSM1        0x17    /**< Main radio control state machine */
#define CC1101_MCSM0        0x18    /**< Main radio control state machine */
#define CC1101_FOCCFG       0x19    /**< Frequency offset compensation */
#define CC1101_BSCFG        0x1A    /**< Bit synchronization config */
#define CC1101_AGCCTRL2     0x1B    /**< AGC control */
#define CC1101_AGCCTRL1     0x1C    /**< AGC control */
#define CC1101_AGCCTRL0     0x1D    /**< AGC control */
#define CC1101_WOREVT1      0x1E    /**< High byte Event0 timeout */
#define CC1101_WOREVT0      0x1F    /**< Low byte Event0 timeout */
#define CC1101_WORCTRL      0x20    /**< Wake On Radio control */
#define CC1101_FREND1       0x21    /**< Front end TX config */
#define CC1101_FREND0       0x22    /**< Front end TX config */
#define CC1101_FSCAL3       0x23    /**< Frequency synthesizer calibration */
#define CC1101_FSCAL2       0x24    /**< Frequency synthesizer calibration */
#define CC1101_FSCAL1       0x25    /**< Frequency synthesizer calibration */
#define CC1101_FSCAL0       0x26    /**< Frequency synthesizer calibration */
#define CC1101_RCCTRL1      0x27    /**< RC oscillator configuration */
#define CC1101_RCCTRL0      0x28    /**< RC oscillator configuration */
#define CC1101_FSTEST        0x29    /**< Frequency synthesizer calibration control */
#define CC1101_PTEST         0x2A    /**< Production test */
#define CC1101_AGCTEST       0x2B    /**< AGC test */
#define CC1101_TEST2         0x2C    /**< Various test settings */
#define CC1101_TEST1         0x2D    /**< Various test settings */
#define CC1101_TEST0         0x2E    /**< Various test settings */

/* ── CC1101 command strobes ─────────────────────────────────────────────── */

#define CC1101_SRES          0x30    /**< Reset chip */
#define CC1101_SFSTXON      0x31    /**< Enable and calibrate freq synth */
#define CC1101_SXOFF         0x32    /**< Turn off crystal oscillator */
#define CC1101_SCAL          0x33    /**< Calibrate freq synth and disable */
#define CC1101_SRX           0x34    /**< Enable RX */
#define CC1101_STX           0x35    /**< Enable TX */
#define CC1101_SIDLE         0x36    /**< Enter IDLE state */
#define CC1101_SAFC          0x37    /**< AFC adjustment */
#define CC1101_SWOR          0x38    /**< Start Wake-on-Radio */
#define CC1101_SPWD          0x39    /**< Enter power-down mode */
#define CC1101_SFRX          0x3A    /**< Flush RX FIFO */
#define CC1101_SFTX          0x3B    /**< Flush TX FIFO */
#define CC1101_SWORRST       0x3C    /**< Reset WOR timer */
#define CC1101_SNOP          0x3D    /**< No operation */

/* ── CC1101 status registers (bank 1, read with burst bit) ─────────────── */

#define CC1101_PARTNUM       0x30    /**< Chip ID: part number */
#define CC1101_VERSION       0x31    /**< Chip ID: version */
#define CC1101_FREQEST       0x32    /**< Frequency offset estimate */
#define CC1101_LQI            0x33    /**< Link quality index */
#define CC1101_RSSI           0x34    /**< RSSI value (signed, dBm) */
#define CC1101_MARCSTATE     0x35    /**< Main radio control state */
#define CC1101_WORTIME1      0x36    /**< High byte WOR time */
#define CC1101_WORTIME0      0x37    /**< Low byte WOR time */
#define CC1101_PKTSTATUS     0x38    /**< Packet status */
#define CC1101_VCO_VC_DAC    0x39    /**< VCO and VC DAC calibration */
#define CC1101_TXBYTES        0x3A    /**< TX FIFO byte count */
#define CC1101_RXBYTES        0x3B    /**< RX FIFO byte count */
#define CC1101_RCSTATUS1     0x3C    /**< RC oscillator status */
#define CC1101_RCSTATUS0     0x3D    /**< RC oscillator status */

/* ── CC1101 PA table and FIFO access ───────────────────────────────────── */

#define CC1101_PATABLE       0x3E    /**< PA power table (0x3E burst) */
#define CC1101_TXFIFO        0x3F    /**< TX FIFO (write: 0x3F, read: 0xBF) */
#define CC1101_RXFIFO        0x3F    /**< RX FIFO (write: 0x7F, read: 0xFF) */

/* ── CC1101 public API ─────────────────────────────────────────────────── */

/**
 * cc1101_init — Initialize CC1101 sub-GHz radio
 *
 * Configures the CC1101 for 868 MHz ISM band operation with GFSK
 * modulation at 250 kbps. Performs full reset, register write,
 * calibration, and ID verification.
 *
 * Returns: 0 on success, -1 on part number mismatch,
 *          -2 on MISO timeout, -3 on reset timeout, -4 on calibration timeout
 */
int cc1101_init(void);

/**
 * cc1101_enter_rx — Put CC1101 into RX mode
 */
void cc1101_enter_rx(void);

/**
 * cc1101_enter_tx — Put CC1101 into TX mode
 */
void cc1101_enter_tx(void);

/**
 * cc1101_enter_idle — Put CC1101 into IDLE mode
 */
void cc1101_enter_idle(void);

/**
 * cc1101_enter_sleep — Put CC1101 into power-down mode
 */
void cc1101_enter_sleep(void);

/**
 * cc1101_write_tx_fifo — Write data to CC1101 TX FIFO
 *
 * @data: Pointer to data to transmit
 * @len:  Number of bytes to write
 */
void cc1101_write_tx_fifo(const uint8_t *data, uint8_t len);

/**
 * cc1101_read_rx_fifo — Read data from CC1101 RX FIFO
 *
 * @data: Buffer to store received data
 * @len:  Number of bytes to read
 */
void cc1101_read_rx_fifo(uint8_t *data, uint8_t len);

/**
 * cc1101_get_rssi_dbm — Convert CC1101 RSSI register value to dBm
 *
 * @rssi_dec: Raw RSSI value from CC1101_RSSI status register
 * Returns: RSSI in dBm (int16_t, range approximately -128 to +10)
 */
int16_t cc1101_get_rssi_dbm(uint8_t rssi_dec);

/**
 * cc1101_get_rssi_x10 — Get current RSSI in dBm × 10 for telemetry
 *
 * Reads the RSSI register and converts to dBm × 10.
 * Returns: RSSI in dBm × 10 (e.g., -740 = -74.0 dBm)
 */
int16_t cc1101_get_rssi_x10(void);

/**
 * cc1101_set_band — Switch CC1101 to a different ISM band configuration
 *
 * Reconfigures the CC1101 for the specified frequency band. This performs
 * a full re-initialization: IDLE, flush FIFOs, write config table,
 * PATABLE write, SCAL strobe, and ID verification.
 *
 * @band: CC1101_BAND_433 (433 MHz), CC1101_BAND_868 (868 MHz),
 *        or CC1101_BAND_915 (915 MHz)
 *
 * Returns: 0 on success, -1 on invalid band or part number mismatch,
 *          -4 on calibration timeout
 */
int cc1101_set_band(int band);

/**
 * cc1101_get_marcstate — Read the CC1101 main radio control state
 *
 * Returns: MARCSTATE value (lower 5 bits of status register)
 *   0x00 = SLEEP, 0x01 = IDLE, 0x02 = XOFF,
 *   0x03 = VCOON_MC, 0x04 = REGON_MC, 0x05 = MANCAL,
 *   0x06 = CAL, 0x07 = CALTRY, 0x08 = SETTLING,
 *   0x09 = REGON, 0x0A = RX, 0x0B = RX_END,
 *   0x0C = RX_RST, 0x0D = TX, 0x0E = TX_END
 */
uint8_t cc1101_get_marcstate(void);

/**
 * cc1101_get_partnum — Read the CC1101 part number
 *
 * Returns: PARTNUM register value (should be 0x00 for CC1101)
 */
uint8_t cc1101_get_partnum(void);

/**
 * cc1101_get_version — Read the CC1101 chip version
 *
 * Returns: VERSION register value (0x14 for rev B, 0x04 for rev A)
 */
uint8_t cc1101_get_version(void);

/* ── CC1101 band identifiers ────────────────────────────────────────────── */

#define CC1101_BAND_433  0    /**< 433 MHz ISM band (EU 433.05–434.79 MHz) */
#define CC1101_BAND_868  1    /**< 868 MHz ISM band (EU 863–870 MHz) */
#define CC1101_BAND_915  2    /**< 915 MHz ISM band (US 902–928 MHz) */

#endif /* CC1101_INIT_H */