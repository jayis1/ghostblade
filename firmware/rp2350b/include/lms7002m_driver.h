/*
 * lms7002m_driver.h — LMS7002M SDR Transceiver Driver API for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides initialization, tuning, and data stream control for the
 * LMS7002M dual-channel RF transceiver. The driver operates over
 * SPI (channel 1 at 20 MHz) and manages:
 *
 *   - PLL frequency synthesis (VCO_L: 1.88–3.72 GHz, VCO_H: 3.72–5.80 GHz)
 *   - RX/TX gain distribution (LNA + TIA + PGA)
 *   - ADC/DAC sample rate configuration (100 kHz – 10 MSPS)
 *   - Multi-channel MIMO operation
 *   - DC offset and IQ imbalance calibration
 *   - FIFO data streaming with DMA ring buffer support
 *
 * The LMS7002M is controlled exclusively by the RP2350B co-processor;
 * the RK3576 accesses SDR samples through the SPI protocol handler
 * and apex_bridge kernel driver.
 *
 * References:
 *   - LMS7002M Data Sheet v3.1r00
 *   - LimeSuite source code (https://github.com/myriadrf/LimeSuite)
 */

#ifndef LMS7002M_DRIVER_H
#define LMS7002M_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Constants
 * ======================================================================== */

/** Reference clock frequency (30.72 MHz) */
#define LMS7002M_REF_CLK_HZ          30720000UL

/** Minimum and maximum tunable RF frequency */
#define LMS7002M_FREQ_MIN             100000UL     /* 100 kHz  */
#define LMS7002M_FREQ_MAX             3800000000UL /* 3.8 GHz  */

/** VCO frequency ranges */
#define LMS7002M_VCO_L_MIN            1880000000ULL  /* 1.88 GHz */
#define LMS7002M_VCO_L_MAX            3720000000ULL  /* 3.72 GHz */
#define LMS7002M_VCO_H_MIN            3720000000ULL  /* 3.72 GHz */
#define LMS7002M_VCO_H_MAX            5800000000ULL  /* 5.80 GHz */

/** Sample rate range */
#define LMS7002M_SAMPLE_RATE_MIN      100000UL     /* 100 kHz  */
#define LMS7002M_SAMPLE_RATE_MAX      10000000UL   /* 10 MSPS  */

/** PLL parameters */
#define LMS7002M_PLL_INT_MIN          1
#define LMS7002M_PLL_INT_MAX          255
#define LMS7002M_PLL_FRAC_BITS       24

/** Output divider range */
#define LMS7002M_DIV_MIN              1
#define LMS7002M_DIV_MAX              32

/** Gain limits (dB × 10, e.g., 730 = 73.0 dB) */
#define LMS7002M_LNA_GAIN_MIN         0     /* 0 dB   */
#define LMS7002M_LNA_GAIN_MAX         730   /* 73 dB  */
#define LMS7002M_TIA_GAIN_FIXED       120   /* 12 dB  (fixed when enabled) */
#define LMS7002M_PGA_GAIN_MIN         0     /* 0 dB   */
#define LMS7002M_PGA_GAIN_MAX         310   /* 31 dB  */
#define LMS7002M_RX_GAIN_TOTAL_MAX    1160  /* 73+12+31 dB */
#define LMS7002M_TX_GAIN_MAX          520   /* 52 dB  */

/** FIFO depth (16-bit I/Q sample pairs) */
#define LMS7002M_FIFO_DEPTH           4096

/** SPI command timeout (ms) */
#define LMS7002M_SPI_TIMEOUT_MS        100

/* ========================================================================
 * Register Map — Top Level (page 0)
 * ======================================================================== */

#define LMS7002M_TOP                   0x0020
#define LMS7002M_REV                   0x0021
#define LMS7002M_RESET                 0x0022

/* ========================================================================
 * Register Map — SXR (RX synthesizer, page 0x10)
 * ======================================================================== */

