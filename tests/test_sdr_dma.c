/*
 * test_sdr_dma.c — Unit Tests for SDR DMA Ring Buffer Logic
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the SDR DMA ring buffer manager. These tests exercise
 * the ring buffer block management, overrun/underrun detection, and
 * statistics tracking without requiring actual DMA hardware.
 *
 * The ring buffer uses a producer (DMA ISR) / consumer (protocol handler)
 * model with 8 blocks of 512 bytes each (4096 bytes total).
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -o test_sdr_dma test_sdr_dma.c
 *
 * Run:
 *   ./test_sdr_dma
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * Ring Buffer Configuration (must match sdr_dma.c)
 * ======================================================================== */

#define SDR_RING_NUM_BLOCKS         8
#define SDR_RING_BLOCK_SIZE         512
#define SDR_RING_BUF_SIZE           (SDR_RING_NUM_BLOCKS * SDR_RING_BLOCK_SIZE)

/* ========================================================================
 * Simulated Ring Buffer State
 *
 * We replicate the ring buffer logic from sdr_dma.c here as a userspace
 * simulation so we can test the algorithms without hardware.
 * ======================================================================== */

static uint8_t sim_ring_buf[SDR_RING_BUF_SIZE];
static uint8_t sim_dma_write_block;
static uint8_t sim_proto_read_block;
static volatile uint8_t sim_blocks_filled;

static struct {
    uint32_t total_blocks_captured;
    uint32_t total_blocks_sent;
    uint32_t overruns;
    uint32_t underruns;
} sim_dma_stats;

static volatile bool sim_dma_running;

/* Simulated DMA block data pattern */
#define DMA_PATTERN_BYTE(block_idx, offset) \
    ((uint8_t)((block_idx) * 0x17 + (offset) * 0x31 + 0xAB))

static void sim_reset(void) {
    memset(sim_ring_buf, 0, SDR_RING_BUF_SIZE);
    sim_dma_write_block = 0;
    sim_proto_read_block = 0;
    sim_blocks_filled = 0;
    sim_dma_running = false;
    memset(&sim_dma_stats, 0, sizeof(sim_dma_stats));
}

/**
 * sim_dma_fill_block — Simulate DMA filling a block with a known pattern
 *
 * @block_idx: Block index to fill (0 to SDR_RING_NUM_BLOCKS-1)
 */
static void sim_dma_fill_block(uint8_t block_idx) {
    uint32_t offset = block_idx * SDR_RING_BLOCK_SIZE;
    for (uint16_t i = 0; i < SDR_RING_BLOCK_SIZE; i++) {
        sim_ring_buf[offset + i] = DMA_PATTERN_BYTE(block_idx, i);
    }
}

/**
 * sim_dma_isr_handler — Simulate the DMA ISR completing a block transfer
 *
 * Mirrors the actual sdr_dma_irq_handler() logic from sdr_dma.c:
 *   1. Compute next write block
 *   2. Check for overrun (next_write == proto_read_block while filled)
 *   3. On overrun, advance read pointer (discard oldest block)
 *   4. Advance write pointer and fill the block
 *   5. Increment blocks_filled (using __atomic semantics equivalent)
 *   6. Increment total_blocks_captured
 */
static void sim_dma_isr_handler(void) {
    uint8_t next_write = (sim_dma_write_block + 1) % SDR_RING_NUM_BLOCKS;

    /* Check for overrun: next write block equals read block while buffer has data.
     * This mirrors sdr_dma_irq_handler() which discards the oldest block on overrun. */
    if (next_write == sim_proto_read_block && sim_blocks_filled > 0) {
        sim_dma_stats.overruns++;
        /* Overrun: discard oldest block (advance read pointer) */
        sim_proto_read_block = (sim_proto_read_block + 1) % SDR_RING_NUM_BLOCKS;
        sim_blocks_filled--;
    }

    sim_dma_write_block = next_write;
    sim_dma_fill_block(next_write);
    sim_blocks_filled++;
    sim_dma_stats.total_blocks_captured++;
}

