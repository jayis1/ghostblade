/*
 * sdr_dma.h — SDR DMA Ring Buffer Manager API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Manages DMA-based ring buffer for SDR IQ data streaming from the
 * LMS7002M to the RK3576 host. The RP2350B's Core 1 runs the DMA
 * processing loop (sdr_dma_process) which continuously transfers
 * IQ samples from the LMS7002M MIPI CSI-2 interface into DMA buffers
 * and feeds them into the SPI protocol handler.
 *
 * Data format: interleaved I16/Q16 samples (4 bytes per IQ sample)
 * by default, configurable via SPI_CMD_SDR_STREAM.
 */

#ifndef SDR_DMA_H
#define SDR_DMA_H

#include <stdint.h>

/* ── DMA ring buffer configuration ──────────────────────────────────────── */

/** Number of DMA buffers in the ring (must be power of 2) */
#define SDR_DMA_RING_SIZE           8

/** Size of each DMA buffer in bytes (must be multiple of 4) */
#define SDR_DMA_BUF_SIZE            32768

/** Maximum IQ sample rate in samples per second */
#define SDR_DMA_MAX_SAMPLE_RATE     4096000

/** Default IQ sample format: I16Q16 (4 bytes per sample) */
#define SDR_DMA_FORMAT_I16Q16      0

/** Packed IQ format: I12Q12 (3 bytes per sample) */
#define SDR_DMA_FORMAT_I12Q12       1

/** Compact IQ format: I8Q8 (2 bytes per sample) */
#define SDR_DMA_FORMAT_I8Q8         2

/* ── DMA ring buffer state ───────────────────────────────────────────────── */

enum sdr_dma_state {
    SDR_DMA_STATE_IDLE = 0,     /**< Not streaming */
    SDR_DMA_STATE_RUNNING,     /**< Actively streaming IQ data */
    SDR_DMA_STATE_ERROR,       /**< DMA or SPI error occurred */
};

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * sdr_dma_init — Initialize the DMA ring buffer engine
 *
 * Allocates DMA buffers, configures DMA channels, and prepares the
 * ring buffer state machine. Must be called before sdr_dma_start().
 *
 * Returns: 0 on success, negative on error
 */
int sdr_dma_init(void);

/**
 * sdr_dma_start — Start SDR IQ data streaming
 *
 * Begins DMA transfers from the LMS7002M CSI-2 interface into the
 * ring buffer. Core 1 will call sdr_dma_process() in a tight loop
 * to feed completed buffers into the SPI protocol handler.
 */
void sdr_dma_start(void);

/**
 * sdr_dma_stop — Stop SDR IQ data streaming
 *
 * Halts DMA transfers and drains the ring buffer. Called when the
 * host sends SPI_CMD_SDR_STREAM with enable=0, or on error.
 */
void sdr_dma_stop(void);

/**
 * sdr_dma_process — Process completed DMA buffers (Core 1 loop)
 *
 * Checks for completed DMA buffers and pushes their IQ data into
 * the SPI protocol handler for transmission to the RK3576 host.
 * Must be called frequently from Core 1's main loop.
 */
void sdr_dma_process(void);

/**
 * sdr_dma_set_frequency — Tune the SDR center frequency
 *
 * @freq_hz:    Center frequency in Hz (100 kHz – 3.8 GHz)
 * @bw_khz:     Bandwidth in kHz (e.g., 20000 for 20 MHz)
 * @gain_db_x10: LNA gain in dB × 10 (e.g., 300 for 30.0 dB)
 *
 * Sends configuration to the LMS7002M via SPI1 to adjust the
 * PLL, baseband filter, and LNA settings.
 */
void sdr_dma_set_frequency(uint32_t freq_hz, uint16_t bw_khz,
                            uint16_t gain_db_x10);

/**
 * sdr_dma_get_state — Get the current DMA engine state
 *
 * Returns: current sdr_dma_state enum value
 */
enum sdr_dma_state sdr_dma_get_state(void);

/**
 * sdr_dma_get_buffers_completed — Get count of completed buffers
 *
 * Returns: total number of DMA buffers processed since last start
 */
uint32_t sdr_dma_get_buffers_completed(void);

/**
 * sdr_dma_get_overrun_count — Get count of ring buffer overruns
 *
 * An overrun occurs when the DMA engine fills a buffer faster
 * than Core 1 can process it, causing data loss.
 *
 * Returns: number of overrun events since last start
 */
uint32_t sdr_dma_get_overrun_count(void);

#endif /* SDR_DMA_H */