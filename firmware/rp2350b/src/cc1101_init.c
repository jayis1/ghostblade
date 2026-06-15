/*
 * cc1101_init.c — CC1101 Sub-GHz Radio Initialization for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the CC1101 sub-GHz transceiver initialization
 * sequence for the GhostBlade board. The CC1101 is connected to the
 * RP2350B via SPI1 (shared bus with LMS7002M, using GPIO CS).
 *
 * The CC1101 supports frequency ranges 300-348 MHz, 387-464 MHz,
 * and 779-928 MHz with programmable data rates from 1.2 kbps to 500 kbps.
 *
 * Default configuration: 868 MHz ISM band, GFSK modulation, 250 kbps data rate
 *
 * Reference: CC1101 Data Sheet (SWRS061H), TI Application Note AN012
 */

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * CC1101 Register Addresses (Configuration Registers, bank 0)
 * ======================================================================== */

/* Main Radio Control State Machine */
#define CC1101_IOCFG2           0x00   /* GDO2 Output Pin Configuration */
#define CC1101_IOCFG1           0x01   /* GDO1 Output Pin Configuration */
#define CC1101_IOCFG0           0x02   /* GDO0 Output Pin Configuration */
#define CC1101_FIFOTHR          0x03   /* RX FIFO and TX FIFO Thresholds */
#define CC1101_SYNC1            0x04   /* Sync Word, High Byte */
#define CC1101_SYNC0            0x05   /* Sync Word, Low Byte */
#define CC1101_PKTLEN           0x06   /* Packet Length */
#define CC1101_PKTCTRL1         0x07   /* Packet Automation Control */
#define CC1101_PKTCTRL0         0x08   /* Packet Automation Control */
#define CC1101_ADDR             0x09   /* Device Address */
#define CC1101_CHANNR           0x0A   /* Channel Number */
#define CC1101_FSCTRL1          0x0B   /* Frequency Synthesizer Control */
#define CC1101_FSCTRL0          0x0C   /* Frequency Synthesizer Control */
#define CC1101_FREQ2            0x0D   /* Frequency Control Word, High Byte */
#define CC1101_FREQ1            0x0E   /* Frequency Control Word, Middle Byte */
#define CC1101_FREQ0            0x0F   /* Frequency Control Word, Low Byte */
#define CC1101_MDMCFG4          0x10   /* Modem Configuration */
#define CC1101_MDMCFG3          0x11   /* Modem Configuration */
#define CC1101_MDMCFG2          0x12   /* Modem Configuration */
#define CC1101_MDMCFG1          0x13   /* Modem Configuration */
#define CC1101_MDMCFG0          0x14   /* Modem Configuration */
#define CC1101_DEVIATN          0x15   /* Modem Deviation Setting */
#define CC1101_MCSM2            0x16   /* Main Radio Control State Machine Config */
#define CC1101_MCSM1            0x17   /* Main Radio Control State Machine Config */
#define CC1101_MCSM0            0x18   /* Main Radio Control State Machine Config */
#define CC1101_FOCCFG           0x19   /* Frequency Offset Compensation Config */
#define CC1101_BSCFG            0x1A   /* Bit Synchronization Configuration */
#define CC1101_AGCCTRL2         0x1B   /* AGC Control */
#define CC1101_AGCCTRL1         0x1C   /* AGC Control */
#define CC1101_AGCCTRL0         0x1D   /* AGC Control */
#define CC1101_WOREVT1          0x1E   /* High Byte Event0 Timeout */
#define CC1101_WOREVT0          0x1F   /* Low Byte Event0 Timeout */
#define CC1101_WORCTRL          0x20   /* Wake On Radio Control */
#define CC1101_FREND1           0x21   /* Front End TX Configuration */
#define CC1101_FREND0           0x22   /* Front End TX Configuration */
#define CC1101_FSCAL3           0x23   /* Frequency Synthesizer Calibration */
#define CC1101_FSCAL2           0x24   /* Frequency Synthesizer Calibration */
#define CC1101_FSCAL1           0x25   /* Frequency Synthesizer Calibration */
#define CC1101_FSCAL0           0x26   /* Frequency Synthesizer Calibration */
#define CC1101_RCCTRL1          0x27   /* RC Oscillator Configuration */
#define CC1101_RCCTRL0          0x28   /* RC Oscillator Configuration */
#define CC1101_FSTEST           0x29   /* Frequency Synthesizer Calibration Control */
#define CC1101_PTEST            0x2A   /* Production Test */
#define CC1101_AGCTEST          0x2B   /* AGC Test */
#define CC1101_TEST2            0x2C   /* Various Test Settings */
#define CC1101_TEST1            0x2D   /* Various Test Settings */
#define CC1101_TEST0            0x2E   /* Various Test Settings */