#define LMS7002M_SXR_PLL_INT           0x0110  /* Integer division ratio (8-bit, 1–255) */
#define LMS7002M_SXR_PLL_FRAC_H       0x0111  /* Fractional ratio [23:16] */
#define LMS7002M_SXR_PLL_FRAC_L       0x0112  /* Fractional ratio [15:0]  */
#define LMS7002M_SXR_DIV               0x0113  /* Output divider            */
#define LMS7002M_SXR_VCO_CTRL          0x0114  /* VCO selection & cal      */
#define LMS7002M_SXR_VCOCAP_H          0x0115  /* VCO cap select [15:8]    */
#define LMS7002M_SXR_VCOCAP_L          0x0116  /* VCO cap select [7:0]     */

/* ========================================================================
 * Register Map — SXT (TX synthesizer, page 0x10)
 * ======================================================================== */

#define LMS7002M_SXT_PLL_INT           0x0120  /* Integer division ratio  */
#define LMS7002M_SXT_PLL_FRAC_H       0x0121  /* Fractional ratio [23:16] */
#define LMS7002M_SXT_PLL_FRAC_L       0x0122  /* Fractional ratio [15:0]  */
#define LMS7002M_SXT_DIV               0x0123  /* Output divider            */
#define LMS7002M_SXT_VCO_CTRL          0x0124  /* VCO selection & cal      */

/* ========================================================================
 * Register Map — RX (page 0x10)
 * ======================================================================== */

#define LMS7002M_RX_GAIN_LNA           0x0130  /* LNA gain index       */
#define LMS7002M_RX_GAIN_PGA           0x0131  /* PGA gain index        */
#define LMS7002M_RX_TIA_CTRL           0x0132  /* TIA enable / gain     */
#define LMS7002M_RX_DC_OFFSET_I        0x0133  /* DC offset correction I */
#define LMS7002M_RX_DC_OFFSET_Q        0x0134  /* DC offset correction Q */
#define LMS7002M_RX_IQ_CORR_GAIN       0x0135  /* IQ gain imbalance     */
#define LMS7002M_RX_IQ_CORR_PHASE      0x0136  /* IQ phase imbalance    */

/* ========================================================================
 * Register Map — TX (page 0x10)
 * ======================================================================== */

#define LMS7002M_TX_GAIN               0x0140   /* TX gain index          */
#define LMS7002M_TX_DC_OFFSET_I        0x0141  /* DC offset correction I */
#define LMS7002M_TX_DC_OFFSET_Q        0x0142  /* DC offset correction Q */
#define LMS7002M_TX_IQ_CORR_GAIN       0x0143  /* IQ gain imbalance     */
#define LMS7002M_TX_IQ_CORR_PHASE      0x0144  /* IQ phase imbalance   */

/* ========================================================================
 * Register Map — ADC/DAC (page 0x10)
 * ======================================================================== */

#define LMS7002M_ADC_DECIMATION         0x0150  /* ADC decimation ratio   */
#define LMS7002M_DAC_INTERPOLATION      0x0151  /* DAC interpolation ratio */
#define LMS7002M_ADC_RATE_H             0x0152  /* ADC sample rate [31:16] */
#define LMS7002M_ADC_RATE_L             0x0153  /* ADC sample rate [15:0]  */

/* ========================================================================
 * Register Map — FIFO / streaming (page 0x10)
 * ======================================================================== */

#define LMS7002M_FIFO_CTRL              0x0160  /* FIFO enable / watermark */
#define LMS7002M_FIFO_STATUS            0x0161  /* FIFO fill level / flags */
#define LMS7002M_STREAM_CTRL            0x0162  /* Stream enable / format   */

/* ========================================================================
 * Register Map — Calibration (page 0x10)
 * ======================================================================== */

#define LMS7002M_CAL_CTRL               0x0170  /* Calibration control      */
#define LMS7002M_CAL_STATUS             0x0171  /* Calibration status       */
#define LMS7002M_CAL_RESULT_H           0x0172  /* Calibration result [31:16] */
#define LMS7002M_CAL_RESULT_L           0x0173  /* Calibration result [15:0]  */

/* ========================================================================
 * SPI Access Encoding
 * ======================================================================== */

/**
 * Encode a write command.
 * Format: [0][addr13:0][data15:0] — 4 bytes total, MSB first.
 * Address field is 14 bits; bit 15 (R/W) = 0 for write.
 */
static inline uint32_t lms7002m_cmd_write(uint16_t addr, uint16_t data)
{
    return ((uint32_t)(addr & 0x3FFF) << 16) | data;
}

