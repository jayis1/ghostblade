/*
 * test_spi0_isr.c — Unit Tests for SPI0 ISR Frame Assembly State Machine
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the SPI0 slave ISR frame assembly state machine
 * implemented in spi0_isr.c. These tests exercise the byte-by-byte
 * frame assembly logic, CRC validation, error recovery, and edge cases.
 *
 * The test simulates the ISR's frame assembly by feeding bytes one at
 * a time through the state machine and verifying correct transitions
 * and error handling, matching the production spi0_process_byte() logic.
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -std=c11 -I../firmware/rp2350b/include \
 *       test_spi0_isr.c -o test_spi0_isr
 *
 * Run:
 *   ./test_spi0_isr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * CRC-64/ECMA-182 Implementation (matches firmware)
 * ======================================================================== */

#define CRC64_POLY   0x42F0E1EBA9EA3693ULL

static uint64_t crc64_table[256];
static bool crc64_table_initialized = false;

static void crc64_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC64_POLY;
            else
                crc >>= 1;
        }
        crc64_table[i] = crc;
    }
    crc64_table_initialized = true;
}

static uint64_t crc64_compute(const uint8_t *data, size_t len) {
    if (!crc64_table_initialized)
        crc64_init_table();

    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (size_t i = 0; i < len; i++) {
        crc = crc64_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/* ========================================================================
 * CRC-32 Implementation (matches firmware)
 * ======================================================================== */

#define CRC32_POLY   0xEDB88320UL

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC32_POLY;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

static uint32_t crc32_compute(const uint8_t *data, size_t len) {
    if (!crc32_table_initialized)
        crc32_init_table();

    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * SPI Frame Format Definitions (matches spi_protocol.h)
 * ======================================================================== */

#define SPI_SYNC_BYTE    0xAA
#define SPI_HDR_SIZE     16
#define SPI_MAX_PAYLOAD  4092
#define SPI_CRC32_SIZE   4

/* Command opcodes */
#define CMD_NOP           0xFF
#define CMD_SDR_TUNE      0x01
#define CMD_SDR_STREAM    0x02
#define CMD_ANT_SELECT    0x03
#define CMD_CC1101_CFG    0x04
#define CMD_NFC_TRANSACT  0x05
#define CMD_TELEMETRY_REQ 0x06
#define CMD_RESET_MCU     0x07

/* ========================================================================
 * Frame Builder (same as test_crc_validation.c)
 * ======================================================================== */

static int build_spi_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t payload_len, uint8_t *frame) {
    if (payload_len > SPI_MAX_PAYLOAD)
        return -1;

    int idx = 0;

    /* Header bytes 0-7 */
    frame[idx++] = SPI_SYNC_BYTE;
    frame[idx++] = cmd;
    frame[idx++] = (uint8_t)(payload_len & 0xFF);
    frame[idx++] = (uint8_t)((payload_len >> 8) & 0xFF);
    frame[idx++] = 0x00;
    frame[idx++] = 0x00;
    frame[idx++] = 0x00;
    frame[idx++] = 0x00;

    /* Header CRC-64 over bytes 0-7 */
    uint64_t hdr_crc = crc64_compute(frame, 8);
    frame[idx++] = (uint8_t)(hdr_crc & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 8) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 16) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 24) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 32) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 40) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 48) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 56) & 0xFF);

    /* Payload */
    if (payload_len > 0 && payload != NULL) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }

    /* Payload CRC-32 */
    if (payload_len > 0) {
        uint32_t pay_crc = crc32_compute(payload, payload_len);
        frame[idx++] = (uint8_t)(pay_crc & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 8) & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 16) & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 24) & 0xFF);
    }

    return idx;
}

/* ========================================================================
 * ISR Frame Assembly State Machine Simulation
 *
 * This replicates the byte-by-byte frame assembly state machine from
 * spi0_isr.c's spi0_process_byte() function. We simulate it in
 * userspace to test the state transitions independently of hardware.
 * ======================================================================== */

enum frame_state {
    FRAME_STATE_IDLE = 0,
    FRAME_STATE_HEADER,
    FRAME_STATE_PAYLOAD,
    FRAME_STATE_COMPLETE,
    FRAME_STATE_ERROR,
};

