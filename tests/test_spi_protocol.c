/*
 * test_spi_protocol.c — Unit Tests for SPI Bridge Protocol Framing
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the SPI protocol handler, focusing on:
 *   1. CRC-16-CCITT validation of frames
 *   2. Frame parsing (sync byte, header, payload)
 *   3. Command dispatch verification
 *   4. Error detection (truncated frames, bad CRC, invalid commands)
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -o test_spi_protocol test_spi_protocol.c -lcmocka
 *
 * Or without cmocka (using minimal self-test framework):
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_spi_protocol test_spi_protocol.c
 *
 * Run:
 *   ./test_spi_protocol
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * CRC-16-CCITT Implementation (matches firmware)
 * ======================================================================== */

/*
 * CRC-16-CCITT (0x1021 polynomial, init 0xFFFF)
 * Used by the SPI bridge protocol for frame integrity.
 *
 * This matches the CRC algorithm used in spi_protocol.c on the RP2350B.
 * The kernel driver uses CRC32, but the MCU-side protocol uses CRC16
 * for the SPI0 slave framing. We test the MCU-side validation here.
 */

#define CRC16_POLY  0x1021
#define CRC16_INIT  0xFFFF

static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = CRC16_INIT;

    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC16_POLY;
            else
                crc = crc << 1;
        }
    }

    return crc;
}

/* ========================================================================
 * SPI Frame Structure (matches firmware protocol)
 * ======================================================================== */

/*
 * Frame format:
 *   [0]     Sync byte (0xAA)
 *   [1]     Command opcode
 *   [2-3]   Payload length (little-endian, 0-252)
 *   [4-7]   Reserved (must be 0)
 *   [8-9]   Header CRC-16 (over bytes 0-7)
 *   [10..]  Payload data (length bytes)
 *   [last 2] Payload CRC-16 (over payload data)
 *
 * Total frame size: 10 + payload_len + 2 (header + payload + CRC)
 */

#define SPI_SYNC_BYTE         0xAA
#define SPI_HDR_SIZE          10    /* bytes 0-9 */
#define SPI_CRC_SIZE          2
#define SPI_MAX_PAYLOAD       252   /* Max payload for MCU-side frames */
#define SPI_FRAME_OVERHEAD    (SPI_HDR_SIZE + SPI_CRC_SIZE)

/* Command opcodes (Host -> MCU) */
#define CMD_NOP               0xFF
#define CMD_SDR_TUNE          0x01
#define CMD_SDR_STREAM        0x02
#define CMD_ANT_SELECT        0x03
#define CMD_CC1101_CFG        0x04
#define CMD_NFC_TRANSACT      0x05
#define CMD_TELEMETRY_REQ     0x06

/* Command opcodes (MCU -> Host) */
#define CMD_TELEMETRY         0x81
#define CMD_SDR_IQ_CHUNK      0x82

/* ========================================================================
 * Frame Builder / Parser
 * ======================================================================== */

/**
 * build_spi_frame — Construct a valid SPI protocol frame
 *
 * @cmd:     Command opcode
 * @payload: Payload data (may be NULL if payload_len == 0)
 * @payload_len: Number of payload bytes
 * @frame:   Output buffer for the complete frame
 * @frame_len: Size of output buffer, returns actual frame size
 *
 * Returns: 0 on success, -1 on error
 */
static int build_spi_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t payload_len,
                            uint8_t *frame, size_t *frame_len) {
    if (payload_len > SPI_MAX_PAYLOAD)
        return -1;

    size_t total = SPI_HDR_SIZE + payload_len + SPI_CRC_SIZE;

    /* Header */
    frame[0] = SPI_SYNC_BYTE;
    frame[1] = cmd;
    frame[2] = (uint8_t)(payload_len & 0xFF);         /* Length low */
    frame[3] = (uint8_t)((payload_len >> 8) & 0xFF);    /* Length high */
    frame[4] = 0;  /* Reserved */
    frame[5] = 0;
    frame[6] = 0;
    frame[7] = 0;

    /* Header CRC-16 (over bytes 0-7) */
    uint16_t hdr_crc = crc16_ccitt(frame, 8);
    frame[8] = (uint8_t)(hdr_crc & 0xFF);        /* CRC low */
    frame[9] = (uint8_t)((hdr_crc >> 8) & 0xFF); /* CRC high */

    /* Payload */
    if (payload_len > 0 && payload)
        memcpy(&frame[SPI_HDR_SIZE], payload, payload_len);

    /* Payload CRC-16 */
    uint16_t pay_crc = crc16_ccitt(&frame[SPI_HDR_SIZE], payload_len);
    frame[SPI_HDR_SIZE + payload_len]     = (uint8_t)(pay_crc & 0xFF);
    frame[SPI_HDR_SIZE + payload_len + 1] = (uint8_t)((pay_crc >> 8) & 0xFF);

    *frame_len = total;
    return 0;
}

