/*
 * sdr_dma.c — DMA Ring Buffer Manager for SDR IQ Data on RP2350B
 *
 * Copyright (C) 2026 Apex One Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements a DMA ring buffer manager for streaming SDR IQ
 * sample data from the LMS7002M transceiver to the RK3576 host via
 * the SPI0 bridge.
 *
 * Architecture:
 *   LMS7002M → (MIPI-CSI-2) → RK3576 DMA ring buffer → userspace
 *   LMS7002M → (SPI1 ctrl)  → RP2350B ← (SPI0 bridge) → RK3576
 *
 * The RP2350B manages the LMS7002M control plane (tuning, gain, etc.)
 * while the MIPI-CSI-2 data plane streams IQ directly to the RK3576.
 * However, for low-sample-rate or debug modes, the RP2350B can capture
 * IQ chunks via SPI1 and forward them to the host via SPI0.
 *
 * The ring buffer uses a producer (DMA ISR) / consumer (SPI0 TX)
 * model with DMA channel 0 (read) and channel 1 (write).
 *
 * Reference: RP2350B Datasheet, Section 2.5 (DMA)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * RP2350B DMA Controller Register Map
 * ======================================================================== */

#define RP2350B_DMA_BASE            0x50000000UL

/* DMA Channel 0 (SDR IQ read from LMS7002M SPI1 RX) */
#define DMA_CH0_CTRL                (RP2350B_DMA_BASE + 0x000)
#define DMA_CH0_READ_ADDR           (RP2350B_DMA_BASE + 0x004)
#define DMA_CH0_WRITE_ADDR          (RP2350B_DMA_BASE + 0x008)
#define DMA_CH0_TRANS_COUNT         (RP2350B_DMA_BASE + 0x00C)

/* DMA Channel 1 (SDR IQ write to SPI0 TX for host) */
#define DMA_CH1_CTRL                (RP2350B_DMA_BASE + 0x040)
#define DMA_CH1_READ_ADDR           (RP2350B_DMA_BASE + 0x044)
#define DMA_CH1_WRITE_ADDR          (RP2350B_DMA_BASE + 0x048)
#define DMA_CH1_TRANS_COUNT         (RP2350B_DMA_BASE + 0x04C)

/* DMA Common Registers */
#define DMA_INTS                    (RP2350B_DMA_BASE + 0x240)
#define DMA_INTE                    (RP2350B_DMA_BASE + 0x244)
#define DMA_INTF                    (RP2350B_DMA_BASE + 0x248)
#define DMA_INTS0                   (RP2350B_DMA_BASE + 0x240)
#define DMA_INTE0                   (RP2350B_DMA_BASE + 0x244)
#define DMA_CHAN_EN                  (RP2350B_DMA_BASE + 0x230)
#define DMA_CHAN_EN0                 (RP2350B_DMA_BASE + 0x230)

/* DMA CTRL register bits */
#define DMA_CTRL_EN                  (1 << 0)    /* Channel enable */
#define DMA_CTRL_HIGH_PRIORITY       (1 << 1)    /* High priority */
#define DMA_CTRL_DATA_SIZE_8         (0 << 2)    /* 8-bit transfers */
#define DMA_CTRL_DATA_SIZE_16        (1 << 2)    /* 16-bit transfers */
#define DMA_CTRL_DATA_SIZE_32        (2 << 2)    /* 32-bit transfers */
#define DMA_CTRL_INCR_READ           (1 << 4)    /* Increment read address */
#define DMA_CTRL_INCR_WRITE          (1 << 5)    /* Increment write address */
#define DMA_CTRL_RING_SEL            (1 << 6)    /* Ring buffer mode */
#define DMA_CTRL_RING_SIZE_4         (0 << 8)    /* Ring size: 4 bytes */
#define DMA_CTRL_RING_SIZE_8         (1 << 8)    /* Ring size: 8 bytes */
#define DMA_CTRL_RING_SIZE_16        (2 << 8)    /* Ring size: 16 bytes */
#define DMA_CTRL_RING_SIZE_32        (3 << 8)    /* Ring size: 32 bytes */
#define DMA_CTRL_CHAIN_TO(ch)        ((ch) << 11)  /* Chain to channel */
#define DMA_CTRL_TREQ_SEL(val)       ((val) << 15) /* Transfer request */
#define DMA_CTRL_IRQ_QUIET          (1 << 21)    /* IRQ quiet mode */
#define DMA_CTRL_BSWAP              (1 << 22)    /* Byte swap */
#define DMA_CTRL_SNIFF_EN           (1 << 23)    /* Sniffer enable */

/* DMA transfer request signals */
#define DMA_TREQ_SPI1_RX            0x0A   /* SPI1 RX FIFO not empty */
#define DMA_TREQ_SPI0_TX            0x06   /* SPI0 TX FIFO not full */
#define DMA_TREQ_PERMANENT          0x3F   /* Always (no pacing) */

/* ========================================================================
 * DMA Ring Buffer Configuration
 * ======================================================================== */