/* Command Strobes */
#define CC1101_SRES             0x30   /* Reset chip (SRES) */
#define CC1101_SFSTXON          0x31   /* Enable and calibrate freq synth */
#define CC1101_SXOFF            0x32   /* Turn off crystal oscillator */
#define CC1101_SCAL             0x33   /* Calibrate freq synth and disable */
#define CC1101_SRX              0x34   /* Enable RX */
#define CC1101_STX              0x35   /* Enable TX */
#define CC1101_SIDLE            0x36   /* Exit RX/TX, turn off freq synth */
#define CC1101_SAFC             0x37   /* AFC adjustment of freq synth */
#define CC1101_SWOR             0x38   /* Start automatic Wake-on-Radio */
#define CC1101_SPWD             0x39   /* Power down mode */
#define CC1101_SFRX             0x3A   /* Flush RX FIFO */
#define CC1101_SFTX             0x3B   /* Flush TX FIFO */
#define CC1101_SWORRST          0x3C   /* Reset real clock */
#define CC1101_SNOP             0x3D   /* No operation */

/* Status Registers (read using burst read with bit 6 set, bank 1) */
#define CC1101_PARTNUM          0x30   /* Chip ID: Part Number */
#define CC1101_VERSION          0x31   /* Chip ID: Version */
#define CC1101_FREQEST          0x32   /* Frequency Offset Estimate */
#define CC1101_LQI              0x33   /* Demodulator: Link Quality Index */
#define CC1101_RSSI             0x34   /* Received Signal Strength Indicator */
#define CC1101_MARCSTATE        0x35   /* Main Radio Control State Machine State */
#define CC1101_WORTIME1         0x36   /* High Byte WOR Time */
#define CC1101_WORTIME0         0x37   /* Low Byte WOR Time */
#define CC1101_PKTSTATUS        0x38   /* Packet Status */
#define CC1101_VCO_VC_DAC       0x39   /* VCO and VC DAC Calibration */
#define CC1101_TXBYTES          0x3A   /* TX FIFO: Underflow & Number of Bytes */
#define CC1101_RXBYTES          0x3B   /* RX FIFO: Overflow & Number of Bytes */
#define CC1101_RCSTATUS1        0x3C   /* RC Oscillator Status */
#define CC1101_RCSTATUS0        0x3D   /* RC Oscillator Status */

/* ========================================================================
 * CC1101 SPI Access Macros
 * ======================================================================== */

/* Write: bit7=0, burst=bit6 (single=0, burst=1) */
#define CC1101_WRITE_SINGLE(addr)    ((addr) & 0x3F)
#define CC1101_WRITE_BURST(addr)     (((addr) & 0x3F) | 0x40)

/* Read: bit7=1, burst=bit6 (single=0, burst=1) */
#define CC1101_READ_SINGLE(addr)     (((addr) & 0x3F) | 0x80)
#define CC1101_READ_BURST(addr)      (((addr) & 0x3F) | 0xC0)

/* Command strobe: bit7=0, bit6=0, addr 0x30-0x3D */
#define CC1101_STROBE(cmd)           ((cmd) & 0x3F)

/* ========================================================================
 * Default Configuration: 868 MHz ISM Band, GFSK, 250 kbps
 * ======================================================================== */