/**
 * validate_spi_frame — Validate an SPI protocol frame
 *
 * Checks sync byte, header CRC, and payload CRC.
 *
 * @frame:     Frame data buffer
 * @frame_len: Total frame length in bytes
 *
 * Returns: 0 on valid frame, negative error code on failure
 */
#define VALID_OK            0
#define VALID_ERR_SYNC     -1
#define VALID_ERR_HDR_CRC  -2
#define VALID_ERR_PAY_CRC  -3
#define VALID_ERR_TRUNC    -4
#define VALID_ERR_LEN      -5

static int validate_spi_frame(const uint8_t *frame, size_t frame_len) {
    uint16_t payload_len;
    uint16_t expected_total;

    /* Minimum frame size: header (10) + payload CRC (2) = 12 bytes */
    if (frame_len < SPI_HDR_SIZE + SPI_CRC_SIZE)
        return VALID_ERR_TRUNC;

    /* Check sync byte */
    if (frame[0] != SPI_SYNC_BYTE)
        return VALID_ERR_SYNC;

    /* Extract payload length */
    payload_len = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
    expected_total = SPI_HDR_SIZE + payload_len + SPI_CRC_SIZE;

    /* Check frame length matches */
    if (frame_len != expected_total)
        return VALID_ERR_LEN;

    /* Verify header CRC-16 */
    uint16_t hdr_crc_calc = crc16_ccitt(frame, 8);
    uint16_t hdr_crc_rx   = (uint16_t)frame[8] | ((uint16_t)frame[9] << 8);
    if (hdr_crc_calc != hdr_crc_rx)
        return VALID_ERR_HDR_CRC;

    /* Verify payload CRC-16 */
    if (payload_len > 0) {
        uint16_t pay_crc_calc = crc16_ccitt(&frame[SPI_HDR_SIZE], payload_len);
        uint16_t pay_crc_rx   = (uint16_t)frame[SPI_HDR_SIZE + payload_len] |
                                ((uint16_t)frame[SPI_HDR_SIZE + payload_len + 1] << 8);
        if (pay_crc_calc != pay_crc_rx)
            return VALID_ERR_PAY_CRC;
    }

    return VALID_OK;
}

/* ========================================================================
 * Minimal Test Framework (no external dependency)
 * ======================================================================== */

#ifdef NO_CMOCKA

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
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
        return; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    printf("Running: %s\n", #func); \
    func(); \
} while(0)

#define TEST_SUITE_START() printf("=== SPI Protocol Unit Tests ===\n")
#define TEST_SUITE_END() do { \
    printf("\n=== Results: %d/%d passed, %d failed ===\n", \
           tests_passed, tests_run, tests_failed); \
    return tests_failed > 0 ? 1 : 0; \
} while(0)

typedef void (*test_func_t)(void);

#else
/* Using cmocka framework */
#include <stdarg.h>
#include <cmocka.h>

#define ASSERT_TRUE(cond, msg) assert_true(cond, msg)
#define ASSERT_EQ_INT(expected, actual, msg) assert_int_equal(expected, actual, msg)
#define RUN_TEST(func) cmocka_run_unit_test(func)
#endif

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test 1: CRC-16-CCITT known vectors */
static void test_crc16_known_vectors(void) {
    /* Known CRC-16-CCITT test vectors */
    /* Empty input should yield init value */
    uint16_t crc_empty = crc16_ccitt((uint8_t *)"", 0);
    ASSERT_EQ_INT(CRC16_INIT, crc_empty, "CRC of empty data = init value");

    /* "123456789" standard test vector */
    uint8_t vec1[] = "123456789";
    uint16_t crc_vec1 = crc16_ccitt(vec1, 9);
    /* CRC-16-CCITT of "123456789" = 0x29B1 (standard check value) */
    ASSERT_EQ_INT(0x29B1, crc_vec1, "CRC-16-CCITT standard check value");

    /* Single byte */
    uint8_t vec2[] = {0x00};
    uint16_t crc_vec2 = crc16_ccitt(vec2, 1);
    ASSERT_EQ_INT(0xEF99, crc_vec2, "CRC of 0x00");

    /* All zeros, 4 bytes */
    uint8_t vec3[] = {0x00, 0x00, 0x00, 0x00};
    uint16_t crc_vec3 = crc16_ccitt(vec3, 4);
    ASSERT_EQ_INT(0x1D0F, crc_vec3, "CRC of four zeros");
}