/* Simulated ISR receive context */
static struct {
    uint8_t buf[SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE];
    uint16_t pos;
    enum frame_state state;
    uint16_t payload_len;
    bool frame_ready;
} sim_rx;

/* Simulated ISR statistics */
static struct {
    uint32_t frames_received;
    uint32_t frames_validated;
    uint32_t frames_rejected_sync;
    uint32_t frames_rejected_hdr_crc;
    uint32_t frames_rejected_pay_crc;
    uint32_t frames_rejected_len;
    uint32_t rx_overflows;
    uint32_t bytes_received;
} sim_stats;

static void sim_reset(void) {
    memset(sim_rx.buf, 0, sizeof(sim_rx.buf));
    sim_rx.pos = 0;
    sim_rx.state = FRAME_STATE_IDLE;
    sim_rx.payload_len = 0;
    sim_rx.frame_ready = false;
    memset(&sim_stats, 0, sizeof(sim_stats));
}

/**
 * sim_validate_sync_byte — Check if a byte is a valid sync marker
 */
static bool sim_validate_sync_byte(uint8_t byte) {
    return byte == SPI_SYNC_BYTE;
}

/**
 * sim_validate_header_crc64 — Validate CRC-64 over the frame header
 */
static bool sim_validate_header_crc64(const uint8_t *buf) {
    uint64_t computed = crc64_compute(buf, 8);
    uint64_t expected = (uint64_t)buf[8]        |
                        ((uint64_t)buf[9] << 8)   |
                        ((uint64_t)buf[10] << 16)  |
                        ((uint64_t)buf[11] << 24)  |
                        ((uint64_t)buf[12] << 32)  |
                        ((uint64_t)buf[13] << 40)  |
                        ((uint64_t)buf[14] << 48)  |
                        ((uint64_t)buf[15] << 56);
    return computed == expected;
}

/**
 * sim_validate_payload_crc32 — Validate CRC-32 over the frame payload
 */
static bool sim_validate_payload_crc32(const uint8_t *buf, uint16_t payload_len) {
    uint32_t computed;
    if (payload_len == 0) {
        computed = crc32_compute(buf, 0);
    } else {
        computed = crc32_compute(&buf[SPI_HDR_SIZE], payload_len);
    }
    uint32_t offset = SPI_HDR_SIZE + payload_len;
    uint32_t expected = (uint32_t)buf[offset]       |
                        ((uint32_t)buf[offset + 1] << 8)  |
                        ((uint32_t)buf[offset + 2] << 16) |
                        ((uint32_t)buf[offset + 3] << 24);
    return computed == expected;
}

/**
 * sim_extract_payload_length — Extract payload length from header
 */
