/*
 * test_spi0_isr.c — Unit Tests for SPI0 ISR Frame Assembly State Machine
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the SPI0 interrupt service routine's frame assembly
 * logic. These tests exercise the state machine that receives bytes
 * from the SPI0 RX FIFO and assembles them into complete frames
 * with CRC-64 and CRC-32 validation.
 *
 * The tests simulate the byte-by-byte arrival of SPI frames and
 * verify that the state machine correctly:
 *   - Synchronizes on the 0xAA sync byte
 *   - Assembles header and payload data
 *   - Validates CRC-64 header checksums
 *   - Validates CRC-32 payload checksums
 *   - Recovers from frame errors (resync)
 *   - Handles back-to-back frames
 *   - Rejects frames with corrupted data
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_spi0_isr test_spi0_isr.c -lm
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
 * SPI Protocol Constants (must match spi_protocol.h)
 * ======================================================================== */

#define SPI_SYNC_BYTE       0xAA
#define SPI_HDR_SIZE        16
#define SPI_MAX_PAYLOAD     4092
#define SPI_CRC32_SIZE      4
#define SPI_FRAME_SIZE_MAX  (SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE)

/* Command opcodes */
#define CMD_NOP              0x00
#define CMD_SDR_TUNE         0x01
#define CMD_SDR_STREAM       0x02
#define CMD_ANT_SELECT       0x03
#define CMD_CC1101_CFG       0x04
#define CMD_NFC_TRANSACT     0x05
#define CMD_TELEMETRY_REQ    0x06
#define CMD_RESET_MCU        0x07
#define CMD_TELEMETRY        0x81
#define CMD_SDR_IQ_CHUNK     0x82

/* Frame state machine states */
enum frame_state {
    FRAME_STATE_IDLE = 0,
    FRAME_STATE_HEADER,
    FRAME_STATE_PAYLOAD,
    FRAME_STATE_COMPLETE,
    FRAME_STATE_ERROR,
};

/* ========================================================================
 * CRC-64/ECMA-182 and CRC-32/ISO-3309 (same as spi_protocol.c)
 * ======================================================================== */

static uint64_t crc64_table[256];
static uint32_t crc32_table[256];
static int crc_tables_initialized = 0;

static void crc64_init_table(void) {
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc64_table[i] = crc;
    }
}

static void crc32_init_table(void) {
    const uint32_t poly = 0xEDB88320UL;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}

static uint64_t crc64_compute(const uint8_t *data, uint32_t len) {
    if (!crc_tables_initialized) {
        crc64_init_table();
        crc32_init_table();
        crc_tables_initialized = 1;
    }
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc64_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    if (!crc_tables_initialized) {
        crc64_init_table();
        crc32_init_table();
        crc_tables_initialized = 1;
    }
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * Frame Builder (same as test_spi_protocol.c)
 * ======================================================================== */

static int build_spi_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t payload_len, uint8_t *buf, uint16_t buf_size) {
    if (payload_len > SPI_MAX_PAYLOAD)
        return -1;
    if (buf_size < SPI_HDR_SIZE + payload_len + SPI_CRC32_SIZE)
        return -1;

    /* Header */
    buf[0] = SPI_SYNC_BYTE;
    buf[1] = cmd;
    buf[2] = (uint8_t)(payload_len & 0xFF);
    buf[3] = (uint8_t)((payload_len >> 8) & 0xFF);
    buf[4] = 0; buf[5] = 0; buf[6] = 0; buf[7] = 0;

    /* CRC-64 over header (bytes 0-7) */
    uint64_t hdr_crc = crc64_compute(buf, 8);
    for (int i = 0; i < 8; i++) {
        buf[8 + i] = (uint8_t)(hdr_crc >> (i * 8));
    }

    /* Payload */
    if (payload_len > 0 && payload != NULL) {
        memcpy(&buf[SPI_HDR_SIZE], payload, payload_len);
    }

    /* CRC-32 over payload */
    uint32_t pay_crc;
    if (payload_len > 0)
        pay_crc = crc32_compute(&buf[SPI_HDR_SIZE], payload_len);
    else
        pay_crc = crc32_compute((uint8_t *)"", 0);

    buf[SPI_HDR_SIZE + payload_len]     = (uint8_t)(pay_crc & 0xFF);
    buf[SPI_HDR_SIZE + payload_len + 1] = (uint8_t)((pay_crc >> 8) & 0xFF);
    buf[SPI_HDR_SIZE + payload_len + 2] = (uint8_t)((pay_crc >> 16) & 0xFF);
    buf[SPI_HDR_SIZE + payload_len + 3] = (uint8_t)((pay_crc >> 24) & 0xFF);

    return SPI_HDR_SIZE + payload_len + SPI_CRC32_SIZE;
}

