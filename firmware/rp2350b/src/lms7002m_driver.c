/*
 * lms7002m_driver.c — LMS7002M SDR Transceiver Driver for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the LMS7002M SDR transceiver control interface
 * for the GhostBlade board. The LMS7002M is connected to the RP2350B
 * via SPI1 (dedicated SPI bus with GPIO chip select).
 *
 * The LMS7002M is a multi-band RF transceiver supporting:
 *   - Frequency range: 100 kHz to 3.8 GHz
 *   - Bandwidth: 2 MHz to 61.44 MHz
 *   - MIMO: 2×2 (TX/RX)
 *   - Sample rates: up to 61.44 MSPS per channel
 *   - 12-bit ADC/DAC resolution
 *
 * On the GhostBlade board:
 *   - SPI1 master is used for LMS7002M configuration (up to 50 MHz SPI)
 *   - MIPI-CSI-2 carries IQ data directly to the RK3576
 *   - GPIO controls TX/RX enable and LNA
 *   - Active-low reset (PIN_SDR_RESET)
 *
 * Reference: LMS7002M Datasheet (v3.1r0), LMS7002M Programming and
 * Calibration Guide (v1.0r1)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "lms7002m_driver.h"
#include "board_pins.h"

/* ========================================================================
 * LMS7002M SPI Protocol
 *
 * The LMS7002M SPI interface uses 16-bit word transfers:
 *   Write: [0] [1] [R/W=0] [Addr(6:0)] [Data(7:0)]
 *   Read:  [0] [1] [R/W=1] [Addr(6:0)] [Dummy(7:0)] → [Data(7:0)]
 *
 * The full 32-bit SPI frame format:
 *   Bits [31:30] = 00 (reserved, must be 0)
 *   Bit  [29]    = R/W (0=write, 1=read)
 *   Bits [28:16]  = Address (13 bits, but only lower 7 used for reg access)
 *   Bits [15:0]   = Data (16 bits)
 *
 * For register access, the address space is 0x0000-0x7FFF with
 * 16-bit data per register. Burst mode auto-increments the address.
 * ======================================================================== */

/* SPI command encoding */
#define LMS7002M_SPI_WRITE      0x00000000UL
#define LMS7002M_SPI_READ       0x20000000UL
#define LMS7002M_SPI_ADDR_MASK  0x3FFF0000UL
#define LMS7002M_SPI_DATA_MASK  0x0000FFFFUL
#define LMS7002M_SPI_ADDR_SHIFT  16

/* ========================================================================
 * LMS7002M Register Addresses (Key Configuration Registers)
 *
 * These are the primary registers needed for basic SDR configuration.
 * The full register map is documented in the LMS7002M Programming Guide.
 * ======================================================================== */

/* General Configuration */
#define LMS7002M_REG(x)           ((uint16_t)(x))

/* Reference clock and PLL registers (0x0020-0x002F) */
#define REG_REFCLK_DIV             LMS7002M_REG(0x0020)
#define REG_SYNTH_EN              LMS7002M_REG(0x0021)
#define REG_CGP_INT               LMS7002M_REG(0x0022)
#define REG_CGP_FRAC              LMS7002M_REG(0x0023)
#define REG_VCOCAP                LMS7002M_REG(0x0024)

/* TX PLL registers */
#define REG_TX_PLL_INT             LMS7002M_REG(0x0100)
#define REG_TX_PLL_FRAC            LMS7002M_REG(0x0101)
#define REG_TX_PLL_VCOCAP          LMS7002M_REG(0x0102)

/* RX PLL registers */
#define REG_RX_PLL_INT             LMS7002M_REG(0x0200)
#define REG_RX_PLL_FRAC            LMS7002M_REG(0x0201)
#define REG_RX_PLL_VCOCAP          LMS7002M_REG(0x0202)

/* Transceiver configuration (0x0100-0x01FF for TX, 0x0200-0x02FF for RX) */
#define REG_XBIAS                  LMS7002M_REG(0x0081)
#define REG_XBUF                   LMS7002M_REG(0x0082)
#define REG_XRXBUF                 LMS7002M_REG(0x0083)
#define REG_XTXBUF                 LMS7002M_REG(0x0084)