static uint16_t sim_extract_payload_length(const uint8_t *buf) {
    return (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
}

/**
 * sim_process_byte — Process one received byte through the state machine
 *
 * This is a faithful replica of spi0_process_byte() from spi0_isr.c.
 */
static void sim_process_byte(uint8_t byte) {
    sim_stats.bytes_received++;

    switch (sim_rx.state) {
    case FRAME_STATE_IDLE:
        if (sim_validate_sync_byte(byte)) {
            sim_rx.buf[0] = byte;
            sim_rx.pos = 1;
            sim_rx.state = FRAME_STATE_HEADER;
        } else {
            sim_stats.frames_rejected_sync++;
        }
        break;

    case FRAME_STATE_HEADER:
        if (sim_rx.pos < SPI_HDR_SIZE) {
            sim_rx.buf[sim_rx.pos++] = byte;

            if (sim_rx.pos == SPI_HDR_SIZE) {
                if (!sim_validate_header_crc64(sim_rx.buf)) {
                    sim_stats.frames_rejected_hdr_crc++;
                    sim_rx.state = FRAME_STATE_ERROR;
                    break;
                }

                sim_rx.payload_len = sim_extract_payload_length(sim_rx.buf);

                if (sim_rx.payload_len > SPI_MAX_PAYLOAD) {
                    sim_stats.frames_rejected_len++;
                    sim_rx.state = FRAME_STATE_ERROR;
                    break;
                }

                sim_rx.state = FRAME_STATE_PAYLOAD;
            }
        }
        break;

    case FRAME_STATE_PAYLOAD:
        if (sim_rx.pos < SPI_HDR_SIZE + sim_rx.payload_len + SPI_CRC32_SIZE) {
            sim_rx.buf[sim_rx.pos++] = byte;

            if (sim_rx.pos == SPI_HDR_SIZE + sim_rx.payload_len + SPI_CRC32_SIZE) {
                if (sim_validate_payload_crc32(sim_rx.buf, sim_rx.payload_len)) {
                    sim_stats.frames_validated++;
                    sim_stats.frames_received++;
                    sim_rx.state = FRAME_STATE_COMPLETE;
                    sim_rx.frame_ready = true;
                } else {
                    sim_stats.frames_rejected_pay_crc++;
                    sim_rx.state = FRAME_STATE_ERROR;
                }
            }
        } else {
            sim_rx.state = FRAME_STATE_ERROR;
        }
        break;

    case FRAME_STATE_COMPLETE:
        sim_stats.rx_overflows++;
        break;

    case FRAME_STATE_ERROR:
        if (sim_validate_sync_byte(byte)) {
            sim_rx.buf[0] = byte;
            sim_rx.pos = 1;
            sim_rx.state = FRAME_STATE_HEADER;
        }
        break;
    }
}

/**
 * sim_release_frame — Release the current frame and reset state machine
 */
static void sim_release_frame(void) {
    volatile uint8_t *p = (volatile uint8_t *)sim_rx.buf;
    for (uint16_t i = 0; i < sim_rx.pos; i++)
        p[i] = 0;

    sim_rx.frame_ready = false;
    sim_rx.pos = 0;
    sim_rx.payload_len = 0;
    sim_rx.state = FRAME_STATE_IDLE;
}

/**
 * sim_feed_frame — Feed an entire frame byte-by-byte into the state machine
 */
static void sim_feed_frame(const uint8_t *frame, int len) {
    for (int i = 0; i < len; i++) {
        sim_process_byte(frame[i]);
    }
}

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_EQ(actual, expected, msg) \
    TEST_ASSERT((actual) == (expected), msg)

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/**
 * Test 1: NOP frame assembly through ISR state machine
 */
static void test_isr_nop_frame(void) {
    printf("Test: ISR NOP frame assembly\n");
    uint8_t frame[32];
    int len;

    sim_reset();
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "NOP frame: state = COMPLETE");
    TEST_ASSERT(sim_rx.frame_ready, "NOP frame: frame_ready = true");
    TEST_ASSERT_EQ((int)sim_rx.payload_len, 0,
                   "NOP frame: payload_len = 0");
    TEST_ASSERT_EQ((int)sim_rx.pos, SPI_HDR_SIZE,
                   "NOP frame: pos = 16 (header only)");
    TEST_ASSERT_EQ((int)sim_stats.frames_validated, 1,
                   "NOP frame: frames_validated = 1");
    TEST_ASSERT_EQ((int)sim_stats.frames_received, 1,
                   "NOP frame: frames_received = 1");

    /* Verify sync byte and command in assembled buffer */
    TEST_ASSERT_EQ((int)sim_rx.buf[0], SPI_SYNC_BYTE,
                   "NOP frame: buf[0] = 0xAA (sync)");
    TEST_ASSERT_EQ((int)sim_rx.buf[1], CMD_NOP,
                   "NOP frame: buf[1] = 0xFF (NOP cmd)");
}

/**
 * Test 2: Frame with payload through ISR state machine
 */
static void test_isr_payload_frame(void) {
    printf("Test: ISR payload frame assembly\n");
    uint8_t frame[64];
    uint8_t payload[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    int len;

    sim_reset();
    len = build_spi_frame(CMD_SDR_TUNE, payload, sizeof(payload), frame);
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "SDR_TUNE frame: state = COMPLETE");
    TEST_ASSERT(sim_rx.frame_ready, "SDR_TUNE frame: frame_ready = true");
    TEST_ASSERT_EQ((int)sim_rx.payload_len, 8,
                   "SDR_TUNE frame: payload_len = 8");
    TEST_ASSERT_EQ((int)sim_stats.frames_validated, 1,
                   "SDR_TUNE frame: frames_validated = 1");

    /* Verify payload content matches */
    TEST_ASSERT(memcmp(&sim_rx.buf[SPI_HDR_SIZE], payload, 8) == 0,
                "SDR_TUNE frame: payload content matches");
}