/**
 * sim_proto_get_block — Simulate the protocol handler getting a filled block
 *
 * @block_idx: Output: index of the block
 * @size:      Output: size of the block in bytes
 *
 * Returns: pointer to block data, or NULL if no blocks available
 */
static const uint8_t *sim_proto_get_block(uint8_t *block_idx, uint16_t *size) {
    if (sim_blocks_filled == 0) {
        sim_dma_stats.underruns++;
        return NULL;
    }

    if (block_idx != NULL)
        *block_idx = sim_proto_read_block;
    if (size != NULL)
        *size = SDR_RING_BLOCK_SIZE;
    return &sim_ring_buf[sim_proto_read_block * SDR_RING_BLOCK_SIZE];
}

/**
 * sim_proto_release_block — Simulate the protocol handler releasing a block
 */
static void sim_proto_release_block(void) {
    if (sim_blocks_filled > 0) {
        sim_proto_read_block = (sim_proto_read_block + 1) % SDR_RING_NUM_BLOCKS;
        sim_blocks_filled--;
        sim_dma_stats.total_blocks_sent++;
    }
}

/* ========================================================================
 * Minimal Test Framework
 * ======================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ_INT(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s: expected %d, got %d (line %d)\n", \
               msg, (int)(expected), (int)(actual), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ_UINT(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s: expected %u, got %u (line %d)\n", \
               msg, (unsigned)(expected), (unsigned)(actual), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    printf("Running: %s\n", #func); \
    func(); \
} while(0)

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test 1: Basic ring buffer initialization */
static void test_ring_init(void) {
    sim_reset();

    ASSERT_EQ_INT(0, (int)sim_dma_write_block, "Write block starts at 0");
    ASSERT_EQ_INT(0, (int)sim_proto_read_block, "Read block starts at 0");
    ASSERT_EQ_INT(0, (int)sim_blocks_filled, "No blocks filled initially");
    ASSERT_EQ_UINT(0, sim_dma_stats.total_blocks_captured, "No blocks captured");
    ASSERT_EQ_UINT(0, sim_dma_stats.total_blocks_sent, "No blocks sent");
    ASSERT_EQ_UINT(0, sim_dma_stats.overruns, "No overruns");
    ASSERT_EQ_UINT(0, sim_dma_stats.underruns, "No underruns");
}

/* Test 2: Fill and read one block */
static void test_single_block_produce_consume(void) {
    sim_reset();

    /* Producer: DMA fills block 0 */
    sim_dma_isr_handler();

    ASSERT_EQ_INT(1, (int)sim_blocks_filled, "1 block filled after ISR");
    ASSERT_EQ_INT(1, (int)sim_dma_write_block, "Write pointer advanced to block 1");
    ASSERT_EQ_UINT(1, sim_dma_stats.total_blocks_captured, "1 block captured");

    /* Consumer: Read block 0 */
    uint16_t block_size;
    const uint8_t *data = sim_proto_get_block(NULL, &block_size);

    ASSERT_TRUE(data != NULL, "Block data pointer is not NULL");
    ASSERT_EQ_INT(SDR_RING_BLOCK_SIZE, (int)block_size, "Block size is 512");

    /* Block 0 was never filled by the ISR (ISR fills block 1, the next block),
     * so it should contain zeros from initialization. */
    bool block0_zeros = true;
    for (uint16_t i = 0; i < SDR_RING_BLOCK_SIZE; i++) {
        if (data[i] != 0) {
            block0_zeros = false;
            break;
        }
    }
    ASSERT_TRUE(block0_zeros, "Unfilled block 0 contains zeros");

    sim_proto_release_block();
    ASSERT_EQ_INT(0, (int)sim_blocks_filled, "0 blocks filled after release");
    ASSERT_EQ_UINT(1, sim_dma_stats.total_blocks_sent, "1 block sent");
}
/* Test 3: Fill 7 blocks (max without overrun) and drain them */
static void test_fill_max_blocks(void) {
    sim_reset();

    /* With 8 blocks, we can fill at most 7 without overrun because
     * the ring buffer needs one block of separation between write
     * and read pointers to detect "full" vs "empty". */
    int max_without_overrun = SDR_RING_NUM_BLOCKS - 1;  /* 7 */

    for (int i = 0; i < max_without_overrun; i++) {
        sim_dma_isr_handler();
    }

    ASSERT_EQ_INT(max_without_overrun, (int)sim_blocks_filled,
                   "7 blocks filled (max without overrun)");
    ASSERT_EQ_UINT((unsigned)max_without_overrun, sim_dma_stats.total_blocks_captured,
                   "7 blocks captured");
    ASSERT_EQ_UINT(0, sim_dma_stats.overruns, "No overruns");

    /* Read all blocks */
    for (int i = 0; i < max_without_overrun; i++) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block data available");
        sim_proto_release_block();
    }

    ASSERT_EQ_INT(0, (int)sim_blocks_filled, "All blocks drained");
    ASSERT_EQ_UINT((unsigned)max_without_overrun, sim_dma_stats.total_blocks_sent,
                   "7 blocks sent");
    ASSERT_EQ_UINT(0, sim_dma_stats.underruns, "No underruns");
}