/*
 * Ring buffer layout:
 *
 * The IQ data ring buffer is divided into N equal-sized blocks.
 * Each block holds one IQ sample chunk (configurable size).
 * The DMA engine fills blocks from SPI1 (LMS7002M RX), and
 * the SPI protocol handler reads complete blocks to send to RK3576.
 *
 * ┌──────────┬──────────┬──────────┬──────────┐
 * │ Block 0  │ Block 1  │ Block 2  │ Block 3  │
 * │ (IQ samples) │      │          │          │
 * └──────────┴──────────┴──────────┴──────────┘
 *  ↑ dma_write_idx              ↑ dma_read_idx
 *  (DMA fills here)             (Protocol reads here)
 */

/* Ring buffer parameters */
#define SDR_RING_NUM_BLOCKS         8      /* Number of blocks in ring */
#define SDR_RING_BLOCK_SIZE         512    /* Bytes per block (256 IQ pairs × 2 bytes) */
#define SDR_RING_BUF_SIZE           (SDR_RING_NUM_BLOCKS * SDR_RING_BLOCK_SIZE)

/* 4096 bytes total ring buffer, aligned to 4-byte boundary */
static uint8_t sdr_ring_buf[SDR_RING_BUF_SIZE] __attribute__((aligned(4)));

/* Block management */
static volatile uint8_t dma_write_block = 0;  /* Next block DMA will fill */
static volatile uint8_t proto_read_block = 0;  /* Next block protocol will read */
static volatile uint8_t blocks_filled = 0;     /* Number of filled blocks available */

/* DMA state */
static volatile bool dma_running = false;
static volatile uint32_t dma_irq_count = 0;

/* Statistics */
static struct {
    uint32_t total_blocks_captured;
    uint32_t total_blocks_sent;
    uint32_t overruns;     /* DMA overwrote unread block */
    uint32_t underruns;    /* Protocol requested empty block */
} dma_stats;

/* ========================================================================
 * SPI1 Base Address (for DMA targeting)
 * ======================================================================== */

#define RP2350B_SPI1_BASE   0x48070000UL
#define SPI_SSPDR           0x008

/* SPI0 Base (for DMA TX to host) */
#define RP2350B_SPI0_BASE   0x48060000UL

/* ========================================================================
 * Helper Macros
 * ======================================================================== */

#define REG32(addr)           (*(volatile uint32_t *)(addr))
#define MIN(a, b)             ((a) < (b) ? (a) : (b))

/* ========================================================================
 * DMA IRQ Handler
 * ======================================================================== */

/**
 * sdr_dma_irq_handler — Called when DMA channel 0 completes a block transfer
 *
 * This is triggered when the DMA channel for SDR IQ data completes
 * filling one block of the ring buffer. It advances the write pointer
 * and optionally chains to the next block.
 */
void sdr_dma_irq_handler(void) {
    uint32_t ints = REG32(DMA_INTS0);

    dma_irq_count++;

    if (ints & (1 << 0)) {
        /* Channel 0 (SDR RX from SPI1) transfer complete */

        /* Clear interrupt */
        REG32(DMA_INTS0) = (1 << 0);

        /* Advance write block pointer */
        uint8_t next_write = (dma_write_block + 1) % SDR_RING_NUM_BLOCKS;

        /* Check for overrun: next write block equals read block */
        if (next_write == proto_read_block) {
            dma_stats.overruns++;
            /* Overrun: discard oldest block (advance read pointer) */
            proto_read_block = (proto_read_block + 1) % SDR_RING_NUM_BLOCKS;
        }

        dma_write_block = next_write;
        blocks_filled++;
        dma_stats.total_blocks_captured++;

        /* If streaming is active, re-chain DMA for next block */
        if (dma_running) {
            sdr_dma_start_block(next_write);
        }
    }
}

/* ========================================================================
 * DMA Channel Configuration
 * ======================================================================== */

/**
 * sdr_dma_start_block — Start a DMA transfer to fill one ring buffer block
 *
 * @block_idx: Index of the ring buffer block to fill (0 to NUM_BLOCKS-1)
 *
 * Configures DMA channel 0 to read from SPI1 RX FIFO and write into
 * the specified block of the ring buffer.
 */
void sdr_dma_start_block(uint8_t block_idx) {
    uint32_t block_addr = (uint32_t)&sdr_ring_buf[block_idx * SDR_RING_BLOCK_SIZE];

    /* Configure DMA channel 0 for SPI1 RX → ring buffer */
    REG32(DMA_CH0_READ_ADDR)  = (uint32_t)(RP2350B_SPI1_BASE + SPI_SSPDR);
    REG32(DMA_CH0_WRITE_ADDR) = block_addr;
    REG32(DMA_CH0_TRANS_COUNT) = SDR_RING_BLOCK_SIZE;  /* Transfer 512 bytes */

    /* Control word:
     * - Enable channel
     * - Data size: 8-bit
     * - Increment write address (fill buffer)
     * - Do NOT increment read address (SPI1 RX FIFO is fixed)
     * - Transfer request: SPI1 RX FIFO not empty
     * - Chain to self (auto-restart, but we manage via IRQ)
     * - IRQ on completion
     */
    REG32(DMA_CH0_CTRL) = DMA_CTRL_EN                    |
                           DMA_CTRL_DATA_SIZE_8            |
                           DMA_CTRL_INCR_WRITE             |
                           DMA_TREQ_SPI1_RX << 15          |
                           DMA_CTRL_IRQ_QUIET;

    /* Enable DMA channel 0 interrupt */
    REG32(DMA_INTE0) |= (1 << 0);

    /* Enable DMA channel */
    REG32(DMA_CHAN_EN0) |= (1 << 0);
}