/**
 * Test 3: Corrupted sync byte is rejected
 */
static void test_isr_bad_sync(void) {
    printf("Test: ISR corrupted sync byte\n");
    uint8_t frame[32];
    int len;

    sim_reset();
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);

    /* Corrupt the sync byte */
    frame[0] = 0x55;
    sim_feed_frame(frame, len);

    /* Frame should stay in IDLE state (sync byte not recognized) */
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_IDLE,
                   "Bad sync: state stays IDLE");
    TEST_ASSERT(!sim_rx.frame_ready, "Bad sync: frame_ready = false");
    TEST_ASSERT_EQ((int)sim_stats.frames_rejected_sync, (int)len,
                   "Bad sync: all bytes rejected (not just first)");
}

/**
 * Test 4: Corrupted header CRC is detected
 */
static void test_isr_bad_header_crc(void) {
    printf("Test: ISR corrupted header CRC\n");
    uint8_t frame[32];
    int len;

    sim_reset();
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);

    /* Corrupt a header byte (byte 1 = command) — this invalidates the CRC-64 */
    frame[1] ^= 0x01;
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_ERROR,
                   "Bad header CRC: state = ERROR");
    TEST_ASSERT(!sim_rx.frame_ready, "Bad header CRC: frame_ready = false");
    TEST_ASSERT(sim_stats.frames_rejected_hdr_crc >= 1,
                "Bad header CRC: header CRC rejection counted");
}

/**
 * Test 5: Corrupted payload CRC-32 is detected
 */
static void test_isr_bad_payload_crc(void) {
    printf("Test: ISR corrupted payload CRC-32\n");
    uint8_t frame[64];
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    int len;

    sim_reset();
    len = build_spi_frame(CMD_ANT_SELECT, payload, sizeof(payload), frame);

    /* Corrupt a payload byte */
    frame[SPI_HDR_SIZE] ^= 0x01;
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_ERROR,
                   "Bad payload CRC: state = ERROR");
    TEST_ASSERT(!sim_rx.frame_ready, "Bad payload CRC: frame_ready = false");
    TEST_ASSERT(sim_stats.frames_rejected_pay_crc >= 1,
                "Bad payload CRC: payload CRC rejection counted");
}

/**
 * Test 6: Payload length exceeding maximum is rejected
 */
static void test_isr_oversized_payload(void) {
    printf("Test: ISR oversized payload\n");
    uint8_t frame[8192];
    uint8_t payload[SPI_MAX_PAYLOAD + 1];
    int len;

    /* build_spi_frame rejects oversized payloads, so we manually construct
     * a frame with an oversized payload_len field. */
    sim_reset();

    memset(payload, 0x42, sizeof(payload));
    /* Manually craft a frame header with payload_len > SPI_MAX_PAYLOAD */
    uint8_t bad_frame[SPI_HDR_SIZE];
    bad_frame[0] = SPI_SYNC_BYTE;
    bad_frame[1] = CMD_NOP;
    bad_frame[2] = 0xFF;  /* payload_len = 0x0FFF = 4095 > SPI_MAX_PAYLOAD(4092) */
    bad_frame[3] = 0x0F;
    bad_frame[4] = 0x00;
    bad_frame[5] = 0x00;
    bad_frame[6] = 0x00;
    bad_frame[7] = 0x00;

    /* Compute header CRC-64 over the header bytes */
    uint64_t hdr_crc = crc64_compute(bad_frame, 8);
    /* We only feed the header portion to check that the state machine
     * rejects the oversized payload length. */
    for (int i = 0; i < 8; i++)
        sim_process_byte(bad_frame[i]);

    /* Feed the CRC-64 bytes */
    for (int i = 0; i < 8; i++)
        sim_process_byte((uint8_t)((hdr_crc >> (8 * i)) & 0xFF));

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_ERROR,
                   "Oversized payload: state = ERROR after header");
    TEST_ASSERT(sim_stats.frames_rejected_len >= 1,
                "Oversized payload: length rejection counted");
}