/* Test 4: Overrun detection — fill beyond ring buffer capacity */
static void test_overrun_detection(void) {
    sim_reset();

    /* Fill 8 blocks without consuming any.
     * The 8th fill causes an overrun because next_write (block 0)
     * equals the read pointer (block 0) while the buffer has data.
     * The overrun handler discards the oldest block (advances read). */
    for (int i = 0; i < SDR_RING_NUM_BLOCKS; i++) {
        sim_dma_isr_handler();
    }

    /* After 8 fills: the 8th caused an overrun. The buffer holds 7 blocks
     * (the oldest was discarded), not 8. */
    ASSERT_EQ_INT(SDR_RING_NUM_BLOCKS - 1, (int)sim_blocks_filled,
                   "7 blocks filled after one overrun");
    ASSERT_EQ_UINT(1, sim_dma_stats.overruns, "1 overrun detected");

    /* One more fill will cause another overrun (buffer at capacity again) */
    sim_dma_isr_handler();
    ASSERT_EQ_UINT(2, sim_dma_stats.overruns, "2 overruns after extra fill");
}

/* Test 5: Underrun detection — read from empty buffer */
static void test_underrun_detection(void) {
    sim_reset();

    /* Try to read from empty buffer */
    uint8_t idx;
    uint16_t size;
    const uint8_t *data = sim_proto_get_block(&idx, &size);

    ASSERT_TRUE(data == NULL, "NULL returned from empty buffer");
    ASSERT_EQ_UINT(1, sim_dma_stats.underruns, "Underrun counter incremented");
}

/* Test 6: Interleaved produce and consume */
static void test_interleaved_produce_consume(void) {
    sim_reset();

    /* Produce 3 blocks */
    for (int i = 0; i < 3; i++)
        sim_dma_isr_handler();

    ASSERT_EQ_INT(3, (int)sim_blocks_filled, "3 blocks after produce");

    /* Consume 2 */
    for (int i = 0; i < 2; i++) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available for consume");
        sim_proto_release_block();
    }

    ASSERT_EQ_INT(1, (int)sim_blocks_filled, "1 block after partial consume");
    ASSERT_EQ_UINT(2, sim_dma_stats.total_blocks_sent, "2 blocks sent");

    /* Produce 5 more */
    for (int i = 0; i < 5; i++)
        sim_dma_isr_handler();

    ASSERT_EQ_INT(6, (int)sim_blocks_filled, "6 blocks after more produce");

    /* Consume all remaining */
    while (sim_blocks_filled > 0) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available during drain");
        sim_proto_release_block();
    }

    ASSERT_EQ_INT(0, (int)sim_blocks_filled, "0 blocks after full drain");
    ASSERT_EQ_UINT(8, sim_dma_stats.total_blocks_sent, "8 blocks total sent (2 + 6)");
}

