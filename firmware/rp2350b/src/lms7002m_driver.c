/*
 * lms7002m_driver.c — LMS7002M SDR Transceiver Driver for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the LMS7002M wideband SDR transceiver driver
 * for the RP2350B co-processor on the GhostBlade platform. It provides:
 *
 *   1. Chip initialization and reset sequencing
 *   2. PLL frequency synthesis (VCO_L: 1.88–3.72 GHz, VCO_H: 3.72–5.8 GHz)
 *   3. RX/TX gain distribution across LNA, TIA, and PGA stages
 *   4. ADC/DAC sample rate configuration with decimation/interpolation
 *   5. FIFO-based IQ data streaming
 *   6. DC offset and IQ imbalance calibration
 *   7. SPI register access (single and burst modes)
 *
 * The LMS7002M uses a 4-byte SPI command format:
 *   Write: [0][addr13:0][data15:0]
 *   Read:  [1][addr13:0][don't care]  →  response: [data15:0]
 *
 * The RP2350B communicates with the RK3576 host through the SPI protocol
 * handler (spi_protocol.c), which encapsulates SDR control commands and
 * IQ data streams in framed packets with CRC-16 validation.
 *
 * References:
 *   - LMS7002M Data Sheet v3.1r00 (Lime Microsystems)
 *   - LimeSuite (https://github.com/myriadrf/LimeSuite)
 */

#include "lms7002m_driver.h"
#include <string.h>

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

/** Minimum VCO frequency (Hz) — covers both VCO_L and VCO_H ranges */
#define VCO_MIN_HZ          1880000000ULL   /* 1.88 GHz */
#define VCO_MAX_HZ           5800000000ULL   /* 5.80 GHz */
#define VCO_H_THRESHOLD_HZ  3720000000ULL   /* VCO_H starts at 3.72 GHz */

/** Maximum SPI transfer size (bytes) */
#define LMS7002M_SPI_MAX_XFER  256

/** Chip reset timeout (ms) */
#define LMS7002M_RESET_TIMEOUT_MS  50

/** PLL lock timeout (ms) */
#define LMS7002M_PLL_LOCK_TIMEOUT_MS  100

/** Calibration timeouts (ms) */
#define LMS7002M_DC_CAL_TIMEOUT_MS    500
#define LMS7002M_IQ_CAL_TIMEOUT_MS    1000

/* ========================================================================
 * SPI Interface (hardware-specific, to be implemented by platform)
 * ======================================================================== */

/**
 * Platform SPI transfer function.
 * Sends and receives 4 bytes (32 bits) in a single SPI transaction.
 *
 * @param tx_data: 32-bit command word to send
 * @return: 32-bit response word received
 *
 * This function must be implemented by the platform-specific SPI layer.
 * It should handle chip-select assertion/deassertion and bit ordering.
 */
extern uint32_t lms7002m_platform_spi_xfer(uint32_t tx_data);

/**
 * Platform delay function.
 * @param ms: Delay in milliseconds
 */
extern void lms7002m_platform_delay_ms(uint32_t ms);

/**
 * Platform timestamp function.
 * @return: Current timestamp in milliseconds
 */
extern uint32_t lms7002m_platform_timestamp_ms(void);

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * wait_for_bitmask — Poll a register until a bitmask matches or timeout.
 *
 * @drv: Driver state
 * @addr: Register address to poll
 * @mask: Bitmask to check
 * @value: Expected value (after masking)
 * @timeout_ms: Maximum time to wait in milliseconds
 *
 * Returns: LMS7002M_OK if the mask matches within the timeout,
 *          LMS7002M_ERR_TIMEOUT otherwise.
 */
static int wait_for_bitmask(const struct lms7002m_driver *drv,
                             uint16_t addr, uint16_t mask,
                             uint16_t value, uint32_t timeout_ms)
{
    uint32_t start = lms7002m_platform_timestamp_ms();

    while (1) {
        int reg_val = lms7002m_spi_read(drv, addr);
        if (reg_val < 0)
            return reg_val;

        if ((uint16_t)(reg_val & mask) == value)
            return LMS7002M_OK;

        if (lms7002m_platform_timestamp_ms() - start >= timeout_ms)
            return LMS7002M_ERR_TIMEOUT;

        lms7002m_platform_delay_ms(1);
    }
}