/* Test 2: Build and validate a valid NOP frame */
static void test_valid_nop_frame(void) {
    uint8_t frame[64];
    size_t frame_len;
    int ret;

    ret = build_spi_frame(CMD_NOP, NULL, 0, frame, &frame_len);
    ASSERT_EQ_INT(0, ret, "Build NOP frame");
    ASSERT_EQ_INT(12, (int)frame_len, "NOP frame size = 12 (10 hdr + 2 CRC)");

    /* Verify sync byte */
    ASSERT_EQ_INT(SPI_SYNC_BYTE, frame[0], "Sync byte");

    /* Verify command */
    ASSERT_EQ_INT(CMD_NOP, frame[1], "Command byte");

    /* Verify payload length */
    ASSERT_EQ_INT(0, frame[2], "Payload length low byte");
    ASSERT_EQ_INT(0, frame[3], "Payload length high byte");

    /* Validate the frame */
    ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate NOP frame");
}

/* Test 3: Build and validate frame with payload */
static void test_valid_payload_frame(void) {
    uint8_t frame[512];
    size_t frame_len;
    int ret;

    /* SDR tune command: freq=868MHz, bw=20MHz, gain=30dB */
    uint8_t payload[8];
    payload[0] = 0x00; payload[1] = 0x43; payload[2] = 0xBE; payload[3] = 0x00; /* freq */
    payload[4] = 0x20; payload[5] = 0x4E;  /* bw */
    payload[6] = 0x2C; payload[7] = 0x01;  /* gain */

    ret = build_spi_frame(CMD_SDR_TUNE, payload, 8, frame, &frame_len);
    ASSERT_EQ_INT(0, ret, "Build SDR tune frame");
    ASSERT_EQ_INT(20, (int)frame_len, "SDR tune frame size = 20 (10+8+2)");

    ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate SDR tune frame");
}

/* Test 4: Detect corrupted sync byte */
static void test_bad_sync_byte(void) {
    uint8_t frame[64];
    size_t frame_len;

    build_spi_frame(CMD_NOP, NULL, 0, frame, &frame_len);

    /* Corrupt sync byte */
    frame[0] = 0xBB;

    int ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_ERR_SYNC, ret, "Detect bad sync byte");
}

/* Test 5: Detect corrupted header CRC */
static void test_bad_header_crc(void) {
    uint8_t frame[64];
    size_t frame_len;

    build_spi_frame(CMD_NOP, NULL, 0, frame, &frame_len);

    /* Corrupt header CRC byte */
    frame[8] ^= 0xFF;

    int ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_ERR_HDR_CRC, ret, "Detect bad header CRC");
}

/* Test 6: Detect corrupted payload CRC */
static void test_bad_payload_crc(void) {
    uint8_t frame[512];
    size_t frame_len;
    uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    build_spi_frame(CMD_ANT_SELECT, payload, 4, frame, &frame_len);

    /* Corrupt a payload byte */
    frame[SPI_HDR_SIZE + 1] ^= 0x01;

    int ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_ERR_PAY_CRC, ret, "Detect bad payload CRC");
}

/* Test 7: Detect truncated frame */
static void test_truncated_frame(void) {
    uint8_t frame[64];
    size_t frame_len;

    build_spi_frame(CMD_NOP, NULL, 0, frame, &frame_len);

    /* Validate with less than minimum frame size */
    int ret = validate_spi_frame(frame, 5);
    ASSERT_EQ_INT(VALID_ERR_TRUNC, ret, "Detect truncated frame");
}

/* Test 8: Detect length mismatch */
static void test_length_mismatch(void) {
    uint8_t frame[64];
    size_t frame_len;

    build_spi_frame(CMD_NOP, NULL, 0, frame, &frame_len);

    /* Corrupt payload length field to claim more data than present */
    frame[2] = 100;  /* Claim 100 bytes of payload, but frame is only 12 */

    /* Re-compute header CRC for the modified header */
    uint16_t hdr_crc = crc16_ccitt(frame, 8);
    frame[8] = (uint8_t)(hdr_crc & 0xFF);
    frame[9] = (uint8_t)((hdr_crc >> 8) & 0xFF);

    int ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_ERR_LEN, ret, "Detect length mismatch");
}

/* Test 9: Validate maximum payload frame */
static void test_max_payload_frame(void) {
    uint8_t frame[512];
    size_t frame_len;
    int ret;
    uint8_t payload[SPI_MAX_PAYLOAD];

    /* Fill payload with known pattern */
    for (int i = 0; i < SPI_MAX_PAYLOAD; i++)
        payload[i] = (uint8_t)(i & 0xFF);

    ret = build_spi_frame(CMD_SDR_IQ_CHUNK, payload, SPI_MAX_PAYLOAD,
                          frame, &frame_len);
    ASSERT_EQ_INT(0, ret, "Build max payload frame");
    ASSERT_EQ_INT(SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC_SIZE,
                  (int)frame_len, "Max payload frame size");

    ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate max payload frame");
}