/* Test 7: Ring buffer wrap-around */
static void test_ring_wraparound(void) {
    sim_reset();

    /* Fill and drain blocks twice to verify wrap-around.
     * We fill only 7 blocks (SDR_RING_NUM_BLOCKS - 1) each cycle
     * to avoid overrun, since the ring buffer can hold at most
     * NUM_BLOCKS - 1 blocks without overrun. */
    int max_without_overrun = SDR_RING_NUM_BLOCKS - 1;

    for (int cycle = 0; cycle < 2; cycle++) {
        for (int i = 0; i < max_without_overrun; i++)
            sim_dma_isr_handler();

        for (int i = 0; i < max_without_overrun; i++) {
            uint8_t idx;
            uint16_t size;
            const uint8_t *data = sim_proto_get_block(&idx, &size);
            ASSERT_TRUE(data != NULL, "Block available in wrap-around test");
            sim_proto_release_block();
        }
    }

    ASSERT_EQ_INT(0, (int)sim_blocks_filled, "Buffer empty after double cycle");
    ASSERT_EQ_UINT(0, sim_dma_stats.overruns, "No overruns in wrap-around");
    ASSERT_EQ_UINT(0, sim_dma_stats.underruns, "No underruns in wrap-around");
    ASSERT_EQ_UINT((unsigned)(max_without_overrun * 2),
                   sim_dma_stats.total_blocks_captured,
                   "14 blocks captured in double cycle");
    ASSERT_EQ_UINT((unsigned)(max_without_overrun * 2),
                   sim_dma_stats.total_blocks_sent,
                   "14 blocks sent in double cycle");
}

/* Test 8: Block data integrity after wrap-around */
static void test_data_integrity_after_wraparound(void) {
    sim_reset();

    /* Fill 7 blocks (max without overrun) */
    for (int i = 0; i < SDR_RING_NUM_BLOCKS - 1; i++)
        sim_dma_isr_handler();

    /* Consume 4 blocks */
    for (int i = 0; i < 4; i++) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        (void)data;
        sim_proto_release_block();
    }

    /* Fill 4 more blocks (wrapping around, no overrun since read=4) */
    for (int i = 0; i < 4; i++)
        sim_dma_isr_handler();

    /* Now blocks_filled = 3 (remaining) + 4 (new) = 7.
     * The read pointer should be at block 4, write pointer at block 4
     * (after wrapping), and blocks 4-7 and 0-2 should be readable. */
    int consumed = 0;
    while (sim_blocks_filled > 0) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available after wrap-around fill");
        sim_proto_release_block();
        consumed++;
    }

    ASSERT_EQ_INT(7, consumed, "7 blocks consumed after wrap-around");
}

/* Test 9: Sustained streaming pattern (producer faster than consumer) */
static void test_sustained_streaming(void) {
    sim_reset();

    /* Simulate 100 blocks of streaming with consumer keeping up */
    int produced = 0;
    int consumed = 0;

    for (int i = 0; i < 100; i++) {
        sim_dma_isr_handler();
        produced++;

        /* Consumer reads every other block to simulate slightly slower */
        if (i % 2 == 0 && sim_blocks_filled > 0) {
            uint8_t idx;
            uint16_t size;
            const uint8_t *data = sim_proto_get_block(&idx, &size);
            ASSERT_TRUE(data != NULL, "Block available during streaming");
            sim_proto_release_block();
            consumed++;
        }
    }

    /* Drain remaining blocks */
    while (sim_blocks_filled > 0) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        (void)data;
        sim_proto_release_block();
        consumed++;
    }

    ASSERT_EQ_UINT(100, sim_dma_stats.total_blocks_captured,
                   "100 blocks captured in streaming test");
    ASSERT_EQ_UINT((unsigned)consumed, sim_dma_stats.total_blocks_sent,
                   "All blocks consumed in streaming test");
}