/**
 * Encode a read command.
 * Format: [1][addr13:0][don't care] — 4 bytes total, MSB first.
 * Bit 15 (R/W) = 1 for read.
 * The read data is returned in the next SPI frame.
 */
static inline uint32_t lms7002m_cmd_read(uint16_t addr)
{
    return ((uint32_t)((addr & 0x3FFF) | 0x8000) << 16);
}

/* ========================================================================
 * Data Types
 * ======================================================================== */

/** SDR channel identifier (A or B) */
enum lms7002m_channel {
    LMS7002M_CH_A = 0,
    LMS7002M_CH_B = 1,
    LMS7002M_CH_COUNT
};

/** SDR direction */
enum lms7002m_direction {
    LMS7002M_RX = 0,
    LMS7002M_TX = 1
};

/** PLL configuration */
struct lms7002m_pll_config {
    uint16_t nint;       /**< Integer part of PLL ratio */
    uint32_t nfrac;      /**< Fractional part (0 – 0x7FFFFF) */
    uint8_t  div_out;    /**< Output divider (1,2,4,8,16,32) */
    bool     vco_high;   /**< True → VCO_H (3.72–5.8 GHz), False → VCO_L (1.88–3.72 GHz) */
};

/** Gain configuration */
struct lms7002m_gain_config {
    uint8_t  lna_gain;   /**< LNA gain index (0–73 dB) */
    bool     tia_enable;  /**< TIA enable (adds fixed 12 dB when on) */
    uint8_t  pga_gain;    /**< PGA gain index (0–31 dB) */
    uint8_t  tx_gain;     /**< TX gain index (0–52 dB) */
};

/** Sample rate configuration */
struct lms7002m_rate_config {
    uint32_t sample_rate;  /**< Desired sample rate in Hz */
    uint8_t  decimation;   /**< ADC decimation factor (1,2,4,8,16,32) */
    uint8_t  interpolation;/**< DAC interpolation factor (1,2,4,8,16,32) */
};

/** FIFO configuration */
struct lms7002m_fifo_config {
    uint16_t watermark;   /**< FIFO watermark level (0 – FIFO_DEPTH-1) */
    bool     enabled;     /**< FIFO enable flag */
    uint8_t  format;      /**< Data format: 0=I/Q interleaved, 1=I then Q */
};

/** Calibration result */
struct lms7002m_cal_result {
    bool     dc_complete;      /**< DC offset calibration done */
    bool     iq_gain_complete; /**< IQ gain calibration done    */
    bool     iq_phase_complete; /**< IQ phase calibration done  */
    int16_t  dc_offset_i;       /**< Measured DC offset (I)      */
    int16_t  dc_offset_q;       /**< Measured DC offset (Q)      */
    int16_t  iq_gain_corr;     /**< IQ gain correction (0.01 dB steps) */
    int16_t  iq_phase_corr;    /**< IQ phase correction (0.1° steps)   */
};

/** Driver state */
struct lms7002m_driver {
    bool                    initialized;
    enum lms7002m_channel   active_channel;
    struct lms7002m_pll_config rx_pll;
    struct lms7002m_pll_config tx_pll;
    struct lms7002m_gain_config rx_gain;
    struct lms7002m_gain_config tx_gain;
    struct lms7002m_rate_config rate;
    struct lms7002m_fifo_config fifo;
    struct lms7002m_cal_result cal;
    uint64_t                rx_frequency_hz;
    uint64_t                tx_frequency_hz;
    uint32_t               spi_speed_hz;
};

/* ========================================================================
 * Error Codes
 * ======================================================================== */

#define LMS7002M_OK                  0
#define LMS7002M_ERR_INIT            (-1)
#define LMS7002M_ERR_SPI             (-2)
#define LMS7002M_ERR_INVALID_PARAM  (-3)
#define LMS7002M_ERR_TIMEOUT         (-4)
#define LMS7002M_ERR_CALIBRATION     (-5)
#define LMS7002M_ERR_NOT_INITIALIZED (-6)
#define LMS7002M_ERR_PLL_LOCK       (-7)
#define LMS7002M_ERR_FIFO_OVERRUN    (-8)

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * lms7002m_init — Initialize the LMS7002M driver and reset the chip.
 * @drv: Pointer to driver state struct (caller-allocated)
 * @spi_speed_hz: SPI clock frequency in Hz (max 20 MHz)
 *
 * Returns: LMS7002M_OK on success, negative error code on failure.
 */