/* ========================================================================
 * DMA Ring Buffer Public API
 * ======================================================================== */

/**
 * sdr_dma_init — Initialize the DMA ring buffer for SDR IQ data
 *
 * Must be called after SPI1 master is initialized.
 * Does not start DMA — call sdr_dma_start() to begin streaming.
 */
void sdr_dma_init(void) {
    /* Clear ring buffer */
    memset(sdr_ring_buf, 0, SDR_RING_BUF_SIZE);

    /* Reset state */
    dma_write_block = 0;
    proto_read_block = 0;
    blocks_filled = 0;
    dma_running = false;
    dma_irq_count = 0;

    /* Reset statistics */
    memset(&dma_stats, 0, sizeof(dma_stats));

    /* Disable DMA channels */
    REG32(DMA_CHAN_EN0) = 0;

    /* Clear any pending DMA interrupts */
    REG32(DMA_INTS0) = 0xFFFFFFFF;

    /* Disable all DMA interrupts */
    REG32(DMA_INTE0) = 0;
}

/**
 * sdr_dma_start — Start DMA-based SDR IQ capture
 *
 * Begins streaming IQ data from SPI1 (LMS7002M RX) into the ring buffer.
 * Each completed block triggers an IRQ, and the protocol handler
 * sends it to the RK3576 via SPI0.
 *
 * Returns: 0 on success
 */
int sdr_dma_start(void) {
    if (dma_running)
        return 0;  /* Already running */

    /* Reset ring buffer pointers */
    dma_write_block = 0;
    proto_read_block = 0;
    blocks_filled = 0;

    dma_running = true;

    /* Start first block DMA transfer */
    sdr_dma_start_block(0);

    return 0;
}

/**
 * sdr_dma_stop — Stop DMA-based SDR IQ capture
 *
 * Halts the DMA engine and disables the DMA channel.
 * Any in-progress transfer is aborted.
 */
void sdr_dma_stop(void) {
    dma_running = false;

    /* Disable DMA channel 0 */
    REG32(DMA_CHAN_EN0) &= ~(1 << 0);

    /* Abort any in-progress transfer */
    REG32(DMA_CH0_CTRL) = 0;

    /* Disable DMA interrupt */
    REG32(DMA_INTE0) &= ~(1 << 0);

    /* Clear pending interrupt */
    REG32(DMA_INTS0) = (1 << 0);
}

/**
 * sdr_dma_get_block — Get a pointer to the next available IQ data block
 *
 * @block_idx: Output: index of the available block
 * @size:      Output: size of the block in bytes
 *
 * Returns: pointer to the block data, or NULL if no blocks available
 */
const uint8_t *sdr_dma_get_block(uint8_t *block_idx, uint16_t *size) {
    if (blocks_filled == 0) {
        dma_stats.underruns++;
        return NULL;
    }

    *block_idx = proto_read_block;
    *size = SDR_RING_BLOCK_SIZE;

    return &sdr_ring_buf[proto_read_block * SDR_RING_BLOCK_SIZE];
}

/**
 * sdr_dma_release_block — Release a consumed block back to the ring buffer
 *
 * Called after the protocol handler has sent the block to the host.
 * Advances the read pointer and decrements the filled count.
 */
void sdr_dma_release_block(void) {
    if (blocks_filled > 0) {
        proto_read_block = (proto_read_block + 1) % SDR_RING_NUM_BLOCKS;
        blocks_filled--;
        dma_stats.total_blocks_sent++;
    }
}

/**
 * sdr_dma_get_stats — Get DMA ring buffer statistics
 *
 * @total_captured: Total blocks captured from SDR
 * @total_sent: Total blocks sent to host
 * @overruns: Number of DMA overruns (blocks overwritten before reading)
 * @underruns: Number of protocol underruns (empty when data requested)
 */
void sdr_dma_get_stats(uint32_t *total_captured, uint32_t *total_sent,
                        uint32_t *overruns, uint32_t *underruns) {
    if (total_captured) *total_captured = dma_stats.total_blocks_captured;
    if (total_sent)     *total_sent     = dma_stats.total_blocks_sent;
    if (overruns)       *overruns       = dma_stats.overruns;
    if (underruns)      *underruns      = dma_stats.underruns;
}

/**
 * sdr_dma_is_running — Check if DMA capture is active
 *
 * Returns: true if DMA is currently streaming IQ data
 */
bool sdr_dma_is_running(void) {
    return dma_running;
}

/**
 * sdr_dma_blocks_available — Get number of filled blocks ready to read
 *
 * Returns: count of blocks available for the protocol handler
 */
uint8_t sdr_dma_blocks_available(void) {
    return blocks_filled;
}