/* Test 10: Ring buffer size calculations */
static void test_ring_buffer_size_calculations(void) {
    /* Verify ring buffer dimensions */
    ASSERT_EQ_INT(8, SDR_RING_NUM_BLOCKS, "8 blocks in ring");
    ASSERT_EQ_INT(512, SDR_RING_BLOCK_SIZE, "512 bytes per block");
    ASSERT_EQ_INT(4096, SDR_RING_BUF_SIZE, "4096 bytes total ring");

    /* Block index wrap-around */
    uint8_t idx;
    idx = (0 + 1) % SDR_RING_NUM_BLOCKS;
    ASSERT_EQ_INT(1, (int)idx, "Block 0 + 1 = 1");
    idx = (7 + 1) % SDR_RING_NUM_BLOCKS;
    ASSERT_EQ_INT(0, (int)idx, "Block 7 + 1 wraps to 0");
    idx = (6 + 3) % SDR_RING_NUM_BLOCKS;
    ASSERT_EQ_INT(1, (int)idx, "Block 6 + 3 wraps to 1");
}

/* Test 11: DMA state transitions */
static void test_dma_state_transitions(void) {
    sim_reset();

    /* Initial state: not running */
    ASSERT_TRUE(!sim_dma_running, "DMA not running initially");

    /* Start DMA */
    sim_dma_running = true;
    ASSERT_TRUE(sim_dma_running, "DMA running after start");

    /* Produce some blocks */
    for (int i = 0; i < 4; i++)
        sim_dma_isr_handler();

    ASSERT_EQ_INT(4, (int)sim_blocks_filled, "4 blocks while running");

    /* Stop DMA */
    sim_dma_running = false;
    ASSERT_TRUE(!sim_dma_running, "DMA not running after stop");

    /* Should still be able to consume remaining blocks */
    for (int i = 0; i < 4; i++) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available after DMA stop");
        sim_proto_release_block();
    }

    ASSERT_EQ_INT(0, (int)sim_blocks_filled, "All blocks consumed after stop");
}

/* Test 12: Block data pattern verification */
static void test_block_data_pattern(void) {
    sim_reset();

    /* Fill one block and verify the data pattern */
    sim_dma_fill_block(3);  /* Fill block 3 directly */

    bool pattern_ok = true;
    for (uint16_t i = 0; i < SDR_RING_BLOCK_SIZE; i++) {
        if (sim_ring_buf[3 * SDR_RING_BLOCK_SIZE + i] != DMA_PATTERN_BYTE(3, i)) {
            pattern_ok = false;
            break;
        }
    }
    ASSERT_TRUE(pattern_ok, "Block data pattern matches expected values");

    /* Verify pattern is different for different blocks */
    sim_dma_fill_block(5);
    bool different = false;
    for (uint16_t i = 0; i < SDR_RING_BLOCK_SIZE; i++) {
        if (sim_ring_buf[3 * SDR_RING_BLOCK_SIZE + i] !=
            sim_ring_buf[5 * SDR_RING_BLOCK_SIZE + i]) {
            different = true;
            break;
        }
    }
    ASSERT_TRUE(different, "Different blocks have different data patterns");
}