int lms7002m_init(struct lms7002m_driver *drv, uint32_t spi_speed_hz);

/**
 * lms7002m_deinit — Shutdown the LMS7002M and release resources.
 */
void lms7002m_deinit(struct lms7002m_driver *drv);

/**
 * lms7002m_configure_rx — Configure RX channel frequency and sample rate.
 * @drv: Driver state
 * @freq_hz: Desired receive frequency in Hz (100 kHz – 3.8 GHz)
 * @sample_rate: Desired sample rate in Hz (100 kHz – 10 MSPS)
 * @ch: Channel selector (A or B)
 *
 * Returns: LMS7002M_OK on success, negative error code on failure.
 */
int lms7002m_configure_rx(struct lms7002m_driver *drv,
                           uint64_t freq_hz, uint32_t sample_rate,
                           enum lms7002m_channel ch);

/**
 * lms7002m_configure_tx — Configure TX channel frequency and sample rate.
 * @drv: Driver state
 * @freq_hz: Desired transmit frequency in Hz (100 kHz – 3.8 GHz)
 * @sample_rate: Desired sample rate in Hz (100 kHz – 10 MSPS)
 * @ch: Channel selector (A or B)
 *
 * Returns: LMS7002M_OK on success, negative error code on failure.
 */
int lms7002m_configure_tx(struct lms7002m_driver *drv,
                           uint64_t freq_hz, uint32_t sample_rate,
                           enum lms7002m_channel ch);

/**
 * lms7002m_set_rx_gain — Set RX gain.
 * @drv: Driver state
 * @gain_db_x10: Total RX gain in dB × 10 (0 – 1160)
 *
 * Distributes gain across LNA, TIA, and PGA stages.
 * Returns: LMS7002M_OK on success, negative error code on failure.
 */
int lms7002m_set_rx_gain(struct lms7002m_driver *drv, int16_t gain_db_x10);

/**
 * lms7002m_set_tx_gain — Set TX gain.
 * @drv: Driver state
 * @gain_db_x10: TX gain in dB × 10 (0 – 520)
 *
 * Returns: LMS7002M_OK on success, negative error code on failure.
 */
int lms7002m_set_tx_gain(struct lms7002m_driver *drv, int16_t gain_db_x10);

/**
 * lms7002m_start_rx — Enable RX data streaming.
 * @drv: Driver state
 *
 * Returns: LMS7002M_OK on success.
 */
int lms7002m_start_rx(struct lms7002m_driver *drv);

/**
 * lms7002m_stop_rx — Disable RX data streaming.
 * @drv: Driver state
 */
void lms7002m_stop_rx(struct lms7002m_driver *drv);

/**
 * lms7002m_start_tx — Enable TX data streaming.
 * @drv: Driver state
 *
 * Returns: LMS7002M_OK on success.
 */
int lms7002m_start_tx(struct lms7002m_driver *drv);

/**
 * lms7002m_stop_tx — Disable TX data streaming.
 * @drv: Driver state
 */
void lms7002m_stop_tx(struct lms7002m_driver *drv);

/**
 * lms7002m_calibrate_dc — Run DC offset calibration.
 * @drv: Driver state
 * @ch: Channel selector
 * @timeout_ms: Maximum calibration time in milliseconds
 *
 * Returns: LMS7002M_OK on success, LMS7002M_ERR_TIMEOUT or
 *          LMS7002M_ERR_CALIBRATION on failure.
 */
int lms7002m_calibrate_dc(struct lms7002m_driver *drv,
                           enum lms7002m_channel ch, uint32_t timeout_ms);

/**
 * lms7002m_calibrate_iq — Run IQ imbalance calibration.
 * @drv: Driver state
 * @direction: RX or TX
 * @ch: Channel selector
 * @timeout_ms: Maximum calibration time in milliseconds
 *
 * Returns: LMS7002M_OK on success.
 */
int lms7002m_calibrate_iq(struct lms7002m_driver *drv,
                           enum lms7002m_direction direction,
                           enum lms7002m_channel ch, uint32_t timeout_ms);