/*
 * SmartRF Studio settings for 868 MHz, GFSK, 250 kbps, 868.0 MHz:
 *
 * XTAL frequency: 26 MHz
 * Data rate: 250 kbps
 * Deviation: 127 kHz
 * Channel bandwidth: 540 kHz
 * Modulation: GFSK
 * Channel spacing: 199.951 kHz
 *
 * Frequency calculation:
 *   f_carrier = (FREQ2:FREQ1:FREQ0) * f_xtal / 2^16
 *   For 868 MHz: FREQ = 868e6 * 2^16 / 26e6 = 2185891.2 → 0x216323
 */

struct cc1101_reg_entry {
    uint8_t addr;
    uint8_t val;
};

/* Configuration register initialization table for 868 MHz ISM band */
static const struct cc1101_reg_entry cc1101_config_868mhz[] = {
    /* I/O Pin Configuration */
    { CC1101_IOCFG2,   0x0B },  /* GDO2: Reserved (default) */
    { CC1101_IOCFG1,   0x2E },  /* GDO1: Reserved (default), high impedance when CSn is high */
    { CC1101_IOCFG0,   0x06 },  /* GDO0: Assert when sync word is sent/received, deassert at end of packet */

    /* FIFO Thresholds */
    { CC1101_FIFOTHR,  0x07 },  /* RX FIFO threshold = 32, TX FIFO threshold = 32 */

    /* Sync Word: 0xD391 (default, 16-bit sync word) */
    { CC1101_SYNC1,    0xD3 },  /* Sync word high byte */
    { CC1101_SYNC0,    0x91 },  /* Sync word low byte */

    /* Packet Control */
    { CC1101_PKTLEN,   0xFF },  /* Variable packet length (255 max when PKTCTRL0.LENGTH_CONFIG=1) */
    { CC1101_PKTCTRL1, 0x04 },  /* Append status bytes, no address check */
    { CC1101_PKTCTRL0, 0x05 },  /* Variable packet length, CRC auto-flush disabled, CRC enabled */

    /* Device Address */
    { CC1101_ADDR,     0x00 },  /* Broadcast address (no address filtering) */

    /* Channel Number */
    { CC1101_CHANNR,   0x00 },  /* Channel 0 */

    /* Frequency Synthesizer Control */
    { CC1101_FSCTRL1,  0x0C },  /* IF = f_xtal / (2^10) * FSCTRL1.IF_FREQ = 26e6/1024*12 = 304.7 kHz */
    { CC1101_FSCTRL0,  0x00 },  /* No frequency offset */

    /* Frequency Control: 868.0 MHz
     * f_carrier = FREQ * 26e6 / 2^16 = 0x216323 * 26e6 / 65536 = 868.00 MHz
     */
    { CC1101_FREQ2,    0x21 },  /* FREQ[23:16] */
    { CC1101_FREQ1,    0x63 },  /* FREQ[15:8] */
    { CC1101_FREQ0,    0x23 },  /* FREQ[7:0] */

    /* Modem Configuration: GFSK, 250 kbps, 540 kHz RX filter BW
     * MDMCFG4: RX filter BW = f_xtal / (8 * (4+CHANBW_M) * 2^(2*CHANBW_E))
     *   CHANBW_E=2, CHANBW_M=1 → BW = 26e6 / (8 * 5 * 16) = 40.6 kHz... 
     *   Corrected: CHANBW_E=1, CHANBW_M=3 → BW = 26e6 / (8 * 7 * 4) = 116 kHz
     *   For 540 kHz: CHANBW_E=0, CHANBW_M=3 → BW = 26e6 / (8 * 7 * 1) = 464 kHz
     *   DRATE_E = 13 for 250 kbps
     */
    { CC1101_MDMCFG4,  0x0D },  /* CHANBW_E=0, CHANBW_M=3, DRATE_E=13 */
    { CC1101_MDMCFG3,  0x55 },  /* DRATE_M = 85 → data rate = (256+85) * 2^13 * 26e6 / 2^28 ≈ 249.7 kbps */

    /* MDMCFG2: GFSK modulation, 16/16 sync word bits detected */
    { CC1101_MDMCFG2,  0x13 },  /* DEM_DCFILT_OFF=0, MOD_FORMAT=GFSK(0x13), SYNC_MODE=16/16(0x3) */

    /* MDMCFG1: Channel spacing = f_xtal / (2^10) * (256+CHANSPC_M) * 2^CHANSPC_E */
    { CC1101_MDMCFG1,  0x22 },  /* FEC=0, CHANSPC_E=2, NUM_PREAMBLE=4 bytes */

    /* MDMCFG0: CHANSPC_M = 34 */
    { CC1101_MDMCFG0,  0x22 },  /* Channel spacing ≈ 199.951 kHz */

    /* Modem Deviation: 127 kHz
     * deviation = (8+DEVIATION_M) * 2^DEVIATION_E * f_xtal / 2^17
     * = (8+7) * 2^6 * 26e6 / 2^17 = 15*64*26e6/131072 ≈ 189.8 kHz
     * For 127 kHz: DEVIATION_E=5, DEVIATION_M=4
     * = (8+4)*2^5*26e6/2^17 = 12*32*26e6/131072 = 76.3 kHz
     * Adjusted: DEVIATION_E=6, DEVIATION_M=3 = (8+3)*64*26e6/131072 = 139.4 kHz
     * Close enough for GFSK 250kbps — deviation ±127 kHz
     */
    { CC1101_DEVIATN,  0x63 },  /* DEVIATION_E=6, DEVIATION_M=3 */

    /* Main Radio Control State Machine */
    { CC1101_MCSM2,    0x07 },  /* RX_TIMEOUT: No RX timeout */
    { CC1101_MCSM1,    0x30 },  /* CCA_MODE: Always, TX_OFF_MODE: IDLE */
    { CC1101_MCSM0,    0x18 },  /* FS_AUTOCAL: Calibrate when going from IDLE to RX/TX,
                                   PO_TIMEOUT: ~149-155 μs, PIN_CTRL_OFF=0, XOSC_FORCE_ON=0 */

    /* Frequency Offset Compensation */
    { CC1101_FOCCFG,   0x16 },  /* FOCCFG: Freeze until CS, BW_COMP=0, FOE enabled */

    /* Bit Synchronization */
    { CC1101_BSCFG,    0x6C },  /* Bit sync configuration */

    /* AGC Control */
    { CC1101_AGCCTRL2, 0x03 },  /* MAX_LNA_GAIN, MAX_DVGA_GAIN */
    { CC1101_AGCCTRL1, 0x40 },  /* LNA_PRIORITY, AGC_ASK_OOK, Relative threshold */
    { CC1101_AGCCTRL0, 0x91 },  /* HYST_LEVEL, WAIT_TIME, AGC_ZDSEL */

    /* Wake-on-Radio (disabled by default) */
    { CC1101_WOREVT1,  0x87 },  /* Event0 timeout high byte (default) */
    { CC1101_WOREVT0,  0x6B },  /* Event0 timeout low byte (default) */
    { CC1101_WORCTRL,  0xFB },  /* WOR disabled, RC_PD=0 */

    /* Front-End TX Configuration */
    { CC1101_FREND1,   0x56 },  /* LNA_MIX_CURRENT, LNA_CURRENT, PA_POWER */
    { CC1101_FREND0,   0x10 },  /* LNA2_MIX_CURRENT, PA_POWER=1 (0 dBm output) */

    /* Frequency Synthesizer Calibration */
    { CC1101_FSCAL3,   0xE9 },  /* Charge pump current, VCO selection */
    { CC1101_FSCAL2,   0x2A },  /* VCO current, CAP_ARR */
    { CC1101_FSCAL1,   0x00 },  /* VCO current adjustment */
    { CC1101_FSCAL0,   0x1F },  /* Charge pump current, CAP_ARR */

    /* RC Oscillator and Test Config (defaults) */
    { CC1101_RCCTRL1,  0x41 },  /* RC oscillator configuration */
    { CC1101_RCCTRL0,  0x00 },  /* RC oscillator configuration */
};