/* ========================================================================
 * Public API — Initialization
 * ======================================================================== */

int lms7002m_init(struct lms7002m_driver *drv, uint32_t spi_speed_hz)
{
    int ret;

    if (!drv)
        return LMS7002M_ERR_INVALID_PARAM;

    if (spi_speed_hz > 20000000)
        return LMS7002M_ERR_INVALID_PARAM;

    memset(drv, 0, sizeof(*drv));
    drv->spi_speed_hz = spi_speed_hz;

    /* Soft-reset the chip */
    ret = lms7002m_reset(drv);
    if (ret != LMS7002M_OK)
        return ret;

    /* Verify chip revision */
    int rev = lms7002m_spi_read(drv, LMS7002M_REV);
    if (rev < 0)
        return LMS7002M_ERR_SPI;

    /* Configure defaults:
     *   - RX channel A, gain = 0 dB
     *   - TX channel A, gain = 0 dB
     *   - Sample rate = 2 MSPS
     *   - FIFO watermark = 50% */
    drv->active_channel = LMS7002M_CH_A;
    drv->rx_gain.lna_gain = 0;
    drv->rx_gain.tia_enable = true;
    drv->rx_gain.pga_gain = 0;
    drv->tx_gain.tx_gain = 0;
    drv->rate.sample_rate = 2000000;
    drv->rate.decimation = 4;
    drv->rate.interpolation = 4;
    drv->fifo.watermark = LMS7002M_FIFO_DEPTH / 2;
    drv->fifo.enabled = false;
    drv->fifo.format = 0;

    /* Enable power to all RF sections */
    ret = lms7002m_spi_write(drv, LMS7002M_TOP, 0x0001);
    if (ret != LMS7002M_OK)
        return ret;

    drv->initialized = true;
    return LMS7002M_OK;
}

void lms7002m_deinit(struct lms7002m_driver *drv)
{
    if (!drv || !drv->initialized)
        return;

    /* Stop streaming if active */
    lms7002m_stop_rx(drv);
    lms7002m_stop_tx(drv);

    /* Disable FIFO */
    drv->fifo.enabled = false;
    lms7002m_spi_write(drv, LMS7002M_FIFO_CTRL, 0x0000);

    /* Power down RF sections */
    lms7002m_spi_write(drv, LMS7002M_TOP, 0x0000);

    /* Securely wipe driver state to prevent leakage of PLL config,
     * gain settings, and frequency parameters after deinitialization. */
    volatile uint8_t *p = (volatile uint8_t *)drv;
    for (size_t i = 0; i < sizeof(*drv); i++)
        p[i] = 0;

    drv->initialized = false;
}

/* ========================================================================
 * Public API — PLL Tuning
 * ======================================================================== */