/* RX and TX enable / data path */
#define REG_EN_DIR                 LMS7002M_REG(0x0240)
#define REG_EN_LNA                 LMS7002M_REG(0x0241)

/* LNA and TIA registers (RX path) */
#define REG_RFE_LNA_GAIN           LMS7002M_REG(0x0250)
#define REG_RFE_LNA_MODE           LMS7002M_REG(0x0251)
#define REG_RFE_TIA_GAIN           LMS7002M_REG(0x0252)

/* Baseband filter registers */
#define REG_TDD_BBFLT              LMS7002M_REG(0x0300)
#define REG_RX_BBFILT              LMS7002M_REG(0x0301)
#define REG_TX_BBFILT              LMS7002M_REG(0x0302)

/* Decimation / Interpolation */
#define REG_RX_DECIMATION          LMS7002M_REG(0x0310)
#define REG_TX_INTERPOLATION       LMS7002M_REG(0x0311)

/* Power down and enable registers */
#define REG_POWER_DOWN             LMS7002M_REG(0x0092)
#define REG_TX_ENABLE              LMS7002M_REG(0x0093)
#define REG_RX_ENABLE              LMS7002M_REG(0x0094)

/* SPI data interface */
#define REG_SPI_DATA_IN            LMS7002M_REG(0x0086)
#define REG_SPI_DATA_OUT           LMS7002M_REG(0x0087)

/* ========================================================================
 * SPI1 Master Transfer Functions
 * ======================================================================== */

/* SPI1 data register for master transfers */
#define SPI1_SSPDR               (0x48070000UL + 0x008)
#define SPI1_SSPSR               (0x48070000UL + 0x00C)

#define REG32(addr)               (*(volatile uint32_t *)(addr))

/**
 * lms7002m_spi_xfer — Transfer 32 bits to/from LMS7002M via SPI1
 *
 * @tx_data: 32-bit word to send
 * Returns: 32-bit word received
 *
 * The LMS7002M SPI uses 32-bit transfers with the MSB-first format:
 *   Bits [31:30] = 00 (reserved)
 *   Bit  [29]    = R/W (0=write, 1=read)
 *   Bits [28:16] = Address
 *   Bits [15:0]  = Data (write) or Dummy (read)
 */
static uint32_t lms7002m_spi_xfer(uint32_t tx_data) {
    volatile uint32_t *dr = (volatile uint32_t *)SPI1_SSPDR;
    volatile uint32_t *sr = (volatile uint32_t *)SPI1_SSPSR;

    /* Wait until TX FIFO has space */
    while (!(*sr & (1 << 1)))  /* TNF: TX FIFO not full */
        ;

    /* Write 32-bit data */
    *dr = tx_data;

    /* Wait until RX FIFO has data */
    while (!(*sr & (1 << 2)))  /* RNE: RX FIFO not empty */
        ;

    /* Read 32-bit data */
    return *dr;
}

/**
 * lms7002m_cs_assert — Assert LMS7002M chip select
 */
static void lms7002m_cs_assert(void) {
    /* Deassert CC1101 CSn first (shared SPI1 bus) */
    rp2350b_gpio_set(PIN_CC_SPI_CSN, true);

    /* Assert LMS7002M CSn (active-low) */
    rp2350b_gpio_set(PIN_SDR_SPI_CSN, false);

    /* CS setup time: LMS7002M requires >10 ns */
    for (volatile int i = 0; i < 2; i++)
        __asm__("nop");
}

/**
 * lms7002m_cs_release — Release LMS7002M chip select
 */
static void lms7002m_cs_release(void) {
    rp2350b_gpio_set(PIN_SDR_SPI_CSN, true);

    /* CS hold time */
    for (volatile int i = 0; i < 2; i++)
        __asm__("nop");
}

/* ========================================================================
 * LMS7002M Register Access Functions
 * ======================================================================== */

/**
 * lms7002m_write_reg — Write a 16-bit value to an LMS7002M register
 *
 * @addr: Register address (0x0000-0x7FFF)
 * @data: 16-bit data to write
 */