/**
 * Test 7: Error recovery — resync after corrupted frame
 */
static void test_isr_resync_after_error(void) {
    printf("Test: ISR resync after error\n");
    uint8_t frame[32];
    int len;

    sim_reset();

    /* First, feed a corrupted frame */
    uint8_t bad_frame[20];
    bad_frame[0] = SPI_SYNC_BYTE;
    bad_frame[1] = 0xFF;
    bad_frame[2] = 0x00;
    bad_frame[3] = 0x00;
    /* Corrupt the header to trigger error state */
    bad_frame[4] = 0xFF;  /* Non-zero reserved field */
    bad_frame[5] = 0xFF;
    bad_frame[6] = 0xFF;
    bad_frame[7] = 0xFF;
    uint64_t bad_hdr_crc = crc64_compute(bad_frame, 8);
    for (int i = 0; i < 8; i++)
        bad_frame[8 + i] = (uint8_t)((bad_hdr_crc >> (8 * i)) & 0xFF);

    /* Feed the corrupted frame — CRC-64 should still validate because
     * we computed it over the corrupted header. But the reserved field
     * is nonzero, which the production code might check. Actually,
     * the ISR doesn't check the reserved field — it only checks CRC.
     * So let's corrupt the CRC instead. */
    bad_frame[8] ^= 0x01;  /* Flip one bit in the CRC */

    sim_feed_frame(bad_frame, SPI_HDR_SIZE);
    /* Should be in ERROR state now */
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_ERROR,
                   "Resync: state = ERROR after bad CRC");

    /* Now feed a valid NOP frame — should resync on the sync byte */
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Resync: state = COMPLETE after valid frame");
    TEST_ASSERT(sim_rx.frame_ready, "Resync: frame_ready = true after resync");
}

/**
 * Test 8: Multiple consecutive frames
 */
static void test_isr_multiple_frames(void) {
    printf("Test: ISR multiple consecutive frames\n");
    uint8_t frame[64];
    int len;

    sim_reset();

    /* Frame 1: NOP */
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);
    sim_feed_frame(frame, len);
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Multi-frame 1: COMPLETE");
    sim_release_frame();

    /* Frame 2: SDR_TUNE */
    uint8_t payload[8] = { 0x00, 0x00, 0x89, 0x33, 0xD0, 0x07, 0x2C, 0x01 };
    len = build_spi_frame(CMD_SDR_TUNE, payload, sizeof(payload), frame);
    sim_feed_frame(frame, len);
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Multi-frame 2: COMPLETE");
    sim_release_frame();

    /* Frame 3: ANT_SELECT */
    uint8_t ant_payload[1] = { 0x01 };
    len = build_spi_frame(CMD_ANT_SELECT, ant_payload, 1, frame);
    sim_feed_frame(frame, len);
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Multi-frame 3: COMPLETE");

    TEST_ASSERT_EQ((int)sim_stats.frames_validated, 3,
                   "Multi-frame: 3 frames validated");
    TEST_ASSERT_EQ((int)sim_stats.frames_received, 3,
                   "Multi-frame: 3 frames received");
}

/**
 * Test 9: Maximum payload frame through ISR state machine
 */
static void test_isr_max_payload(void) {
    printf("Test: ISR maximum payload frame\n");
    uint8_t *frame = malloc(SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE + 1);
    uint8_t *payload = malloc(SPI_MAX_PAYLOAD);
    int len;

    TEST_ASSERT(frame != NULL && payload != NULL,
                "Max payload: allocation succeeded");

    memset(payload, 0x5A, SPI_MAX_PAYLOAD);
    len = build_spi_frame(CMD_SDR_STREAM, payload, SPI_MAX_PAYLOAD, frame);
    TEST_ASSERT(len > 0, "Max payload: frame built successfully");

    sim_reset();
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Max payload: state = COMPLETE");
    TEST_ASSERT(sim_rx.frame_ready, "Max payload: frame_ready = true");
    TEST_ASSERT_EQ((int)sim_rx.payload_len, SPI_MAX_PAYLOAD,
                   "Max payload: payload_len = 4092");
    TEST_ASSERT(memcmp(&sim_rx.buf[SPI_HDR_SIZE], payload, SPI_MAX_PAYLOAD) == 0,
                "Max payload: payload content matches");

    free(payload);
    free(frame);
}