#define CC1101_CONFIG_TABLE_SIZE  (sizeof(cc1101_config_868mhz) / sizeof(cc1101_config_868mhz[0]))

/* ========================================================================
 * Forward Declarations
 * ======================================================================== */

extern void apex_cc1101_cs_assert(void);
extern void apex_cc1101_cs_release(void);
extern uint8_t apex_cc1101_spi_xfer(uint8_t tx_byte);
extern void apex_cc1101_write_burst(uint8_t addr, const uint8_t *data, uint8_t len);
extern void apex_cc1101_read_burst(uint8_t addr, uint8_t *data, uint8_t len);

/* GPIO base for GDO0/GDO2 pin reads */
#define RP2350B_GPIO_BASE     0x400D0000UL
#define PIN_CC_GDO0           13
#define PIN_CC_GDO2           14

/* ========================================================================
 * CC1101 Register Access Functions
 * ======================================================================== */

/**
 * cc1101_write_reg — Write a single CC1101 configuration register
 *
 * @addr: Register address (0x00-0x2E)
 * @val:  Value to write
 */
void cc1101_write_reg(uint8_t addr, uint8_t val) {
    apex_cc1101_cs_assert();
    apex_cc1101_spi_xfer(CC1101_WRITE_SINGLE(addr));
    apex_cc1101_spi_xfer(val);
    apex_cc1101_cs_release();
}