static void lms7002m_write_reg(uint16_t addr, uint16_t data) {
    uint32_t spi_cmd;

    spi_cmd = LMS7002M_SPI_WRITE
            | ((uint32_t)(addr & 0x3FFF) << LMS7002M_SPI_ADDR_SHIFT)
            | ((uint32_t)data & 0xFFFF);

    lms7002m_cs_assert();
    lms7002m_spi_xfer(spi_cmd);
    lms7002m_cs_release();
}

/**
 * lms7002m_read_reg — Read a 16-bit value from an LMS7002M register
 *
 * @addr: Register address (0x0000-0x7FFF)
 * Returns: 16-bit register value
 */
static uint16_t lms7002m_read_reg(uint16_t addr) {
    uint32_t spi_cmd;
    uint32_t rx_data;

    spi_cmd = LMS7002M_SPI_READ
            | ((uint32_t)(addr & 0x3FFF) << LMS7002M_SPI_ADDR_SHIFT);

    lms7002m_cs_assert();
    /* First transfer: send read command */
    lms7002m_spi_xfer(spi_cmd);
    lms7002m_cs_release();

    /* Second transfer: read data (send dummy) */
    lms7002m_cs_assert();
    rx_data = lms7002m_spi_xfer(0x00000000);
    lms7002m_cs_release();

    return (uint16_t)(rx_data & 0xFFFF);
}

/**
 * lms7002m_write_burst — Write multiple consecutive registers
 *
 * @start_addr: Starting register address
 * @data:        Array of 16-bit values to write
 * @count:       Number of registers to write
 */
static void lms7002m_write_burst(uint16_t start_addr,
                                    const uint16_t *data, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        lms7002m_write_reg(start_addr + i, data[i]);
    }
}

/* ========================================================================
 * PLL Frequency Calculation
 * ======================================================================== */

/*
 * The LMS7002M uses a fractional-N PLL for frequency synthesis.
 *
 * The VCO frequency is calculated as:
 *   f_VCO = f_REF * (N_INT + N_FRAC / 2^23) / 2
 *
 * Where:
 *   f_REF = reference clock frequency (typically 30.72 MHz)
 *   N_INT = integer part of the PLL division ratio
 *   N_FRAC = fractional part (0 to 2^23-1)
 *
 * The output frequency is:
 *   f_OUT = f_VCO / DIV_RATIO
 *
 * DIV_RATIO is 1, 2, 4, 8, 16, 32, or 64 depending on the
 * selected VCO divider.
 *
 * For the GhostBlade board, the reference clock is 30.72 MHz
 * from the on-board TCXO.
 */

#define LMS7002M_REF_FREQ_HZ       30720000UL   /* 30.72 MHz TCXO */
#define LMS7002M_FRAC_BITS          23           /* Fractional PLL resolution */
#define LMS7002M_FRAC_MODULO        (1UL << 23)  /* 2^23 = 8388608 */

/**
 * lms7002m_calc_pll_params — Calculate PLL integer and fractional parameters
 *
 * @freq_hz:    Target frequency in Hz (100 kHz to 3.8 GHz)
 * @n_int:      Output: PLL integer division ratio
 * @n_frac:     Output: PLL fractional division ratio
 * @div_ratio:  Output: VCO divider ratio (1, 2, 4, 8, 16, 32, or 64)
 *
 * The VCO operates in the range 2.4-3.8 GHz. The divider brings
 * the output down to the desired frequency.
 *
 * Returns: 0 on success, -1 on error (frequency out of range)
 */