/**
 * Test 10: Inter-frame garbage bytes are ignored
 */
static void test_isr_inter_frame_garbage(void) {
    printf("Test: ISR inter-frame garbage bytes\n");
    uint8_t frame[32];
    int len;

    sim_reset();
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);

    /* Feed garbage bytes before the valid frame */
    uint8_t garbage[] = { 0x00, 0x55, 0xFF, 0x01, 0x02, 0x03 };
    sim_feed_frame(garbage, sizeof(garbage));

    /* Should be in IDLE state (no sync byte found) */
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_IDLE,
                   "Garbage: state = IDLE after garbage");

    /* Now feed the valid frame */
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Garbage: state = COMPLETE after valid frame");
    TEST_ASSERT(sim_rx.frame_ready, "Garbage: frame_ready = true");
    TEST_ASSERT_EQ((int)sim_stats.frames_validated, 1,
                   "Garbage: 1 frame validated");
    /* The garbage bytes should have been counted as sync rejections */
    TEST_ASSERT(sim_stats.frames_rejected_sync > 0,
                "Garbage: sync rejections > 0");
}

/**
 * Test 11: Overflow — frame arrives before previous one is consumed
 */
static void test_isr_overflow(void) {
    printf("Test: ISR frame overflow\n");
    uint8_t frame1[32], frame2[32];
    int len1, len2;

    sim_reset();
    len1 = build_spi_frame(CMD_NOP, NULL, 0, frame1);
    len2 = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame2);

    /* Feed first frame */
    sim_feed_frame(frame1, len1);
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Overflow: first frame COMPLETE");
    TEST_ASSERT(sim_rx.frame_ready, "Overflow: first frame ready");

    /* Don't release — feed second frame. The state machine is in
     * COMPLETE state, so all bytes of the second frame should
     * be counted as overflows. */
    sim_feed_frame(frame2, len2);

    /* Should still be in COMPLETE state (not transitioned) */
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Overflow: state still COMPLETE");
    TEST_ASSERT(sim_stats.rx_overflows > 0,
                "Overflow: overflow counter > 0");

    /* Now release and try again */
    sim_release_frame();
    sim_feed_frame(frame2, len2);
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Overflow: second frame COMPLETE after release");
}

/**
 * Test 12: Zero-length payload frame (NOP with no CRC-32 trailer)
 *
 * Note: In the current implementation, zero-length payload frames
 * still include a CRC-32 trailer (CRC-32 of empty data = 0x00000000).
 * The ISR processes 4 bytes for the CRC-32 after the header.
 */
static void test_isr_zero_payload_with_crc(void) {
    printf("Test: ISR zero-length payload frame with CRC-32\n");
    uint8_t frame[32];
    int len;

    sim_reset();
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);

    /* NOP has zero payload but still includes CRC-32 of empty data.
     * However, build_spi_frame only adds CRC-32 for non-zero payloads.
     * The SPI protocol defines that zero-length payload frames have
     * exactly SPI_HDR_SIZE bytes (no CRC-32 trailer).
     *
     * Let's verify: a 16-byte NOP frame is valid. */
    TEST_ASSERT_EQ(len, SPI_HDR_SIZE, "Zero payload: frame = 16 bytes");

    sim_feed_frame(frame, len);
    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Zero payload: state = COMPLETE");
    TEST_ASSERT(sim_rx.frame_ready, "Zero payload: frame_ready = true");
    TEST_ASSERT_EQ((int)sim_rx.payload_len, 0,
                   "Zero payload: payload_len = 0");
}

/**
 * Test 13: Verify ISR statistics accounting
 */