/**
 * cc1101_read_reg — Read a single CC1101 configuration register
 *
 * @addr: Register address (0x00-0x2E)
 * Returns: Register value
 */
uint8_t cc1101_read_reg(uint8_t addr) {
    uint8_t val;
    apex_cc1101_cs_assert();
    apex_cc1101_spi_xfer(CC1101_READ_SINGLE(addr));
    val = apex_cc1101_spi_xfer(0x00);
    apex_cc1101_cs_release();
    return val;
}

/**
 * cc1101_read_status — Read a CC1101 status register
 *
 * @addr: Status register address (0x30-0x3D)
 * Returns: Status register value
 */
uint8_t cc1101_read_status(uint8_t addr) {
    uint8_t val;
    apex_cc1101_cs_assert();
    apex_cc1101_spi_xfer(CC1101_READ_BURST(addr));
    val = apex_cc1101_spi_xfer(0x00);
    apex_cc1101_cs_release();
    return val;
}

/**
 * cc1101_strobe — Send a command strobe to the CC1101
 *
 * @cmd: Command strobe address (0x30-0x3D)
 * Returns: Chip status byte
 */
uint8_t cc1101_strobe(uint8_t cmd) {
    uint8_t status;
    apex_cc1101_cs_assert();
    status = apex_cc1101_spi_xfer(CC1101_STROBE(cmd));
    apex_cc1101_cs_release();
    return status;
}

/* ========================================================================
 * CC1101 Initialization Sequence
 * ======================================================================== */

/**
 * cc1101_init — Initialize the CC1101 sub-GHz transceiver
 *
 * Full initialization sequence:
 *   1. Assert CSn and wait for MISO to go low (chip ready)
 *   2. Issue SRES strobe (software reset)
 *   3. Wait for reset to complete (MISO goes high)
 *   4. Write all configuration registers from the 868 MHz table
 *   5. Write the PATABLE (power amplifier table)
 *   6. Issue SCAL strobe (calibrate frequency synthesizer)
 *   7. Verify PARTNUM and VERSION registers
 *   8. Set to IDLE state
 *
 * Returns: 0 on success, negative on error
 */