static int lms7002m_calc_pll_params(uint32_t freq_hz,
                                      uint16_t *n_int, uint32_t *n_frac,
                                      uint8_t *div_ratio) {
    uint32_t vco_freq;
    uint64_t n_total;
    uint8_t div;

    /* Validate frequency range */
    if (freq_hz < 100000UL || freq_hz > 3800000000UL)
        return -1;

    /* Select VCO divider based on target frequency.
     * VCO range: 2.4 - 3.8 GHz
     * We need f_VCO = freq * div_ratio to be within VCO range. */
    if (freq_hz >= 2400000000UL) {
        div = 1;   /* No division needed */
    } else if (freq_hz >= 1200000000UL) {
        div = 2;
    } else if (freq_hz >= 600000000UL) {
        div = 4;
    } else if (freq_hz >= 300000000UL) {
        div = 8;
    } else if (freq_hz >= 150000000UL) {
        div = 16;
    } else if (freq_hz >= 75000000UL) {
        div = 32;
    } else {
        div = 64;
    }

    vco_freq = freq_hz * div;

    /* Verify VCO frequency is in range */
    if (vco_freq < 2400000000UL || vco_freq > 3800000000UL) {
        /* Try the next divider if possible */
        if (div < 64) {
            div *= 2;
            vco_freq = freq_hz * div;
            if (vco_freq < 2400000000UL || vco_freq > 3800000000UL)
                return -1;
        } else {
            return -1;
        }
    }

    /* Calculate PLL parameters:
     * f_VCO = f_REF * (N_INT + N_FRAC / 2^23) / 2
     * N_TOTAL = N_INT + N_FRAC / 2^23 = 2 * f_VCO / f_REF
     */
    n_total = ((uint64_t)vco_freq * 2 * LMS7002M_FRAC_MODULO) / LMS7002M_REF_FREQ_HZ;

    *n_int = (uint16_t)(n_total / LMS7002M_FRAC_MODULO);
    *n_frac = (uint32_t)(n_total % LMS7002M_FRAC_MODULO);
    *div_ratio = div;

    return 0;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

/**
 * lms7002m_init — Initialize the LMS7002M SDR transceiver
 *
 * Performs a full initialization sequence:
 *   1. Assert and release hardware reset
 *   2. Power up the chip
 *   3. Configure the reference clock (30.72 MHz TCXO)
 *   4. Configure default RX parameters (868 MHz, 2 MHz BW, 30 dB gain)
 *   5. Enable RX path (but not TX — requires explicit enable)
 *
 * Returns: 0 on success, negative on error
 */
int lms7002m_init(void) {
    uint16_t reg_val;

    /* Step 1: Hardware reset sequence.
     * The LMS7002M reset is active-low (PIN_SDR_RESET).
     * rp2350b_init() already released the reset, but we cycle it here
     * to ensure a clean state. */
    rp2350b_gpio_set(PIN_SDR_RESET, false);  /* Assert reset */
    for (volatile int i = 0; i < 15000; i++)   /* ~1 ms */
        __asm__("nop");
    rp2350b_gpio_set(PIN_SDR_RESET, true);   /* Release reset */
    for (volatile int i = 0; i < 150000; i++)  /* ~10 ms for PLL startup */
        __asm__("nop");

    /* Step 2: Verify SPI communication by reading the ID register.
     * LMS7002M should return a known value at the status register. */
    reg_val = lms7002m_read_reg(0x002F);
    /* If we read 0xFFFF, the chip is not responding */
    if (reg_val == 0xFFFF) {
        return -1;  /* SPI communication failure */
    }

    /* Step 3: Power-down all blocks first, then selectively enable.
     * This ensures a known clean state regardless of previous configuration. */
    lms7002m_write_reg(REG_POWER_DOWN, 0x03FF);  /* Power down all */
    for (volatile int i = 0; i < 1500; i++)
        __asm__("nop");  /* ~100 μs */

    /* Step 4: Enable the reference clock path and PLL. */
    lms7002m_write_reg(REG_REFCLK_DIV, 0x0000);  /* No reference clock division */
    lms7002m_write_reg(REG_SYNTH_EN, 0x0001);   /* Enable synthesizer */

    /* Step 5: Configure RX path for default settings.
     * Using channel A (LNAW path) at 868 MHz as default. */
    lms7002m_tune_rx(868000000UL, 2000, 300);

    /* Step 6: Enable RX path only (TX requires explicit enable) */
    lms7002m_write_reg(REG_RX_ENABLE, 0x0001);   /* Enable RX */
    lms7002m_write_reg(REG_TX_ENABLE, 0x0000);   /* TX disabled */

    /* Step 7: Configure baseband filter and decimation.
     * Default: 2 MHz bandwidth, 256 ksps sample rate (decimation=122) */
    lms7002m_set_rx_bandwidth(2000);   /* 2 MHz bandwidth */
    lms7002m_set_rx_decimation(16);     /* Decimation by 16 for ~256 ksps */

    return 0;
}

/**
 * lms7002m_tune_rx — Tune the LMS7002M RX frequency
 *
 * @freq_hz:    Center frequency in Hz (100 kHz to 3.8 GHz)
 * @bw_khz:     Bandwidth in kHz (ignored if <= 0)
 * @gain_db_x10: LNA gain in dB × 10 (e.g., 300 = 30.0 dB)
 *
 * Configures the RX PLL, VCO divider, baseband filter, and LNA gain.
 */
void lms7002m_tune_rx(uint32_t freq_hz, uint16_t bw_khz, uint16_t gain_db_x10) {
    uint16_t n_int;
    uint32_t n_frac;
    uint8_t div_ratio;

    if (lms7002m_calc_pll_params(freq_hz, &n_int, &n_frac, &div_ratio) != 0)
        return;  /* Invalid frequency */

    /* Program RX PLL integer and fractional dividers */
    lms7002m_write_reg(REG_RX_PLL_INT, n_int);
    lms7002m_write_reg(REG_RX_PLL_FRAC, (uint16_t)(n_frac & 0xFFFF));
    lms7002m_write_reg(REG_RX_PLL_FRAC + 1, (uint16_t)((n_frac >> 16) & 0x007F));

    /* Program VCO divider */
    /* DIV_RATIO is encoded in the CGP register:
     * 0=div1, 1=div2, 2=div4, 3=div8, 4=div16, 5=div32, 6=div64 */
    uint16_t div_enc = 0;
    switch (div_ratio) {
    case 1:  div_enc = 0; break;
    case 2:  div_enc = 1; break;
    case 4:  div_enc = 2; break;
    case 8:  div_enc = 3; break;
    case 16: div_enc = 4; break;
    case 32: div_enc = 5; break;
    case 64: div_enc = 6; break;
    default: div_enc = 0; break;
    }
    uint16_t cgp_reg = lms7002m_read_reg(REG_CGP_INT);
    cgp_reg = (cgp_reg & 0xFF8F) | (div_enc << 4);
    lms7002m_write_reg(REG_CGP_INT, cgp_reg);

    /* Set bandwidth if specified */
    if (bw_khz > 0) {
        lms7002m_set_rx_bandwidth(bw_khz);
    }

    /* Set LNA gain */
    lms7002m_set_rx_gain(gain_db_x10);

    /* Allow PLL to lock (~200 μs) */
    for (volatile int i = 0; i < 30000; i++)
        __asm__("nop");
}

/**
 * lms7002m_set_rx_bandwidth — Set the LMS7002M RX baseband filter bandwidth
 *
 * @bw_khz: Bandwidth in kHz (e.g., 2000 for 2 MHz)
 *
 * Configures the RX baseband filter bandwidth and adjusts the
 * decimation ratio to maintain the desired sample rate.
 */
void lms7002m_set_rx_bandwidth(uint16_t bw_khz) {
    /* Baseband filter bandwidth is controlled by the RX_BBFILT register.
     * The LMS7002M supports bandwidths from ~1.5 MHz to ~61.44 MHz.
     *
     * Simplified mapping for common bandwidths:
     *   BW_kHz → RX_BBFILT value (approximate)
     *
     * For now, use a simplified direct mapping. The actual LMS7002M
     * bandwidth configuration requires calibration table lookup
     * and is typically done via LimeSuite calibration. */
    uint16_t bb_val;

    if (bw_khz <= 1500)
        bb_val = 0x0C00;  /* ~1.5 MHz */
    else if (bw_khz <= 2500)
        bb_val = 0x0A00;  /* ~2.5 MHz */
    else if (bw_khz <= 5000)
        bb_val = 0x0800;  /* ~5 MHz */
    else if (bw_khz <= 10000)
        bb_val = 0x0400;  /* ~10 MHz */
    else if (bw_khz <= 20000)
        bb_val = 0x0200;  /* ~20 MHz */
    else
        bb_val = 0x0100;  /* >20 MHz (up to 61.44 MHz) */

    lms7002m_write_reg(REG_RX_BBFILT, bb_val);
}

/**
 * lms7002m_set_rx_gain — Set the LNA gain for the RX path
 *
 * @gain_db_x10: Gain in dB × 10 (e.g., 300 = 30.0 dB)
 *
 * Maps the gain value to LMS7002M LNA and TIA gain settings.
 * The LMS7002M has ~73 dB of total RX gain range, configurable
 * via LNA gain steps and TIA gain control.
 */
void lms7002m_set_rx_gain(uint16_t gain_db_x10) {
    /* LMS7002M LNA gain control:
     * The RFE_LNA_GAIN register controls the LNA gain in ~3 dB steps.
     * The TIA gain provides additional ~12 dB of gain control.
     *
     * Simplified mapping (gain_db_x10 / 10 = gain in dB):
     *   0-15 dB: LNA lowest gain + TIA lowest
     *   15-30 dB: LNA medium gain
     *   30-45 dB: LNA high gain
     *   45-73 dB: LNA maximum gain + TIA maximum */
    uint16_t lna_gain;
    uint16_t tia_gain;

    if (gain_db_x10 < 150) {
        lna_gain = 0x00;  /* LNA minimum gain */
        tia_gain = 0x00;  /* TIA minimum gain */
    } else if (gain_db_x10 < 300) {
        lna_gain = 0x01;  /* LNA medium gain */
        tia_gain = 0x00;
    } else if (gain_db_x10 < 450) {
        lna_gain = 0x02;  /* LNA high gain */
        tia_gain = 0x01;
    } else {
        lna_gain = 0x03;  /* LNA maximum gain */
        tia_gain = 0x03;  /* TIA maximum gain */
    }

    lms7002m_write_reg(REG_RFE_LNA_GAIN, lna_gain);
    lms7002m_write_reg(REG_RFE_LNA_MODE, 0x0001);  /* LNAW path selected */
    lms7002m_write_reg(REG_RFE_TIA_GAIN, tia_gain);
}

/**
 * lms7002m_set_rx_decimation — Set the RX decimation ratio
 *
 * @decimation: Decimation factor (1, 2, 4, 8, 16, 32, or 64)
 *
 * The LMS7002M ADC runs at a fixed rate. Decimation reduces the
 * output sample rate:
 *   f_sample = f_ADC / decimation
 *   At 30.72 MHz ADC rate:
 *     div 1  = 30.72 MSPS
 *     div 2  = 15.36 MSPS
 *     div 4  = 7.68 MSPS
 *     div 8  = 3.84 MSPS
 *     div 16 = 1.92 MSPS
 *     div 32 = 960 kSPS
 *     div 64 = 480 kSPS
 */
void lms7002m_set_rx_decimation(uint16_t decimation) {
    /* Map decimation factor to register value.
     * REG_RX_DECIMATION format: bits[2:0] encode the factor.
     * 0=div1, 1=div2, 2=div4, 3=div8, 4=div16, 5=div32, 6=div64 */
    uint16_t dec_val;

    switch (decimation) {
    case 1:   dec_val = 0; break;
    case 2:   dec_val = 1; break;
    case 4:   dec_val = 2; break;
    case 8:   dec_val = 3; break;
    case 16:  dec_val = 4; break;
    case 32:  dec_val = 5; break;
    case 64:  dec_val = 6; break;
    default:  dec_val = 4; break;  /* Default: div16 (~1.92 MSPS) */
    }

    lms7002m_write_reg(REG_RX_DECIMATION, dec_val);
}

/**
 * lms7002m_enable_rx — Enable the LMS7002M RX path
 *
 * Enables the LNA, mixer, TIA, and ADC in the RX chain.
 * The LMS7002M starts producing IQ data on the MIPI CSI-2 interface.
 */
void lms7002m_enable_rx(void) {
    rp2350b_gpio_set(PIN_SDR_GPIO1, true);   /* RX enable */
    rp2350b_gpio_set(PIN_SDR_LNA_EN, true);  /* LNA enable */
    lms7002m_write_reg(REG_RX_ENABLE, 0x0001);
}

/**
 * lms7002m_disable_rx — Disable the LMS7002M RX path
 */
void lms7002m_disable_rx(void) {
    lms7002m_write_reg(REG_RX_ENABLE, 0x0000);
    rp2350b_gpio_set(PIN_SDR_GPIO1, false);  /* RX disable */
    rp2350b_gpio_set(PIN_SDR_LNA_EN, false); /* LNA disable */
}

/**
 * lms7002m_enable_tx — Enable the LMS7002M TX path
 *
 * Enables the DAC, mixer, PA driver, and power amplifier.
 * WARNING: Transmitting on many frequencies requires a license.
 */
void lms7002m_enable_tx(void) {
    rp2350b_gpio_set(PIN_SDR_GPIO0, true);  /* TX enable */
    lms7002m_write_reg(REG_TX_ENABLE, 0x0001);
}

/**
 * lms7002m_disable_tx — Disable the LMS7002M TX path
 */
void lms7002m_disable_tx(void) {
    lms7002m_write_reg(REG_TX_ENABLE, 0x0000);
    rp2350b_gpio_set(PIN_SDR_GPIO0, false);  /* TX disable */
}

/**
 * lms7002m_power_down — Put the LMS7002M into low-power mode
 *
 * Disables all blocks and sets the power-down register.
 */
void lms7002m_power_down(void) {
    lms7002m_write_reg(REG_TX_ENABLE, 0x0000);
    lms7002m_write_reg(REG_RX_ENABLE, 0x0000);
    lms7002m_write_reg(REG_POWER_DOWN, 0x03FF);  /* Power down all blocks */
    rp2350b_gpio_set(PIN_SDR_GPIO0, false);
    rp2350b_gpio_set(PIN_SDR_GPIO1, false);
    rp2350b_gpio_set(PIN_SDR_LNA_EN, false);
}

/**
 * lms7002m_read_rssi — Read the LMS7002M RSSI value
 *
 * Returns: RSSI in dBm × 10 (signed, e.g., -740 = -74.0 dBm)
 *
 * This is a simplified implementation that reads the LMS7002M
 * internal RSSI register. For accurate RSSI, the LMS7002M needs
 * to be in RX mode with AGC enabled.
 */
int16_t lms7002m_read_rssi(void) {
    /* The LMS7002M RSSI register provides an 8-bit signed value
     * that is approximately proportional to the received signal level.
     * The exact mapping requires calibration with a known signal source.
     *
     * Simplified formula: RSSI_dBm = register_value - 127 (approximate)
     * In dBm × 10: RSSI_x10 = (register_value - 127) * 10 */
    uint16_t raw = lms7002m_read_reg(0x0400);  /* RSSI register address */
    int16_t rssi_x10 = ((int16_t)(raw & 0xFF) - 127) * 10;
    return rssi_x10;
}

/**
 * lms7002m_get_field_strength_mv — Get estimated field strength in mV
 *
 * This provides a rough estimate of the RF field strength at the
 * antenna, useful for antenna testing and debug.
 *
 * Returns: Estimated field strength in mV (0-5000)
 */
uint16_t lms7002m_get_field_strength_mv(void) {
    int16_t rssi_x10 = lms7002m_read_rssi();
    /* Convert from dBm×10 to approximate mV across 50Ω:
     * V_rms = 10^((dBm + 13) / 20) * 1000 / sqrt(2) */
    /* Simplified: approximate mV from RSSI */
    int32_t rssi_dbm = rssi_x10 / 10;
    if (rssi_dbm < -100) rssi_dbm = -100;
    if (rssi_dbm > 0) rssi_dbm = 0;

    /* Very rough mapping: -100 dBm → 2 mV, 0 dBm → 224 mV */
    uint16_t mv = (uint16_t)((rssi_dbm + 100) * 224 / 100);
    return mv;
}