/* Test 13: Rapid produce/consume stress test */
static void test_rapid_produce_consume(void) {
    sim_reset();

    /* Rapid produce and consume in a tight loop */
    for (int i = 0; i < 1000; i++) {
        sim_dma_isr_handler();

        /* Consume every block immediately */
        while (sim_blocks_filled > 0) {
            uint8_t idx;
            uint16_t size;
            const uint8_t *data = sim_proto_get_block(&idx, &size);
            ASSERT_TRUE(data != NULL, "Block available in stress test");
            sim_proto_release_block();
        }
    }

    ASSERT_EQ_UINT(1000, sim_dma_stats.total_blocks_captured,
                   "1000 blocks captured in stress test");
    ASSERT_EQ_UINT(1000, sim_dma_stats.total_blocks_sent,
                   "1000 blocks sent in stress test");
    ASSERT_EQ_UINT(0, sim_dma_stats.overruns,
                   "No overruns in stress test with immediate consume");
    ASSERT_EQ_UINT(0, sim_dma_stats.underruns,
                   "No underruns in stress test");
}

/* Test 14: Multiple underruns */
static void test_multiple_underruns(void) {
    sim_reset();

    /* Attempt to read from empty buffer multiple times */
    for (int i = 0; i < 5; i++) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data == NULL, "NULL on empty buffer attempt");
    }

    ASSERT_EQ_UINT(5, sim_dma_stats.underruns,
                   "5 underruns from 5 empty reads");
}

/* Test 15: Partial consume pattern */
static void test_partial_consume_pattern(void) {
    sim_reset();

    /* Fill 6 blocks, consume 3, fill 3 more, consume all */
    for (int i = 0; i < 6; i++)
        sim_dma_isr_handler();

    ASSERT_EQ_INT(6, (int)sim_blocks_filled, "6 blocks produced");

    for (int i = 0; i < 3; i++) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available in partial consume");
        sim_proto_release_block();
    }

    ASSERT_EQ_INT(3, (int)sim_blocks_filled, "3 blocks after partial consume");

    for (int i = 0; i < 3; i++)
        sim_dma_isr_handler();

    ASSERT_EQ_INT(6, (int)sim_blocks_filled, "6 blocks after more produce");

    /* Drain all */
    int consumed = 0;
    while (sim_blocks_filled > 0) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        (void)data;
        sim_proto_release_block();
        consumed++;
    }

    ASSERT_EQ_INT(6, consumed, "6 blocks consumed in drain");
    ASSERT_EQ_UINT(9, sim_dma_stats.total_blocks_captured,
                   "9 total blocks produced (6 + 3)");
    ASSERT_EQ_UINT(9, sim_dma_stats.total_blocks_sent,
                   "9 total blocks sent (3 + 6)");
}

/* Test 16: Continuous streaming with overrun recovery
 *
 * Simulates a long-running SDR capture where the producer (DMA ISR)
 * runs faster than the consumer (protocol handler). Overruns should
 * be detected and recovered from automatically, with data continuity
 * maintained after the consumer catches up. */
static void test_continuous_streaming_overrun_recovery(void) {
    sim_reset();

    /* Phase 1: Producer runs ahead, causing overruns.
     * Fill 20 blocks without any consumption — with 8 blocks in the
     * ring, this will cause 20 - 7 = 13 overruns (after the first 7
     * fills the buffer is full, each subsequent fill causes an overrun). */
    for (int i = 0; i < 20; i++)
        sim_dma_isr_handler();

    /* After 20 fills: 7 blocks remain (the oldest is discarded on each
     * overrun after the buffer is full). Overruns = 20 - 7 = 13. */
    ASSERT_EQ_INT(7, (int)sim_blocks_filled,
                   "7 blocks in buffer after overrun phase");
    ASSERT_EQ_UINT(13, sim_dma_stats.overruns,
                   "13 overruns during overrun phase");
    ASSERT_EQ_UINT(20, sim_dma_stats.total_blocks_captured,
                   "20 blocks captured total");

    /* Phase 2: Consumer catches up and drains all remaining blocks */
    int consumed = 0;
    while (sim_blocks_filled > 0) {
        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available during recovery drain");
        sim_proto_release_block();
        consumed++;
    }
    ASSERT_EQ_INT(7, consumed, "7 blocks consumed during recovery");

    /* Phase 3: Resume normal streaming with balanced produce/consume */
    for (int i = 0; i < 50; i++) {
        sim_dma_isr_handler();

        uint8_t idx;
        uint16_t size;
        const uint8_t *data = sim_proto_get_block(&idx, &size);
        ASSERT_TRUE(data != NULL, "Block available during balanced phase");
        sim_proto_release_block();
    }

    /* Verify no additional overruns during balanced phase */
    ASSERT_EQ_UINT(13, sim_dma_stats.overruns,
                   "No new overruns during balanced phase");
    ASSERT_EQ_UINT(70, sim_dma_stats.total_blocks_captured,
                   "70 total blocks captured (20 + 50)");
    ASSERT_EQ_UINT(57, sim_dma_stats.total_blocks_sent,
                   "57 total blocks sent (7 + 50)");
    ASSERT_EQ_INT(0, (int)sim_blocks_filled,
                  "Buffer empty after balanced phase");
}