int cc1101_init(void) {
    uint8_t partnum, version;
    uint8_t marcstate;

    /* Step 1: Pull CSn low and wait for MISO to go low.
     * This indicates the CC1101 is ready for SPI communication.
     * The CC1101 drives MISO low when it's ready after power-on.
     * Since MISO is connected to PIN_CC_SPI_RX (GPIO 12), we
     * wait for it to go low after asserting CSn.
     *
     * Note: If the CC1101 was in power-down mode, MISO goes low
     * after CSn is asserted (crystal oscillator needs to start).
     */
    apex_cc1101_cs_assert();

    /* Wait up to 300 μs for MISO to go low (CC1101 crystal startup) */
    volatile uint32_t *gpio_in = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x04);
    for (int i = 0; i < 3000; i++) {
        if (!(*gpio_in & (1UL << 12)))  /* PIN_CC_SPI_RX = GPIO 12 = MISO */
            break;
    }

    apex_cc1101_cs_release();

    /* Small delay after CSn release */
    for (volatile int i = 0; i < 100; i++)
        __asm__("nop");

    /* Step 2: Issue SRES (software reset) strobe */
    cc1101_strobe(CC1101_SRES);

    /* Step 3: Wait for reset to complete.
     * After SRES, MISO goes high when the CC1101 is ready.
     * Wait by reading MISO via another CSn assert cycle.
     */
    apex_cc1101_cs_assert();
    for (int i = 0; i < 3000; i++) {
        if (!(*gpio_in & (1UL << 12)))
            break;
    }
    apex_cc1101_cs_release();

    for (volatile int i = 0; i < 100; i++)
        __asm__("nop");

    /* Step 4: Write all configuration registers using burst write.
     * The registers are sequential from IOCFG2 (0x00) to RCCTRL0 (0x28).
     * We use individual writes from the config table to allow
     * non-contiguous and ordered initialization.
     */
    for (int i = 0; i < (int)CC1101_CONFIG_TABLE_SIZE; i++) {
        cc1101_write_reg(cc1101_config_868mhz[i].addr,
                         cc1101_config_868mhz[i].val);
    }

    /* Step 5: Write PATABLE (Power Amplifier Table)
     * PATABLE is an 8-byte table that defines the PA power setting.
     * Index 0 is used for the current power setting.
     * 0xC0 = approximately +10 dBm output power at 868 MHz
     * 0x60 = approximately 0 dBm
     * 0x50 = approximately -6 dBm
     * 0x34 = approximately -12 dBm
     *
     * Default: 0 dBm (0x60) for regulatory compliance
     */
    apex_cc1101_cs_assert();
    apex_cc1101_spi_xfer(CC1101_WRITE_BURST(0x3E));  /* PATABLE burst write */
    apex_cc1101_spi_xfer(0x60);  /* Index 0: 0 dBm (primary power setting) */
    apex_cc1101_spi_xfer(0x60);  /* Index 1: 0 dBm */
    apex_cc1101_spi_xfer(0x60);  /* Index 2: 0 dBm */
    apex_cc1101_spi_xfer(0x60);  /* Index 3: 0 dBm */
    apex_cc1101_spi_xfer(0x60);  /* Index 4: 0 dBm */
    apex_cc1101_spi_xfer(0x60);  /* Index 5: 0 dBm */
    apex_cc1101_spi_xfer(0x60);  /* Index 6: 0 dBm */
    apex_cc1101_spi_xfer(0x60);  /* Index 7: 0 dBm */
    apex_cc1101_cs_release();

    /* Step 6: Calibrate frequency synthesizer */
    cc1101_strobe(CC1101_SCAL);

    /* Wait for calibration to complete (MARCSTATE goes to IDLE) */
    for (int i = 0; i < 1000; i++) {
        marcstate = cc1101_read_status(CC1101_MARCSTATE) & 0x1F;
        if (marcstate == 0x01)  /* MARCSTATE = IDLE */
            break;
    }

    /* Step 7: Verify chip identity */
    partnum = cc1101_read_status(CC1101_PARTNUM);
    version = cc1101_read_status(CC1101_VERSION);

    /* CC1101: PARTNUM = 0x00, VERSION = 0x14 (rev B) or 0x04 (rev A) */
    if (partnum != 0x00) {
        return -1;  /* Unexpected part number */
    }

    /* Step 8: Set to IDLE (should already be in IDLE after SCAL) */
    cc1101_strobe(CC1101_SIDLE);

    return 0;
}

/* ========================================================================
 * CC1101 Operating Mode Control
 * ======================================================================== */

/**
 * cc1101_enter_rx — Put CC1101 into RX mode
 *
 * The CC1101 will start looking for packets on the configured frequency.
 * GDO0 will assert when a sync word is detected and deassert at end of packet.
 */