/* ========================================================================
 * Simulated Frame Assembly State Machine
 *
 * We replicate the spi0_isr.c state machine logic here for userspace
 * testing without hardware dependencies.
 * ======================================================================== */

#define FRAME_BUF_SIZE (SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE)

static struct {
    uint8_t buf[FRAME_BUF_SIZE];
    uint16_t pos;
    enum frame_state state;
    uint16_t payload_len;
    volatile bool frame_ready;
} sim_rx;

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

static bool validate_sync_byte_sim(uint8_t byte) {
    return byte == SPI_SYNC_BYTE;
}

static bool validate_header_crc64_sim(const uint8_t *buf) {
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

static bool validate_payload_crc32_sim(const uint8_t *buf, uint16_t payload_len) {
    uint32_t computed;
    if (payload_len == 0) {
        computed = crc32_compute((uint8_t *)"", 0);
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

static uint16_t extract_payload_length_sim(const uint8_t *buf) {
    return (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
}

static void sim_reset(void) {
    memset(&sim_rx, 0, sizeof(sim_rx));
    sim_rx.state = FRAME_STATE_IDLE;
    memset(&sim_stats, 0, sizeof(sim_stats));
}

static void sim_reset_stats_only(void) {
    memset(&sim_stats, 0, sizeof(sim_stats));
}

/**
 * sim_process_byte — Process one byte through the state machine
 *
 * This is a direct copy of the spi0_process_byte() logic from spi0_isr.c,
 * adapted for userspace testing.
 */
static void sim_process_byte(uint8_t byte) {
    sim_stats.bytes_received++;

    switch (sim_rx.state) {
    case FRAME_STATE_IDLE:
        if (validate_sync_byte_sim(byte)) {
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
                if (!validate_header_crc64_sim(sim_rx.buf)) {
                    sim_stats.frames_rejected_hdr_crc++;
                    sim_rx.state = FRAME_STATE_ERROR;
                    break;
                }

                sim_rx.payload_len = extract_payload_length_sim(sim_rx.buf);

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
                if (validate_payload_crc32_sim(sim_rx.buf, sim_rx.payload_len)) {
                    sim_stats.frames_validated++;
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
        if (validate_sync_byte_sim(byte)) {
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
    sim_rx.frame_ready = false;
    sim_rx.pos = 0;
    sim_rx.payload_len = 0;
    sim_rx.state = FRAME_STATE_IDLE;
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

/* Test 1: State machine starts in IDLE state */
static void test_initial_state(void) {
    sim_reset();

    ASSERT_EQ_INT(FRAME_STATE_IDLE, sim_rx.state, "Initial state is IDLE");
    ASSERT_EQ_INT(0, (int)sim_rx.pos, "Initial position is 0");
    ASSERT_TRUE(!sim_rx.frame_ready, "No frame ready initially");
    ASSERT_EQ_UINT(0, sim_stats.bytes_received, "No bytes received initially");
}

/* Test 2: Receiving a sync byte transitions to HEADER state */
static void test_sync_byte_transition(void) {
    sim_reset();

    sim_process_byte(0xAA);

    ASSERT_EQ_INT(FRAME_STATE_HEADER, sim_rx.state, "State transitions to HEADER");
    ASSERT_EQ_INT(1, (int)sim_rx.pos, "Position advances to 1");
    ASSERT_EQ_INT(0xAA, sim_rx.buf[0], "Sync byte stored at position 0");
}

/* Test 3: Non-sync bytes in IDLE state are rejected */
static void test_non_sync_rejected(void) {
    sim_reset();

    sim_process_byte(0x55);
    sim_process_byte(0x01);
    sim_process_byte(0x00);

    ASSERT_EQ_INT(FRAME_STATE_IDLE, sim_rx.state, "State stays IDLE");
    ASSERT_EQ_UINT(3, sim_stats.frames_rejected_sync, "3 sync rejections");
}

/* Test 4: Complete NOP frame (no payload) is assembled correctly */
static void test_complete_nop_frame(void) {
    sim_reset();

    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Feed each byte to the state machine */
    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "State reaches COMPLETE after NOP frame");
    ASSERT_TRUE(sim_rx.frame_ready, "Frame is marked as ready");
    ASSERT_EQ_INT(frame_len, (int)sim_rx.pos, "Position matches frame length");

    /* Verify frame content */
    ASSERT_EQ_INT(CMD_NOP, sim_rx.buf[1], "Command byte is NOP");
    ASSERT_EQ_INT(0, (int)sim_rx.payload_len, "Payload length is 0");
    ASSERT_EQ_UINT(1, sim_stats.frames_validated, "1 frame validated");
}

/* Test 5: Complete frame with payload */
static void test_complete_payload_frame(void) {
    sim_reset();

    uint8_t payload[16];
    for (int i = 0; i < 16; i++)
        payload[i] = (uint8_t)(i * 0x11);

    uint8_t frame[128];
    int frame_len = build_spi_frame(CMD_CC1101_CFG, payload, 16, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build CC1101 config frame");

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "State reaches COMPLETE after payload frame");
    ASSERT_TRUE(sim_rx.frame_ready, "Frame ready");
    ASSERT_EQ_INT(16, (int)sim_rx.payload_len, "Payload length is 16");
    ASSERT_EQ_INT(CMD_CC1101_CFG, sim_rx.buf[1], "Command is CC1101_CFG");

    /* Verify payload content */
    bool payload_match = true;
    for (int i = 0; i < 16; i++) {
        if (sim_rx.buf[SPI_HDR_SIZE + i] != payload[i]) {
            payload_match = false;
            break;
        }
    }
    ASSERT_TRUE(payload_match, "Payload content matches original");
}

/* Test 6: Corrupted header CRC-64 causes error */
static void test_corrupted_header_crc64(void) {
    sim_reset();

    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Corrupt a header byte (but not the sync byte) */
    frame[1] ^= 0xFF;  /* Flip command byte */

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_ERROR, sim_rx.state,
                  "State reaches ERROR on corrupted header");
    ASSERT_EQ_UINT(1, sim_stats.frames_rejected_hdr_crc,
                   "Header CRC rejection counted");
}

/* Test 7: Corrupted payload CRC-32 causes error */
static void test_corrupted_payload_crc32(void) {
    sim_reset();

    uint8_t payload[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t frame[128];
    int frame_len = build_spi_frame(CMD_SDR_TUNE, payload, 8, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build SDR tune frame");

    /* Corrupt a payload byte */
    frame[SPI_HDR_SIZE + 2] ^= 0x01;

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_ERROR, sim_rx.state,
                  "State reaches ERROR on corrupted payload");
    ASSERT_EQ_UINT(1, sim_stats.frames_rejected_pay_crc,
                   "Payload CRC rejection counted");
}

/* Test 8: Error recovery — resync after corrupted frame */
static void test_error_recovery_resync(void) {
    sim_reset();

    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t frame[128];

    /* First: send a corrupted frame */
    int frame_len = build_spi_frame(CMD_ANT_SELECT, payload, 4, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build frame for corruption test");

    /* Corrupt payload */
    frame[SPI_HDR_SIZE + 1] ^= 0x80;

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_ERROR, sim_rx.state, "Error state after corrupted frame");

    /* Now send a valid frame — the state machine should resync on sync byte */
    sim_release_frame();  /* Not needed in ERROR state but for cleanliness */

    uint8_t frame2[128];
    int frame2_len = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame2, sizeof(frame2));
    ASSERT_TRUE(frame2_len > 0, "Build telemetry request frame");

    for (int i = 0; i < frame2_len; i++) {
        sim_process_byte(frame2[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "State recovers to COMPLETE after valid frame");
    ASSERT_EQ_INT(CMD_TELEMETRY_REQ, sim_rx.buf[1],
                  "Recovered frame has correct command");
    ASSERT_EQ_UINT(1, sim_stats.frames_validated,
                   "1 frame validated after recovery");
}

/* Test 9: Back-to-back frames */
static void test_back_to_back_frames(void) {
    sim_reset();

    uint8_t frame1[64];
    uint8_t frame2[64];
    uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    int len1 = build_spi_frame(CMD_NOP, NULL, 0, frame1, sizeof(frame1));
    int len2 = build_spi_frame(CMD_ANT_SELECT, payload, 4, frame2, sizeof(frame2));
    ASSERT_TRUE(len1 > 0, "Build frame 1");
    ASSERT_TRUE(len2 > 0, "Build frame 2");

    /* Send first frame */
    for (int i = 0; i < len1; i++) {
        sim_process_byte(frame1[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state, "First frame complete");
    ASSERT_EQ_INT(CMD_NOP, sim_rx.buf[1], "First frame command = NOP");

    /* Release first frame */
    sim_release_frame();

    /* Send second frame */
    for (int i = 0; i < len2; i++) {
        sim_process_byte(frame2[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state, "Second frame complete");
    ASSERT_EQ_INT(CMD_ANT_SELECT, sim_rx.buf[1], "Second frame command = ANT_SELECT");
    ASSERT_EQ_UINT(2, sim_stats.frames_validated, "2 frames validated total");
}

/* Test 10: Oversized payload length is rejected */
static void test_oversized_payload_rejected(void) {
    sim_reset();

    /* Build a header with payload_len = 4093 (exceeds SPI_MAX_PAYLOAD=4092) */
    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build base frame");

    /* Modify the payload length field to be oversized */
    frame[2] = (uint8_t)(4093 & 0xFF);
    frame[3] = (uint8_t)((4093 >> 8) & 0xFF);

    /* Recompute header CRC-64 */
    uint64_t new_crc = crc64_compute(frame, 8);
    for (int i = 0; i < 8; i++) {
        frame[8 + i] = (uint8_t)(new_crc >> (i * 8));
    }

    for (int i = 0; i < SPI_HDR_SIZE; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_ERROR, sim_rx.state,
                  "Oversized payload length causes error");
    ASSERT_EQ_UINT(1, sim_stats.frames_rejected_len,
                   "Length rejection counted");
}

/* Test 11: Garbage bytes before sync byte are discarded */
static void test_garbage_before_sync(void) {
    sim_reset();

    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Send garbage bytes first */
    sim_process_byte(0x00);
    sim_process_byte(0xFF);
    sim_process_byte(0x55);
    sim_process_byte(0x01);

    ASSERT_EQ_UINT(4, sim_stats.frames_rejected_sync,
                   "4 garbage bytes rejected");

    /* Now send the valid frame */
    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "Frame assembled after garbage");
    ASSERT_EQ_INT(CMD_NOP, sim_rx.buf[1], "Command is NOP");
}

/* Test 12: Maximum payload frame */
static void test_max_payload_frame(void) {
    sim_reset();

    uint8_t *payload = (uint8_t *)malloc(SPI_MAX_PAYLOAD);
    uint8_t *frame = (uint8_t *)malloc(SPI_FRAME_SIZE_MAX + 16);
    ASSERT_TRUE(payload != NULL, "Allocate payload");
    ASSERT_TRUE(frame != NULL, "Allocate frame buffer");

    for (int i = 0; i < SPI_MAX_PAYLOAD; i++)
        payload[i] = (uint8_t)(i & 0xFF);

    int frame_len = build_spi_frame(CMD_SDR_IQ_CHUNK, payload, SPI_MAX_PAYLOAD,
                                     frame, SPI_FRAME_SIZE_MAX);
    ASSERT_EQ_INT(SPI_FRAME_SIZE_MAX, frame_len, "Max payload frame size");

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "Max payload frame assembled correctly");
    ASSERT_EQ_INT(SPI_MAX_PAYLOAD, (int)sim_rx.payload_len,
                  "Max payload length parsed");
    ASSERT_EQ_UINT(1, sim_stats.frames_validated, "1 frame validated");

    free(payload);
    free(frame);
}

/* Test 13: Multiple command types assembled correctly */
static void test_multiple_command_types(void) {
    uint8_t frame[512];

    struct {
        uint8_t cmd;
        uint16_t len;
        const uint8_t *data;
    } test_cmds[] = {
        { CMD_SDR_TUNE, 0, NULL },
        { CMD_SDR_STREAM, 1, (uint8_t[]){0x01} },
        { CMD_TELEMETRY_REQ, 0, NULL },
    };

    int n_cmds = sizeof(test_cmds) / sizeof(test_cmds[0]);

    for (int t = 0; t < n_cmds; t++) {
        sim_reset();

        int frame_len = build_spi_frame(test_cmds[t].cmd, test_cmds[t].data,
                                         test_cmds[t].len, frame, sizeof(frame));
        ASSERT_TRUE(frame_len > 0, "Build frame for command type");

        for (int i = 0; i < frame_len; i++) {
            sim_process_byte(frame[i]);
        }

        ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state, "Frame complete");
        ASSERT_EQ_INT(test_cmds[t].cmd, sim_rx.buf[1], "Command matches");
    }
}

/* Test 14: Byte-by-byte feeding (simulating slow SPI) */
static void test_byte_by_byte_feeding(void) {
    sim_reset();

    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Feed one byte at a time with "gaps" (no processing between bytes) */
    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
        /* After sync byte, we should be in HEADER state */
        if (i == 0) {
            ASSERT_EQ_INT(FRAME_STATE_HEADER, sim_rx.state,
                          "In HEADER state after sync byte");
        }
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "Frame complete after byte-by-byte feeding");
    ASSERT_EQ_INT(frame_len, (int)sim_rx.pos,
                  "Position matches total frame length");
}

/* Test 15: Frame not consumed causes overflow on next byte */
static void test_frame_not_consumed_overflow(void) {
    sim_reset();

    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state, "Frame complete");

    /* Try to send another byte without releasing the frame */
    sim_process_byte(0x00);
    ASSERT_EQ_UINT(1, sim_stats.rx_overflows,
                   "Overflow counted when frame not consumed");
}

/* Test 16: Telemetry request frame (command from host to MCU) */
static void test_telemetry_request_frame(void) {
    sim_reset();

    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build telemetry request frame");

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state, "Telemetry request complete");
    ASSERT_EQ_INT(CMD_TELEMETRY_REQ, sim_rx.buf[1], "Command is TELEMETRY_REQ");
    ASSERT_EQ_INT(0, (int)sim_rx.payload_len, "No payload for telemetry request");
    ASSERT_EQ_UINT(0, sim_stats.frames_rejected_sync, "No sync rejections");
    ASSERT_EQ_UINT(0, sim_stats.frames_rejected_hdr_crc, "No header CRC rejections");
    ASSERT_EQ_UINT(0, sim_stats.frames_rejected_pay_crc, "No payload CRC rejections");
}

/* Test 17: SDR stream start frame (1-byte payload) */
static void test_sdr_stream_start_frame(void) {
    sim_reset();

    uint8_t enable = 1;
    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_SDR_STREAM, &enable, 1, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build SDR stream start frame");

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state, "Stream start complete");
    ASSERT_EQ_INT(CMD_SDR_STREAM, sim_rx.buf[1], "Command is SDR_STREAM");
    ASSERT_EQ_INT(1, (int)sim_rx.payload_len, "Payload length is 1");
    ASSERT_EQ_INT(1, sim_rx.buf[SPI_HDR_SIZE], "Enable flag = 1");
}

/* Test 18: Statistics tracking */
static void test_statistics_tracking(void) {
    sim_reset();

    /* Send 3 valid frames */
    for (int f = 0; f < 3; f++) {
        uint8_t frame[64];
        int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));

        for (int i = 0; i < frame_len; i++) {
            sim_process_byte(frame[i]);
        }
        sim_release_frame();
    }

    ASSERT_EQ_UINT(3, sim_stats.frames_validated, "3 frames validated");

    /* Send some garbage */
    sim_reset_stats_only();
    sim_process_byte(0x00);
    sim_process_byte(0xFF);
    sim_process_byte(0x55);
    ASSERT_EQ_UINT(3, sim_stats.frames_rejected_sync, "3 garbage bytes rejected");
}

/* Test 19: Resync after partial frame + valid frame */
static void test_resync_after_partial(void) {
    sim_reset();

    /* Send partial frame (just sync byte + some header bytes) */
    sim_process_byte(0xAA);  /* Sync */
    sim_process_byte(0x01);  /* Command */
    sim_process_byte(0x08);  /* Length low */
    sim_process_byte(0x00);  /* Length high */

    /* Now send a corrupted CRC byte */
    sim_process_byte(0xFF);  /* Bad reserved byte */
    /* Continue with invalid CRC - this should fail header validation */
    /* Let's just send more random bytes that won't match CRC-64 */
    for (int i = 0; i < 11; i++) {
        sim_process_byte(0x00);
    }

    /* The header CRC will fail, putting us in ERROR state */
    ASSERT_EQ_INT(FRAME_STATE_ERROR, sim_rx.state, "Error after bad header");

    /* Now send a valid frame — should resync on sync byte */
    uint8_t frame[64];
    int frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "Recovered to COMPLETE after resync");
}

/* Test 20: IQ chunk frame (MCU → Host response simulation) */
static void test_iq_chunk_frame(void) {
    sim_reset();

    /* Simulate a 512-byte IQ chunk (like SDR data) */
    uint8_t iq_data[512];
    for (int i = 0; i < 512; i++) {
        iq_data[i] = (uint8_t)(i & 0xFF);
    }

    uint8_t *frame = (uint8_t *)malloc(SPI_FRAME_SIZE_MAX + 16);
    ASSERT_TRUE(frame != NULL, "Allocate IQ frame buffer");

    int frame_len = build_spi_frame(CMD_SDR_IQ_CHUNK, iq_data, 512,
                                     frame, SPI_FRAME_SIZE_MAX);
    ASSERT_TRUE(frame_len > 0, "Build IQ chunk frame");
    ASSERT_EQ_INT(SPI_HDR_SIZE + 512 + SPI_CRC32_SIZE, frame_len,
                  "IQ chunk frame size correct");

    for (int i = 0; i < frame_len; i++) {
        sim_process_byte(frame[i]);
    }

    ASSERT_EQ_INT(FRAME_STATE_COMPLETE, sim_rx.state,
                  "IQ chunk frame complete");
    ASSERT_EQ_INT(512, (int)sim_rx.payload_len, "Payload length = 512");

    /* Verify IQ data integrity */
    bool iq_match = true;
    for (int i = 0; i < 512; i++) {
        if (sim_rx.buf[SPI_HDR_SIZE + i] != (uint8_t)(i & 0xFF)) {
            iq_match = false;
            break;
        }
    }
    ASSERT_TRUE(iq_match, "IQ data content matches");

    free(frame);
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade SPI0 ISR Frame Assembly Unit Tests ===\n\n");

    /* Initialize CRC tables */
    crc64_init_table();
    crc32_init_table();

    RUN_TEST(test_initial_state);
    RUN_TEST(test_sync_byte_transition);
    RUN_TEST(test_non_sync_rejected);
    RUN_TEST(test_complete_nop_frame);
    RUN_TEST(test_complete_payload_frame);
    RUN_TEST(test_corrupted_header_crc64);
    RUN_TEST(test_corrupted_payload_crc32);
    RUN_TEST(test_error_recovery_resync);
    RUN_TEST(test_back_to_back_frames);
    RUN_TEST(test_oversized_payload_rejected);
    RUN_TEST(test_garbage_before_sync);
    RUN_TEST(test_max_payload_frame);
    RUN_TEST(test_multiple_command_types);
    RUN_TEST(test_byte_by_byte_feeding);
    RUN_TEST(test_frame_not_consumed_overflow);
    RUN_TEST(test_telemetry_request_frame);
    RUN_TEST(test_sdr_stream_start_frame);
    RUN_TEST(test_statistics_tracking);
    RUN_TEST(test_resync_after_partial);
    RUN_TEST(test_iq_chunk_frame);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}