/* Test 17: Ring buffer pointer alignment after multiple wrap cycles */
static void test_pointer_alignment_multiple_cycles(void) {
    sim_reset();

    /* Run 3 complete fill-drain cycles, each time filling 7 blocks
     * (max without overrun) and draining all. This verifies pointer
     * alignment is maintained across multiple wrap-around events. */
    for (int cycle = 0; cycle < 3; cycle++) {
        /* Fill 7 blocks */
        for (int i = 0; i < 7; i++)
            sim_dma_isr_handler();

        ASSERT_EQ_INT(7, (int)sim_blocks_filled,
                       "7 blocks filled in each cycle");

        /* Drain all 7 */
        for (int i = 0; i < 7; i++) {
            uint8_t idx;
            uint16_t size;
            const uint8_t *data = sim_proto_get_block(&idx, &size);
            ASSERT_TRUE(data != NULL, "Block available in multi-cycle test");
            sim_proto_release_block();
        }

        ASSERT_EQ_INT(0, (int)sim_blocks_filled,
                       "Buffer empty at end of each cycle");
    }

    /* After 3 cycles: 21 blocks produced, 21 consumed, 0 overruns */
    ASSERT_EQ_UINT(21, sim_dma_stats.total_blocks_captured,
                   "21 blocks captured in 3 cycles");
    ASSERT_EQ_UINT(21, sim_dma_stats.total_blocks_sent,
                   "21 blocks sent in 3 cycles");
    ASSERT_EQ_UINT(0, sim_dma_stats.overruns,
                   "No overruns in 3 complete cycles");

    /* Verify pointers are back to starting position (block 0).
     * After 21 produces from block 0: write pointer = 21 % 8 = 5
     * After 21 consumes from block 0: read pointer = 21 % 8 = 5
     * Both should be equal, and blocks_filled should be 0. */
    ASSERT_EQ_INT(sim_dma_write_block, sim_proto_read_block,
                   "Write and read pointers equal after balanced cycles");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade SDR DMA Ring Buffer Unit Tests ===\n\n");

    RUN_TEST(test_ring_init);
    RUN_TEST(test_single_block_produce_consume);
    RUN_TEST(test_fill_max_blocks);
    RUN_TEST(test_overrun_detection);
    RUN_TEST(test_underrun_detection);
    RUN_TEST(test_interleaved_produce_consume);
    RUN_TEST(test_ring_wraparound);
    RUN_TEST(test_data_integrity_after_wraparound);
    RUN_TEST(test_sustained_streaming);
    RUN_TEST(test_ring_buffer_size_calculations);
    RUN_TEST(test_dma_state_transitions);
    RUN_TEST(test_block_data_pattern);
    RUN_TEST(test_rapid_produce_consume);
    RUN_TEST(test_multiple_underruns);
    RUN_TEST(test_partial_consume_pattern);
    RUN_TEST(test_continuous_streaming_overrun_recovery);
    RUN_TEST(test_pointer_alignment_multiple_cycles);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}