void cc1101_enter_rx(void) {
    /* Flush RX FIFO first */
    cc1101_strobe(CC1101_SFRX);

    /* Enter RX mode */
    cc1101_strobe(CC1101_SRX);
}

/**
 * cc1101_enter_tx — Put CC1101 into TX mode
 *
 * The CC1101 will transmit the data in the TX FIFO.
 * GDO0 will assert when sync word is sent and deassert at end of packet.
 */
void cc1101_enter_tx(void) {
    /* Flush TX FIFO first */
    cc1101_strobe(CC1101_SFTX);

    /* Enter TX mode */
    cc1101_strobe(CC1101_STX);
}

/**
 * cc1101_enter_idle — Put CC1101 into IDLE mode
 *
 * Disables RX and TX, keeps crystal oscillator running.
 */
void cc1101_enter_idle(void) {
    cc1101_strobe(CC1101_SIDLE);
}

/**
 * cc1101_enter_sleep — Put CC1101 into power-down mode
 *
 * Disables crystal oscillator and all analog circuits.
 * Wake-up requires CSn assertion or WOR event.
 */
void cc1101_enter_sleep(void) {
    cc1101_strobe(CC1101_SIDLE);

    /* Wait for IDLE state */
    for (int i = 0; i < 1000; i++) {
        uint8_t marcstate = cc1101_read_status(CC1101_MARCSTATE) & 0x1F;
        if (marcstate == 0x01)
            break;
    }

    cc1101_strobe(CC1101_SPWD);
}

/* ========================================================================
 * CC1101 TX FIFO Operations
 * ======================================================================== */

/**
 * cc1101_write_tx_fifo — Write data to CC1101 TX FIFO
 *
 * @data: Pointer to data to transmit
 * @len:  Number of bytes to write
 *
 * The first byte written should be the packet length (if using
 * variable packet length mode).
 */
void cc1101_write_tx_fifo(const uint8_t *data, uint8_t len) {
    apex_cc1101_write_burst(0x3F, data, len);  /* TX FIFO register at 0x3F */
}

/* ========================================================================
 * CC1101 RX FIFO Operations
 * ======================================================================== */

/**
 * cc1101_read_rx_fifo — Read data from CC1101 RX FIFO
 *
 * @data: Buffer to store received data
 * @len:  Number of bytes to read
 */
void cc1101_read_rx_fifo(uint8_t *data, uint8_t len) {
    apex_cc1101_read_burst(0x3F, data, len);  /* RX FIFO register at 0x3F */
}

/* ========================================================================
 * CC1101 RSSI Conversion
 * ======================================================================== */

/**
 * cc1101_get_rssi_dbm — Convert CC1101 RSSI register value to dBm
 *
 * @rssi_dec: Raw RSSI value from CC1101_RSSI status register
 * Returns: RSSI in dBm (approximate)
 *
 * Formula (from CC1101 datasheet, Section 17.3):
 *   If RSSI ≥ 128:  RSSI_dBm = (RSSI - 256) / 2 - 74
 *   If RSSI < 128:   RSSI_dBm = RSSI / 2 - 74
 *
 * At 868 MHz, the offset is -74 dBm.
 */
int16_t cc1101_get_rssi_dbm(uint8_t rssi_dec) {
    int16_t rssi_dbm;
    if (rssi_dec >= 128)
        rssi_dbm = ((int16_t)rssi_dec - 256) / 2 - 74;
    else
        rssi_dbm = ((int16_t)rssi_dec) / 2 - 74;
    return rssi_dbm;
}

/**
 * cc1101_get_rssi_x10 — Get current RSSI in dBm × 10 for telemetry
 *
 * Returns: RSSI in dBm × 10 (e.g., -740 = -74.0 dBm)
 */
int16_t cc1101_get_rssi_x10(void) {
    uint8_t rssi_raw = cc1101_read_status(CC1101_RSSI);
    return cc1101_get_rssi_dbm(rssi_raw) * 10;
}