int lms7002m_calculate_pll_params(uint64_t freq_hz, uint32_t ref_clk,
                                   uint16_t *nint, uint32_t *nfrac,
                                   uint8_t *div_out)
{
    uint32_t div;
    uint64_t vco_freq;
    uint64_t pll_ratio;
    uint32_t nint_val;
    uint32_t nfrac_val;

    if (freq_hz < LMS7002M_FREQ_MIN || freq_hz > LMS7002M_FREQ_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    if (!ref_clk || !nint || !nfrac || !div_out)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Select output divider to keep VCO in range (1.88 – 5.8 GHz)
     * The LMS7002M has two VCO ranges:
     *   VCO_L: 1.88 – 3.72 GHz (lower frequencies)
     *   VCO_H: 3.72 – 5.8 GHz (higher frequencies)
     * The chip automatically selects the correct VCO band.
     *
     * Divider values: 1, 2, 4, 8, 16, 32 (powers of 2 only). */
    *div_out = 0;
    for (div = 1; div <= 32; div *= 2) {
        vco_freq = freq_hz * div;

        if (vco_freq >= VCO_MIN_HZ && vco_freq <= VCO_MAX_HZ) {
            *div_out = (uint8_t)div;
            break;
        }
    }

    /* If no valid VCO frequency found, the requested frequency is
     * outside the synthesizable range. */
    if (*div_out == 0)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Determine VCO band */
    /* vco_high is set if VCO frequency >= 3.72 GHz threshold */

    /* Calculate PLL parameters:
     * f_VCO = f_REF × (NINT + NFRAC / 2^24)
     * → (NINT + NFRAC / 2^24) = f_VCO / f_REF
     * → PLL ratio = (f_VCO × 2^24) / f_REF
     * → NINT = PLL ratio >> 24
     * → NFRAC = PLL ratio & 0xFFFFFF */
    pll_ratio = (vco_freq << 24) / ref_clk;
    nint_val = (uint32_t)(pll_ratio >> 24);
    nfrac_val = (uint32_t)(pll_ratio & 0x00FFFFFFUL);

    if (nint_val < LMS7002M_PLL_INT_MIN || nint_val > LMS7002M_PLL_INT_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    if (nfrac_val > 0x7FFFFFUL)
        nfrac_val = 0x7FFFFFUL;

    *nint = (uint16_t)nint_val;
    *nfrac = nfrac_val;
    return LMS7002M_OK;
}

int lms7002m_configure_rx(struct lms7002m_driver *drv,
                           uint64_t freq_hz, uint32_t sample_rate,
                           enum lms7002m_channel ch)
{
    int ret;
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (freq_hz < LMS7002M_FREQ_MIN || freq_hz > LMS7002M_FREQ_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    if (sample_rate < LMS7002M_SAMPLE_RATE_MIN ||
        sample_rate > LMS7002M_SAMPLE_RATE_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    if (ch >= LMS7002M_CH_COUNT)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Calculate PLL parameters */
    ret = lms7002m_calculate_pll_params(freq_hz, LMS7002M_REF_CLK_HZ,
                                         &nint, &nfrac, &div_out);
    if (ret != LMS7002M_OK)
        return ret;

    /* Select channel */
    lms7002m_set_channel(drv, ch);

    /* Program SXR PLL (RX synthesizer) */
    ret = lms7002m_spi_write(drv, LMS7002M_SXR_PLL_INT, nint);
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_SXR_PLL_FRAC_H,
                              (uint16_t)((nfrac >> 16) & 0xFF));
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_SXR_PLL_FRAC_L,
                              (uint16_t)(nfrac & 0xFFFF));
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_SXR_DIV, div_out);
    if (ret != LMS7002M_OK) return ret;

    /* Trigger VCO calibration */
    ret = lms7002m_spi_write(drv, LMS7002M_SXR_VCO_CTRL, 0x0001);
    if (ret != LMS7002M_OK) return ret;

    /* Wait for PLL lock */
    ret = wait_for_bitmask(drv, LMS7002M_SXR_VCO_CTRL, 0x0002, 0x0002,
                            LMS7002M_PLL_LOCK_TIMEOUT_MS);
    if (ret != LMS7002M_OK)
        return LMS7002M_ERR_PLL_LOCK;

    /* Configure sample rate */
    uint8_t decimation;
    if (sample_rate >= 5000000)       decimation = 1;
    else if (sample_rate >= 2500000)  decimation = 2;
    else if (sample_rate >= 1250000)  decimation = 4;
    else if (sample_rate >= 625000)   decimation = 8;
    else if (sample_rate >= 312500)   decimation = 16;
    else                               decimation = 32;

    ret = lms7002m_spi_write(drv, LMS7002M_ADC_DECIMATION, decimation);
    if (ret != LMS7002M_OK) return ret;

    /* Store configuration */
    drv->rx_frequency_hz = freq_hz;
    drv->rx_pll.nint = nint;
    drv->rx_pll.nfrac = nfrac;
    drv->rx_pll.div_out = div_out;
    drv->rx_pll.vco_high = (freq_hz * div_out >= VCO_H_THRESHOLD_HZ);
    drv->rate.sample_rate = sample_rate;
    drv->rate.decimation = decimation;

    return LMS7002M_OK;
}

int lms7002m_configure_tx(struct lms7002m_driver *drv,
                           uint64_t freq_hz, uint32_t sample_rate,
                           enum lms7002m_channel ch)
{
    int ret;
    uint16_t nint;
    uint32_t nfrac;
    uint8_t div_out;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (freq_hz < LMS7002M_FREQ_MIN || freq_hz > LMS7002M_FREQ_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    if (sample_rate < LMS7002M_SAMPLE_RATE_MIN ||
        sample_rate > LMS7002M_SAMPLE_RATE_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    if (ch >= LMS7002M_CH_COUNT)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Calculate PLL parameters */
    ret = lms7002m_calculate_pll_params(freq_hz, LMS7002M_REF_CLK_HZ,
                                         &nint, &nfrac, &div_out);
    if (ret != LMS7002M_OK)
        return ret;

    /* Select channel */
    lms7002m_set_channel(drv, ch);

    /* Program SXT PLL (TX synthesizer) */
    ret = lms7002m_spi_write(drv, LMS7002M_SXT_PLL_INT, nint);
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_SXT_PLL_FRAC_H,
                              (uint16_t)((nfrac >> 16) & 0xFF));
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_SXT_PLL_FRAC_L,
                              (uint16_t)(nfrac & 0xFFFF));
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_SXT_DIV, div_out);
    if (ret != LMS7002M_OK) return ret;

    /* Trigger VCO calibration */
    ret = lms7002m_spi_write(drv, LMS7002M_SXT_VCO_CTRL, 0x0001);
    if (ret != LMS7002M_OK) return ret;

    /* Wait for PLL lock */
    ret = wait_for_bitmask(drv, LMS7002M_SXT_VCO_CTRL, 0x0002, 0x0002,
                            LMS7002M_PLL_LOCK_TIMEOUT_MS);
    if (ret != LMS7002M_OK)
        return LMS7002M_ERR_PLL_LOCK;

    /* Configure sample rate */
    uint8_t interpolation;
    if (sample_rate >= 5000000)        interpolation = 1;
    else if (sample_rate >= 2500000)  interpolation = 2;
    else if (sample_rate >= 1250000)  interpolation = 4;
    else if (sample_rate >= 625000)   interpolation = 8;
    else if (sample_rate >= 312500)   interpolation = 16;
    else                               interpolation = 32;

    ret = lms7002m_spi_write(drv, LMS7002M_DAC_INTERPOLATION, interpolation);
    if (ret != LMS7002M_OK) return ret;

    /* Store configuration */
    drv->tx_frequency_hz = freq_hz;
    drv->tx_pll.nint = nint;
    drv->tx_pll.nfrac = nfrac;
    drv->tx_pll.div_out = div_out;
    drv->tx_pll.vco_high = (freq_hz * div_out >= VCO_H_THRESHOLD_HZ);
    drv->rate.interpolation = interpolation;

    return LMS7002M_OK;
}