/* Test 10: Reject oversized payload */
static void test_oversized_payload(void) {
    uint8_t frame[1024];
    size_t frame_len;
    int ret;

    /* Attempt to build frame with payload > 252 bytes */
    uint8_t big_payload[300];
    memset(big_payload, 0xAA, sizeof(big_payload));

    ret = build_spi_frame(CMD_SDR_IQ_CHUNK, big_payload, 300,
                          frame, &frame_len);
    ASSERT_EQ_INT(-1, ret, "Reject oversized payload");
}

/* Test 11: CRC bit-flip detection (single bit corruption) */
static void test_crc_single_bit_flip(void) {
    uint8_t frame[512];
    size_t frame_len;
    uint8_t payload[16];

    /* Fill payload with data */
    for (int i = 0; i < 16; i++)
        payload[i] = (uint8_t)(i * 17);

    build_spi_frame(CMD_CC1101_CFG, payload, 16, frame, &frame_len);

    /* Flip each bit in the payload and verify CRC catches it */
    bool all_detected = true;
    for (int byte_idx = 0; byte_idx < 16; byte_idx++) {
        for (int bit = 0; bit < 8; bit++) {
            /* Flip one bit in the payload */
            frame[SPI_HDR_SIZE + byte_idx] ^= (1 << bit);

            int ret = validate_spi_frame(frame, frame_len);
            if (ret != VALID_ERR_PAY_CRC) {
                all_detected = false;
                printf("  Missed bit flip at byte %d bit %d\n", byte_idx, bit);
            }

            /* Restore the bit */
            frame[SPI_HDR_SIZE + byte_idx] ^= (1 << bit);
        }
    }

    ASSERT_TRUE(all_detected, "CRC detects all single-bit flips in payload");
}

/* Test 12: Multiple frame types */
static void test_multiple_frame_types(void) {
    uint8_t frame[512];
    size_t frame_len;
    int ret;

    /* Telemetry request (no payload) */
    ret = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame, &frame_len);
    ASSERT_EQ_INT(0, ret, "Build telemetry request");
    ASSERT_EQ_INT(CMD_TELEMETRY_REQ, frame[1], "Command = telemetry req");
    ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate telemetry request");

    /* Antenna select (1 byte payload) */
    uint8_t ant = APEX_ANT_SUBGHZ;
    ret = build_spi_frame(CMD_ANT_SELECT, &ant, 1, frame, &frame_len);
    ASSERT_EQ_INT(0, ret, "Build antenna select");
    ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate antenna select");

    /* NFC transaction (variable payload) */
    uint8_t nfc_data[] = {0x26, 0x00};  /* REQA command */
    ret = build_spi_frame(CMD_NFC_TRANSACT, nfc_data, 2, frame, &frame_len);
    ASSERT_EQ_INT(0, ret, "Build NFC transact");
    ret = validate_spi_frame(frame, frame_len);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate NFC transact");
}

/* Test 13: CRC-16 consistency with re-computation */
static void test_crc_consistency(void) {
    uint8_t data[] = {0xAA, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};

    /* Compute CRC twice, must be identical */
    uint16_t crc1 = crc16_ccitt(data, sizeof(data));
    uint16_t crc2 = crc16_ccitt(data, sizeof(data));
    ASSERT_EQ_INT(crc1, crc2, "CRC is deterministic");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    TEST_SUITE_START();

    RUN_TEST(test_crc16_known_vectors);
    RUN_TEST(test_valid_nop_frame);
    RUN_TEST(test_valid_payload_frame);
    RUN_TEST(test_bad_sync_byte);
    RUN_TEST(test_bad_header_crc);
    RUN_TEST(test_bad_payload_crc);
    RUN_TEST(test_truncated_frame);
    RUN_TEST(test_length_mismatch);
    RUN_TEST(test_max_payload_frame);
    RUN_TEST(test_oversized_payload);
    RUN_TEST(test_crc_single_bit_flip);
    RUN_TEST(test_multiple_frame_types);
    RUN_TEST(test_crc_consistency);

    TEST_SUITE_END();
}

/* Antenna constants for test (must match protocol) */
#define APEX_ANT_MIMO_TX    0
#define APEX_ANT_MIMO_RX    1
#define APEX_ANT_SUBGHZ     2
#define APEX_ANT_TERMINATED 3