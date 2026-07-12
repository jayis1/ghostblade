/*
 * lms7002m_driver.h — LMS7002M SDR Transceiver Driver API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides initialization, frequency tuning, and control for the
 * LMS7002M SDR transceiver on the GhostBlade board.
 */

#ifndef LMS7002M_DRIVER_H
#define LMS7002M_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * lms7002m_init — Initialize the LMS7002M SDR transceiver
 *
 * Performs a full initialization sequence: hardware reset, SPI
 * communication check, power configuration, and default RX setup.
 *
 * Returns: 0 on success, -1 on SPI communication failure
 */
int lms7002m_init(void);

/**
 * lms7002m_tune_rx — Tune the LMS7002M RX frequency
 *
 * @freq_hz:    Center frequency in Hz (100 kHz to 3.8 GHz)
 * @bw_khz:     Bandwidth in kHz (e.g., 2000 for 2 MHz), 0 to keep current
 * @gain_db_x10: LNA gain in dB × 10 (e.g., 300 for 30.0 dB)
 */
void lms7002m_tune_rx(uint32_t freq_hz, uint16_t bw_khz, uint16_t gain_db_x10);

/**
 * lms7002m_set_rx_bandwidth — Set the RX baseband filter bandwidth
 *
 * @bw_khz: Bandwidth in kHz
 */
void lms7002m_set_rx_bandwidth(uint16_t bw_khz);

/**
 * lms7002m_set_rx_gain — Set the LNA gain for the RX path
 *
 * @gain_db_x10: Gain in dB × 10 (0 to 730)
 */
void lms7002m_set_rx_gain(uint16_t gain_db_x10);

/**
 * lms7002m_set_rx_decimation — Set the RX decimation ratio
 *
 * @decimation: Decimation factor (1, 2, 4, 8, 16, 32, or 64)
 */
void lms7002m_set_rx_decimation(uint16_t decimation);

/**
 * lms7002m_enable_rx — Enable the LMS7002M RX path
 */
void lms7002m_enable_rx(void);

/**
 * lms7002m_disable_rx — Disable the LMS7002M RX path
 */
void lms7002m_disable_rx(void);

/**
 * lms7002m_enable_tx — Enable the LMS7002M TX path
 */
void lms7002m_enable_tx(void);

/**
 * lms7002m_disable_tx — Disable the LMS7002M TX path
 */
void lms7002m_disable_tx(void);

/**
 * lms7002m_power_down — Put the LMS7002M into low-power mode
 */
void lms7002m_power_down(void);

/**
 * lms7002m_read_rssi — Read the LMS7002M RSSI value
 *
 * Returns: RSSI in dBm × 10 (signed, e.g., -740 = -74.0 dBm)
 */
int16_t lms7002m_read_rssi(void);

/**
 * lms7002m_get_field_strength_mv — Get estimated field strength in mV
 *
 * Returns: Estimated field strength in mV (0-5000)
 */
uint16_t lms7002m_get_field_strength_mv(void);

#endif /* LMS7002M_DRIVER_H */