/* ========================================================================
 * Public API — Gain Control
 * ======================================================================== */

int lms7002m_set_rx_gain(struct lms7002m_driver *drv, int16_t gain_db_x10)
{
    uint8_t lna_gain, pga_gain;
    int16_t remaining;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (gain_db_x10 < 0 || gain_db_x10 > LMS7002M_RX_GAIN_TOTAL_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Subtract TIA fixed gain (12 dB = 120 units) if enabled */
    remaining = gain_db_x10;
    if (drv->rx_gain.tia_enable)
        remaining -= LMS7002M_TIA_GAIN_FIXED;

    if (remaining < 0)
        remaining = 0;

    /* Distribute gain: LNA first (0–73 dB), then PGA (0–31 dB) */
    if (remaining <= LMS7002M_LNA_GAIN_MAX) {
        lna_gain = (uint8_t)(remaining / 10);
        pga_gain = 0;
    } else {
        lna_gain = LMS7002M_LNA_GAIN_MAX / 10; /* 73 */
        remaining -= LMS7002M_LNA_GAIN_MAX;
        if (remaining > LMS7002M_PGA_GAIN_MAX)
            remaining = LMS7002M_PGA_GAIN_MAX;
        pga_gain = (uint8_t)(remaining / 10);
    }

    /* Write gain registers */
    int ret = lms7002m_spi_write(drv, LMS7002M_RX_GAIN_LNA, lna_gain);
    if (ret != LMS7002M_OK) return ret;

    ret = lms7002m_spi_write(drv, LMS7002M_RX_GAIN_PGA, pga_gain);
    if (ret != LMS7002M_OK) return ret;

    /* Update state */
    drv->rx_gain.lna_gain = lna_gain;
    drv->rx_gain.pga_gain = pga_gain;

    return LMS7002M_OK;
}

int lms7002m_set_tx_gain(struct lms7002m_driver *drv, int16_t gain_db_x10)
{
    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (gain_db_x10 < 0 || gain_db_x10 > LMS7002M_TX_GAIN_MAX)
        return LMS7002M_ERR_INVALID_PARAM;

    uint8_t tx_gain_idx = (uint8_t)(gain_db_x10 / 10);

    int ret = lms7002m_spi_write(drv, LMS7002M_TX_GAIN, tx_gain_idx);
    if (ret != LMS7002M_OK)
        return ret;

    drv->tx_gain.tx_gain = tx_gain_idx;
    return LMS7002M_OK;
}

/* ========================================================================
 * Public API — Streaming Control
 * ======================================================================== */

int lms7002m_start_rx(struct lms7002m_driver *drv)
{
    int ret;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    /* Enable FIFO with watermark */
    uint16_t fifo_ctrl = 0x0001 | ((uint16_t)drv->fifo.format << 1);
    ret = lms7002m_spi_write(drv, LMS7002M_FIFO_CTRL, fifo_ctrl);
    if (ret != LMS7002M_OK) return ret;

    drv->fifo.enabled = true;

    /* Enable RX streaming */
    ret = lms7002m_spi_write(drv, LMS7002M_STREAM_CTRL, 0x0001);
    if (ret != LMS7002M_OK) return ret;

    return LMS7002M_OK;
}

void lms7002m_stop_rx(struct lms7002m_driver *drv)
{
    if (!drv || !drv->initialized)
        return;

    lms7002m_spi_write(drv, LMS7002M_STREAM_CTRL, 0x0000);
    lms7002m_spi_write(drv, LMS7002M_FIFO_CTRL, 0x0000);
    drv->fifo.enabled = false;
}

int lms7002m_start_tx(struct lms7002m_driver *drv)
{
    int ret;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    /* Enable TX streaming */
    ret = lms7002m_spi_write(drv, LMS7002M_STREAM_CTRL, 0x0002);
    if (ret != LMS7002M_OK)
        return ret;

    return LMS7002M_OK;
}

void lms7002m_stop_tx(struct lms7002m_driver *drv)
{
    if (!drv || !drv->initialized)
        return;

    lms7002m_spi_write(drv, LMS7002M_STREAM_CTRL, 0x0000);
}

/* ========================================================================
 * Public API — Calibration
 * ======================================================================== */

int lms7002m_calibrate_dc(struct lms7002m_driver *drv,
                           enum lms7002m_channel ch, uint32_t timeout_ms)
{
    int ret;
    uint16_t cal_ctrl;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (ch >= LMS7002M_CH_COUNT)
        return LMS7002M_ERR_INVALID_PARAM;

    lms7002m_set_channel(drv, ch);

    /* Start DC offset calibration */
    cal_ctrl = 0x0001;  /* Bit 0: DC cal enable */
    ret = lms7002m_spi_write(drv, LMS7002M_CAL_CTRL, cal_ctrl);
    if (ret != LMS7002M_OK)
        return ret;

    /* Wait for completion */
    ret = wait_for_bitmask(drv, LMS7002M_CAL_STATUS, 0x0001, 0x0001,
                            timeout_ms ? timeout_ms : LMS7002M_DC_CAL_TIMEOUT_MS);
    if (ret != LMS7002M_OK)
        return LMS7002M_ERR_CALIBRATION;

    /* Read calibration results */
    int result_h = lms7002m_spi_read(drv, LMS7002M_CAL_RESULT_H);
    int result_l = lms7002m_spi_read(drv, LMS7002M_CAL_RESULT_L);

    if (result_h < 0 || result_l < 0)
        return LMS7002M_ERR_SPI;

    drv->cal.dc_offset_i = (int16_t)((result_h << 8) | ((result_l >> 8) & 0xFF));
    drv->cal.dc_offset_q = (int16_t)((result_l & 0xFF) - ((result_l & 0x80) ? 0x100 : 0));
    drv->cal.dc_complete = true;

    /* Clear calibration enable bit */
    lms7002m_spi_write(drv, LMS7002M_CAL_CTRL, 0x0000);

    return LMS7002M_OK;
}

int lms7002m_calibrate_iq(struct lms7002m_driver *drv,
                           enum lms7002m_direction direction,
                           enum lms7002m_channel ch, uint32_t timeout_ms)
{
    int ret;
    uint16_t cal_ctrl;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (ch >= LMS7002M_CH_COUNT)
        return LMS7002M_ERR_INVALID_PARAM;

    lms7002m_set_channel(drv, ch);

    /* Start IQ calibration:
     * Bit 0: DC cal enable (already done)
     * Bit 1: IQ gain cal enable
     * Bit 2: IQ phase cal enable */
    cal_ctrl = 0x0006;  /* IQ gain + phase calibration */

    ret = lms7002m_spi_write(drv, LMS7002M_CAL_CTRL, cal_ctrl);
    if (ret != LMS7002M_OK)
        return ret;

    /* Wait for IQ gain calibration completion */
    ret = wait_for_bitmask(drv, LMS7002M_CAL_STATUS, 0x0002, 0x0002,
                            timeout_ms ? timeout_ms : LMS7002M_IQ_CAL_TIMEOUT_MS);
    if (ret != LMS7002M_OK)
        return LMS7002M_ERR_CALIBRATION;

    /* Wait for IQ phase calibration completion */
    ret = wait_for_bitmask(drv, LMS7002M_CAL_STATUS, 0x0004, 0x0004,
                            timeout_ms ? timeout_ms : LMS7002M_IQ_CAL_TIMEOUT_MS);
    if (ret != LMS7002M_OK)
        return LMS7002M_ERR_CALIBRATION;

    /* Read results */
    int result_h = lms7002m_spi_read(drv, LMS7002M_CAL_RESULT_H);
    int result_l = lms7002m_spi_read(drv, LMS7002M_CAL_RESULT_L);

    if (result_h < 0 || result_l < 0)
        return LMS7002M_ERR_SPI;

    drv->cal.iq_gain_corr = (int16_t)(result_h & 0x3FF);
    drv->cal.iq_phase_corr = (int16_t)(result_l & 0x1FF);
    drv->cal.iq_gain_complete = true;
    drv->cal.iq_phase_complete = true;

    /* Clear calibration enable bits */
    lms7002m_spi_write(drv, LMS7002M_CAL_CTRL, 0x0000);

    return LMS7002M_OK;
}

/* ========================================================================
 * Public API — FIFO / Data Streaming
 * ======================================================================== */

int lms7002m_read_fifo(struct lms7002m_driver *drv,
                        int16_t *buf, uint16_t num_samples)
{
    uint16_t fifo_fill;
    bool pll_locked;
    int ret;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (!buf || num_samples == 0)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Check FIFO status */
    ret = lms7002m_get_status(drv, &fifo_fill, &pll_locked);
    if (ret != LMS7002M_OK)
        return ret;

    /* Cap read to available samples */
    if (fifo_fill < num_samples)
        num_samples = fifo_fill;

    /* Read samples in burst mode (2 × 16-bit per sample: I then Q) */
    for (uint16_t i = 0; i < num_samples; i++) {
        int i_val = lms7002m_spi_read(drv, LMS7002M_FIFO_STATUS);
        if (i_val < 0)
            return i_val;
        buf[i * 2] = (int16_t)i_val;

        int q_val = lms7002m_spi_read(drv, LMS7002M_FIFO_STATUS);
        if (q_val < 0)
            return q_val;
        buf[i * 2 + 1] = (int16_t)q_val;
    }

    return num_samples;
}

int lms7002m_write_fifo(struct lms7002m_driver *drv,
                         const int16_t *buf, uint16_t num_samples)
{
    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    if (!buf || num_samples == 0)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Write samples in burst mode (2 × 16-bit per sample: I then Q) */
    for (uint16_t i = 0; i < num_samples; i++) {
        int ret = lms7002m_spi_write(drv, LMS7002M_FIFO_STATUS,
                                       (uint16_t)buf[i * 2]);
        if (ret != LMS7002M_OK)
            return ret;

        ret = lms7002m_spi_write(drv, LMS7002M_FIFO_STATUS,
                                  (uint16_t)buf[i * 2 + 1]);
        if (ret != LMS7002M_OK)
            return ret;
    }

    return num_samples;
}

/* ========================================================================
 * Public API — SPI Register Access
 * ======================================================================== */

int lms7002m_spi_read(const struct lms7002m_driver *drv, uint16_t addr)
{
    uint32_t cmd, resp;

    if (!drv)
        return LMS7002M_ERR_INVALID_PARAM;

    if (!drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    cmd = lms7002m_cmd_read(addr);
    resp = lms7002m_platform_spi_xfer(cmd);

    /* Response format: [0][addr13:0][data15:0] */
    return (int16_t)(resp & 0xFFFF);
}

int lms7002m_spi_write(struct lms7002m_driver *drv, uint16_t addr, uint16_t data)
{
    uint32_t cmd;

    if (!drv)
        return LMS7002M_ERR_INVALID_PARAM;

    cmd = lms7002m_cmd_write(addr, data);
    lms7002m_platform_spi_xfer(cmd);

    return LMS7002M_OK;
}

int lms7002m_spi_read_burst(const struct lms7002m_driver *drv, uint16_t start_addr,
                              uint16_t *buf, uint16_t num_regs)
{
    uint16_t i;

    if (!drv || !buf)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Prevent register address from wrapping around 16-bit space */
    if (num_regs == 0 || (uint32_t)start_addr + num_regs > 0x4000)
        return LMS7002M_ERR_INVALID_PARAM;

    for (i = 0; i < num_regs; i++) {
        int val = lms7002m_spi_read(drv, start_addr + i);
        if (val < 0)
            return val;
        buf[i] = (uint16_t)val;
    }

    return num_regs;
}

int lms7002m_spi_write_burst(struct lms7002m_driver *drv, uint16_t start_addr,
                               const uint16_t *data, uint16_t num_regs)
{
    uint16_t i;
    int ret;

    if (!drv || !data)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Prevent register address from wrapping around 16-bit space */
    if (num_regs == 0 || (uint32_t)start_addr + num_regs > 0x4000)
        return LMS7002M_ERR_INVALID_PARAM;

    for (i = 0; i < num_regs; i++) {
        ret = lms7002m_spi_write(drv, start_addr + i, data[i]);
        if (ret != LMS7002M_OK)
            return ret;
    }

    return num_regs;
}

/* ========================================================================
 * Public API — Reset and Channel Selection
 * ======================================================================== */

int lms7002m_reset(struct lms7002m_driver *drv)
{
    int ret;

    if (!drv)
        return LMS7002M_ERR_INVALID_PARAM;

    /* Assert reset */
    ret = lms7002m_spi_write(drv, LMS7002M_RESET, 0x0001);
    if (ret != LMS7002M_OK)
        return ret;

    lms7002m_platform_delay_ms(10);

    /* Release reset */
    ret = lms7002m_spi_write(drv, LMS7002M_RESET, 0x0000);
    if (ret != LMS7002M_OK)
        return ret;

    /* Wait for chip ready */
    lms7002m_platform_delay_ms(LMS7002M_RESET_TIMEOUT_MS);

    return LMS7002M_OK;
}

void lms7002m_set_channel(struct lms7002m_driver *drv, enum lms7002m_channel ch)
{
    if (!drv || ch >= LMS7002M_CH_COUNT)
        return;

    drv->active_channel = ch;

    /* Channel select: bit 0 = channel A, bit 1 = channel B */
    lms7002m_spi_write(drv, LMS7002M_TOP, (ch == LMS7002M_CH_B) ? 0x0002 : 0x0001);
}

enum lms7002m_channel lms7002m_get_channel(const struct lms7002m_driver *drv)
{
    if (!drv)
        return LMS7002M_CH_A;

    return drv->active_channel;
}

/* ========================================================================
 * Public API — Status
 * ======================================================================== */

int lms7002m_get_status(const struct lms7002m_driver *drv,
                         uint16_t *fifo_fill, bool *pll_locked)
{
    int status;

    if (!drv || !drv->initialized)
        return LMS7002M_ERR_NOT_INITIALIZED;

    status = lms7002m_spi_read(drv, LMS7002M_FIFO_STATUS);
    if (status < 0)
        return status;

    if (fifo_fill)
        *fifo_fill = (uint16_t)(status & 0x0FFF);

    if (pll_locked)
        *pll_locked = (status & 0x8000) != 0;

    return LMS7002M_OK;
}