static void test_isr_statistics(void) {
    printf("Test: ISR statistics accounting\n");
    uint8_t frame[64];
    int len;
    uint8_t payload[4] = { 0x42, 0x43, 0x44, 0x45 };

    sim_reset();

    /* Feed 5 valid frames */
    for (int i = 0; i < 5; i++) {
        len = build_spi_frame(CMD_NOP, NULL, 0, frame);
        sim_feed_frame(frame, len);
        sim_release_frame();
    }

    TEST_ASSERT_EQ((int)sim_stats.frames_validated, 5,
                   "Stats: 5 frames validated");
    TEST_ASSERT_EQ((int)sim_stats.frames_received, 5,
                   "Stats: 5 frames received");
    TEST_ASSERT_EQ((int)sim_stats.frames_rejected_sync, 0,
                   "Stats: 0 sync rejections");
    TEST_ASSERT_EQ((int)sim_stats.frames_rejected_hdr_crc, 0,
                   "Stats: 0 header CRC rejections");
    TEST_ASSERT_EQ((int)sim_stats.frames_rejected_pay_crc, 0,
                   "Stats: 0 payload CRC rejections");
    TEST_ASSERT_EQ((int)sim_stats.rx_overflows, 0,
                   "Stats: 0 overflows");

    /* Now feed a frame with corrupted header CRC */
    sim_reset();
    len = build_spi_frame(CMD_SDR_TUNE, payload, sizeof(payload), frame);
    frame[1] ^= 0xFF;  /* Corrupt command byte */
    sim_feed_frame(frame, len);

    TEST_ASSERT_EQ((int)sim_stats.frames_rejected_hdr_crc, 1,
                   "Stats: 1 header CRC rejection after corruption");
}

/**
 * Test 14: Byte-by-byte feeding (simulating slow SPI clock)
 */
static void test_isr_byte_by_byte(void) {
    printf("Test: ISR byte-by-byte feeding\n");
    uint8_t frame[64];
    uint8_t payload[4] = { 0xCA, 0xFE, 0xBA, 0xBE };
    int len;

    sim_reset();
    len = build_spi_frame(CMD_ANT_SELECT, payload, sizeof(payload), frame);

    /* Feed one byte at a time with explicit state checks */
    for (int i = 0; i < len; i++) {
        /* Before first byte: IDLE state */
        if (i == 0) {
            TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_IDLE,
                           "Byte-by-byte: starts in IDLE");
        }

        sim_process_byte(frame[i]);

        if (i == 0) {
            /* After sync byte: transition to HEADER */
            TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_HEADER,
                           "Byte-by-byte: sync → HEADER");
        } else if (i < SPI_HDR_SIZE - 1) {
            /* Still accumulating header */
            TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_HEADER,
                           "Byte-by-byte: accumulating header");
        } else if (i == SPI_HDR_SIZE - 1) {
            /* Just completed header — should transition to PAYLOAD */
            /* Note: pos is now SPI_HDR_SIZE, which equals SPI_HDR_SIZE
             * after processing the last header byte */
        }
    }

    TEST_ASSERT_EQ((int)sim_rx.state, (int)FRAME_STATE_COMPLETE,
                   "Byte-by-byte: ends in COMPLETE");
    TEST_ASSERT(sim_rx.frame_ready, "Byte-by-byte: frame_ready = true");
    TEST_ASSERT_EQ((int)sim_rx.payload_len, 4,
                   "Byte-by-byte: payload_len = 4");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade SPI0 ISR Frame Assembly Tests ===\n\n");

    /* Initialize CRC tables */
    crc64_init_table();
    crc32_init_table();

    test_isr_nop_frame();
    test_isr_payload_frame();
    test_isr_bad_sync();
    test_isr_bad_header_crc();
    test_isr_bad_payload_crc();
    test_isr_oversized_payload();
    test_isr_resync_after_error();
    test_isr_multiple_frames();
    test_isr_max_payload();
    test_isr_inter_frame_garbage();
    test_isr_overflow();
    test_isr_zero_payload_with_crc();
    test_isr_statistics();
    test_isr_byte_by_byte();

    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}