/**
 * lms7002m_read_fifo — Read IQ samples from the RX FIFO.
 * @drv: Driver state
 * @buf: Output buffer (must be at least 2*num_samples × sizeof(int16_t))
 * @num_samples: Number of I/Q sample pairs to read
 *
 * Returns: Number of samples read, or negative error code.
 */
int lms7002m_read_fifo(struct lms7002m_driver *drv,
                        int16_t *buf, uint16_t num_samples);

/**
 * lms7002m_write_fifo — Write IQ samples to the TX FIFO.
 * @drv: Driver state
 * @buf: Input buffer (2*num_samples × sizeof(int16_t))
 * @num_samples: Number of I/Q sample pairs to write
 *
 * Returns: Number of samples written, or negative error code.
 */
int lms7002m_write_fifo(struct lms7002m_driver *drv,
                         const int16_t *buf, uint16_t num_samples);

/**
 * lms7002m_get_status — Read chip status and FIFO fill level.
 * @drv: Driver state
 * @fifo_fill: Output — number of samples in FIFO
 * @pll_locked: Output — true if PLL is locked
 *
 * Returns: LMS7002M_OK on success.
 */
int lms7002m_get_status(struct lms7002m_driver *drv,
                         uint16_t *fifo_fill, bool *pll_locked);

/**
 * lms7002m_calculate_pll_params — Compute PLL parameters for a frequency.
 * @freq_hz: Desired RF frequency in Hz
 * @ref_clk: Reference clock frequency in Hz
 * @nint: Output — integer part of PLL ratio
 * @nfrac: Output — fractional part (0 – 0x7FFFFF)
 * @div_out: Output — output divider (1,2,4,8,16,32)
 *
 * Returns: LMS7002M_OK on success, LMS7002M_ERR_INVALID_PARAM on failure.
 */
int lms7002m_calculate_pll_params(uint64_t freq_hz, uint32_t ref_clk,
                                   uint16_t *nint, uint32_t *nfrac,
                                   uint8_t *div_out);

/**
 * lms7002m_spi_read — Read a 16-bit register over SPI.
 * @drv: Driver state
 * @addr: 14-bit register address
 *
 * Returns: Register value, or negative error code.
 */
int lms7002m_spi_read(const struct lms7002m_driver *drv, uint16_t addr);

/**
 * lms7002m_spi_write — Write a 16-bit register over SPI.
 * @drv: Driver state
 * @addr: 14-bit register address
 * @data: 16-bit data value
 *
 * Returns: LMS7002M_OK on success.
 */
int lms7002m_spi_write(struct lms7002m_driver *drv, uint16_t addr, uint16_t data);

/**
 * lms7002m_spi_read_burst — Burst-read multiple registers.
 * @drv: Driver state
 * @start_addr: Starting register address
 * @buf: Output buffer (must hold num_regs × sizeof(uint16_t))
 * @num_regs: Number of consecutive registers to read
 *
 * Returns: Number of registers read, or negative error code.
 */
int lms7002m_spi_read_burst(struct lms7002m_driver *drv, uint16_t start_addr,
                              uint16_t *buf, uint16_t num_regs);

/**
 * lms7002m_spi_write_burst — Burst-write multiple registers.
 * @drv: Driver state
 * @start_addr: Starting register address
 * @data: Input buffer (num_regs × sizeof(uint16_t))
 * @num_regs: Number of consecutive registers to write
 *
 * Returns: Number of registers written, or negative error code.
 */
int lms7002m_spi_write_burst(struct lms7002m_driver *drv, uint16_t start_addr,
                               const uint16_t *data, uint16_t num_regs);

/**
 * lms7002m_reset — Soft-reset the LMS7002M chip.
 * @drv: Driver state
 *
 * Returns: LMS7002M_OK on success.
 */
int lms7002m_reset(struct lms7002m_driver *drv);

/**
 * lms7002m_set_channel — Select the active channel.
 * @drv: Driver state
 * @ch: Channel A or B
 */
void lms7002m_set_channel(struct lms7002m_driver *drv, enum lms7002m_channel ch);

/**
 * lms7002m_get_channel — Get the currently active channel.
 * @drv: Driver state
 *
 * Returns: Currently selected channel.
 */
enum lms7002m_channel lms7002m_get_channel(const struct lms7002m_driver *drv);

#endif /* LMS7002M_DRIVER_H */