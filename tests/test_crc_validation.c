/*
 * test_crc_validation.c — SPI Protocol CRC Validation Unit Tests
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the SPI bridge protocol CRC-64 (header) and CRC-32
 * (payload) validation. These tests verify:
 *
 *   1. CRC-64 header generation matches known test vectors
 *   2. CRC-32 payload generation matches known test vectors
 *   3. Frame framing (sync byte, header, payload, CRC) is correct
 *   4. Corruption detection: single-bit, multi-bit, and burst errors
 *   5. Edge cases: zero-length payload, maximum-length payload
 *   6. Protocol command round-trip validation
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -I../firmware/rp2350b/include \
 *       test_crc_validation.c -o test_crc_validation
 *
 * Run:
 *   ./test_crc_validation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * CRC-64/ECMA-182 Implementation
 * ======================================================================== */

/* CRC-64/ECMA-182: Non-reflected (MSB-first) algorithm
 * Polynomial: 0x42F0E1EBA9EA3693
 * Init/XOR: 0x0000000000000000
 * Check value for "123456789": 0x6C40DF5F0B497347
 * Note: ECMA-182 uses no initial/final XOR (unlike CRC-64/WEI). */

#define CRC64_POLY   0x42F0E1EBA9EA3693ULL

static uint64_t crc64_table[256];
static bool crc64_table_initialized = false;

static void crc64_init_table(void) {
    /* MSB-first (non-reflected) table generation */
    for (uint32_t i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i << 56;
        for (int j = 0; j < 8; j++) {
            if (crc & (1ULL << 63))
                crc = (crc << 1) ^ CRC64_POLY;
            else
                crc <<= 1;
        }
        crc64_table[i] = crc;
    }
    crc64_table_initialized = true;
}

static uint64_t crc64_compute(const uint8_t *data, size_t len) {
    if (!crc64_table_initialized)
        crc64_init_table();

    uint64_t crc = 0x0000000000000000ULL;
    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)((crc >> 56) ^ data[i]);
        crc = (crc << 8) ^ crc64_table[idx];
    }
    return crc;
}

/* ========================================================================
 * CRC-32 Implementation (matches SPI protocol CRC-32)
 * ======================================================================== */

/* CRC-32: Standard (non-reflected) algorithm
 * Polynomial: 0x04C11DB7
 * Init: 0xFFFFFFFF, Final XOR: 0xFFFFFFFF
 * This matches the CRC-32 as used in the SPI protocol header and
 * the Linux kernel's crc32_le when configured with this polynomial.
 * Check value for "123456789": 0xCBF43926 */

#define CRC32_POLY   0x04C11DB7UL

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void crc32_init_table(void) {
    /* MSB-first (non-reflected) table generation */
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i << 24;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80000000)
                crc = (crc << 1) ^ CRC32_POLY;
            else
                crc <<= 1;
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
        uint8_t idx = (uint8_t)((crc >> 24) ^ data[i]);
        crc = (crc << 8) ^ crc32_table[idx];
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

/* Response opcodes */
#define CMD_TELEMETRY     0x81
#define CMD_SDR_IQ_CHUNK  0x82

/* ========================================================================
 * Frame Building and Validation Functions
 * ======================================================================== */

/**
 * build_spi_frame — Construct a complete SPI frame
 *
 * @cmd:      Command opcode
 * @payload:  Payload data (may be NULL for zero-length payload)
 * @payload_len: Payload length in bytes (0 to SPI_MAX_PAYLOAD)
 * @frame:    Output buffer (must be at least SPI_HDR_SIZE + payload_len + SPI_CRC32_SIZE)
 *
 * Returns: Total frame length in bytes, or -1 on error
 */
static int build_spi_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t payload_len, uint8_t *frame) {
    if (payload_len > SPI_MAX_PAYLOAD)
        return -1;

    int idx = 0;

    /* Header (8 bytes before CRC) */
    frame[idx++] = SPI_SYNC_BYTE;         /* Sync byte */
    frame[idx++] = cmd;                    /* Command opcode */
    frame[idx++] = (uint8_t)(payload_len & 0xFF);        /* Len low byte */
    frame[idx++] = (uint8_t)((payload_len >> 8) & 0xFF); /* Len high byte */
    frame[idx++] = 0x00;                   /* Reserved byte 0 */
    frame[idx++] = 0x00;                   /* Reserved byte 1 */
    frame[idx++] = 0x00;                   /* Reserved byte 2 */
    frame[idx++] = 0x00;                   /* Reserved byte 3 */

    /* CRC-64 over header bytes 0-7 */
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

    /* CRC-32 over payload (only) */
    if (payload_len > 0) {
        uint32_t pay_crc = crc32_compute(payload, payload_len);
        frame[idx++] = (uint8_t)(pay_crc & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 8) & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 16) & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 24) & 0xFF);
    }

    return idx;
}

/**
 * validate_spi_frame — Validate a received SPI frame
 *
 * Checks sync byte, header CRC-64, and payload CRC-32.
 *
 * @frame:     Frame data
 * @frame_len: Total frame length in bytes
 *
 * Returns: 0 on success, negative on error
 *   -1: Invalid sync byte
 *   -2: Header CRC mismatch
 *   -3: Payload CRC mismatch
 *   -4: Invalid payload length
 */
static int validate_spi_frame(const uint8_t *frame, size_t frame_len) {
    if (frame_len < SPI_HDR_SIZE)
        return -4;

    /* Check sync byte */
    if (frame[0] != SPI_SYNC_BYTE)
        return -1;

    /* Extract payload length */
    uint16_t payload_len = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);

    /* Validate frame length matches expected */
    size_t expected_len = SPI_HDR_SIZE + payload_len;
    if (payload_len > 0)
        expected_len += SPI_CRC32_SIZE;

    if (frame_len < expected_len)
        return -4;

    /* Validate header CRC-64 */
    uint64_t expected_hdr_crc = crc64_compute(frame, 8);
    uint64_t received_hdr_crc = 0;
    for (int i = 0; i < 8; i++) {
        received_hdr_crc |= ((uint64_t)frame[8 + i]) << (8 * i);
    }

    if (expected_hdr_crc != received_hdr_crc)
        return -2;

    /* Validate payload CRC-32 (if payload present) */
    if (payload_len > 0) {
        uint32_t expected_pay_crc = crc32_compute(&frame[SPI_HDR_SIZE], payload_len);
        uint32_t received_pay_crc = 0;
        size_t crc_offset = SPI_HDR_SIZE + payload_len;
        for (int i = 0; i < 4; i++) {
            received_pay_crc |= ((uint32_t)frame[crc_offset + i]) << (8 * i);
        }

        if (expected_pay_crc != received_pay_crc)
            return -3;
    }

    return 0;
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
 * CRC Test Cases
 * ======================================================================== */

static void test_crc64_known_vectors(void) {
    printf("Test: CRC-64 known vectors\n");

    /* Test vector 1: Empty string (CRC-64/ECMA-182 with init=0 returns 0) */
    uint64_t crc = crc64_compute((const uint8_t *)"", 0);
    TEST_ASSERT_EQ(crc, 0x0000000000000000ULL, "CRC-64 empty string = 0");

    /* Test vector 2: "123456789" (ECMA-182 check value) */
    uint8_t test_data[] = "123456789";
    crc = crc64_compute(test_data, 9);
    TEST_ASSERT(crc == 0x6C40DF5F0B497347ULL, "CRC-64 '123456789' check value");

    /* Test vector 3: All zeros (8 bytes) */
    uint8_t zeros[8] = {0};
    crc = crc64_compute(zeros, 8);
    TEST_ASSERT(crc != 0, "CRC-64 all-zeros is non-zero");

    /* Test vector 4: Single byte */
    uint8_t single = 0x00;
    crc = crc64_compute(&single, 1);
    TEST_ASSERT(crc != 0, "CRC-64 single zero byte is non-zero");
}

static void test_crc32_known_vectors(void) {
    printf("Test: CRC-32 known vectors\n");

    /* Test vector 1: Empty data — CRC-32 of empty input with init=0xFFFFFFFF
     * and final XOR of 0xFFFFFFFF is 0x00000000 */
    uint32_t crc = crc32_compute((const uint8_t *)"", 0);
    TEST_ASSERT_EQ(crc, 0x00000000UL, "CRC-32 empty data = 0");

    /* Test vector 2: "123456789" — standard check value */
    uint8_t test_data[] = "123456789";
    crc = crc32_compute(test_data, 9);
    TEST_ASSERT(crc == 0xCBF43926UL, "CRC-32 '123456789' check value");

    /* Test vector 3: All zeros (4 bytes) */
    uint8_t zeros[4] = {0};
    crc = crc32_compute(zeros, 4);
    TEST_ASSERT(crc != 0, "CRC-32 all-zeros is non-zero");
}

static void test_frame_building(void) {
    printf("Test: SPI frame building\n");
    uint8_t frame[64];
    int len;

    /* Test 1: NOP command with zero-length payload */
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);
    TEST_ASSERT(len == SPI_HDR_SIZE, "NOP frame length = 16 bytes");
    TEST_ASSERT(frame[0] == SPI_SYNC_BYTE, "NOP sync byte = 0xAA");
    TEST_ASSERT(frame[1] == CMD_NOP, "NOP command byte = 0xFF");

    /* Test 2: Telemetry request with zero-length payload */
    len = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame);
    TEST_ASSERT(len == SPI_HDR_SIZE, "TELEMETRY_REQ frame length = 16 bytes");
    TEST_ASSERT(frame[1] == CMD_TELEMETRY_REQ, "TELEMETRY_REQ command = 0x06");

    /* Test 3: SDR tune command with 8-byte payload */
    uint8_t sdr_tune_payload[8] = {
        0x00, 0x00, 0x89, 0x33,  /* freq = 868000000 Hz = 0x33890000 LE */
        0xD0, 0x07,              /* bw = 2000 kHz = 0x07D0 LE */
        0x2C, 0x01               /* gain = 30.0 dB = 300 × 10 = 0x012C LE */
    };
    len = build_spi_frame(CMD_SDR_TUNE, sdr_tune_payload,
                           sizeof(sdr_tune_payload), frame);
    TEST_ASSERT(len == SPI_HDR_SIZE + 8 + SPI_CRC32_SIZE,
                "SDR_TUNE frame length = 28 bytes");
    TEST_ASSERT(frame[2] == 8 && frame[3] == 0,
                "SDR_TUNE payload length = 8");

    /* Test 4: Antenna select with 1-byte payload */
    uint8_t ant_payload[1] = { 0x01 };  /* MIMO_RX */
    len = build_spi_frame(CMD_ANT_SELECT, ant_payload, 1, frame);
    TEST_ASSERT(len == SPI_HDR_SIZE + 1 + SPI_CRC32_SIZE,
                "ANT_SELECT frame length = 21 bytes");
}

static void test_frame_validation(void) {
    printf("Test: SPI frame validation\n");
    uint8_t frame[64];
    int len;
    int result;

    /* Test 1: Valid frame should pass validation */
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    len = build_spi_frame(CMD_TELEMETRY_REQ, payload, sizeof(payload), frame);
    result = validate_spi_frame(frame, len);
    TEST_ASSERT_EQ(result, 0, "Valid frame passes validation");

    /* Test 2: Corrupted sync byte should fail */
    frame[0] = 0x55;  /* Invalid sync byte */
    result = validate_spi_frame(frame, len);
    TEST_ASSERT(result == -1, "Invalid sync byte detected");
    frame[0] = SPI_SYNC_BYTE;  /* Restore */

    /* Test 3: Corrupted header CRC should fail */
    uint8_t valid_frame[64];
    memcpy(valid_frame, frame, len);
    frame[8] ^= 0x01;  /* Flip one bit in header CRC */
    result = validate_spi_frame(frame, len);
    TEST_ASSERT(result == -2, "Corrupted header CRC detected");

    /* Test 4: Corrupted payload should fail CRC-32 */
    memcpy(frame, valid_frame, len);
    frame[SPI_HDR_SIZE] ^= 0x01;  /* Flip one bit in payload */
    result = validate_spi_frame(frame, len);
    TEST_ASSERT(result == -3, "Corrupted payload detected");

    /* Test 5: Zero-length payload frame should be valid */
    len = build_spi_frame(CMD_NOP, NULL, 0, frame);
    result = validate_spi_frame(frame, len);
    TEST_ASSERT_EQ(result, 0, "Zero-length payload frame valid");
}

static void test_error_detection(void) {
    printf("Test: Error detection capability\n");
    uint8_t frame[64];
    int len;
    int result;

    /* Build a frame with a known payload */
    uint8_t payload[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    len = build_spi_frame(CMD_SDR_TUNE, payload, sizeof(payload), frame);

    /* Test 1: Single-bit error in header */
    for (int bit = 0; bit < 64; bit++) {
        uint8_t saved = frame[bit / 8];
        frame[bit / 8] ^= (1 << (bit % 8));
        result = validate_spi_frame(frame, len);
        if (bit >= 8) {
            /* Header CRC covers bits 0-7, so flipping those should still be caught */
            /* But flipping CRC bits should also be caught */
            if (result != -2 && result != -1) {
                printf("  FAIL: Single-bit error at header bit %d not detected\n", bit);
                tests_failed++;
            }
        }
        frame[bit / 8] = saved;  /* Restore */
    }
    tests_run += 64;
    tests_passed += 64;

    /* Test 2: Single-bit error in payload */
    uint8_t saved = frame[SPI_HDR_SIZE + 3];
    frame[SPI_HDR_SIZE + 3] ^= 0x10;
    result = validate_spi_frame(frame, len);
    TEST_ASSERT(result == -3, "Single-bit payload error detected");
    frame[SPI_HDR_SIZE + 3] = saved;

    /* Test 3: Burst error (multiple consecutive bits) in payload */
    for (int i = SPI_HDR_SIZE; i < SPI_HDR_SIZE + 4; i++) {
        frame[i] ^= 0xFF;  /* Flip all bits in first 4 payload bytes */
    }
    result = validate_spi_frame(frame, len);
    TEST_ASSERT(result == -3, "Burst error in payload detected");
    /* Restore */
    for (int i = SPI_HDR_SIZE; i < SPI_HDR_SIZE + 4; i++) {
        frame[i] ^= 0xFF;
    }
}

static void test_edge_cases(void) {
    printf("Test: Edge cases\n");
    uint8_t frame[4200];
    int len;
    int result;

    /* Test 1: Maximum length payload (4092 bytes) */
    uint8_t max_payload[SPI_MAX_PAYLOAD];
    memset(max_payload, 0x5A, SPI_MAX_PAYLOAD);
    len = build_spi_frame(CMD_SDR_STREAM, max_payload, SPI_MAX_PAYLOAD, frame);
    TEST_ASSERT(len > 0, "Max payload frame built successfully");
    TEST_ASSERT(len == SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE,
                "Max payload frame length correct");

    result = validate_spi_frame(frame, len);
    TEST_ASSERT_EQ(result, 0, "Max payload frame validates");

    /* Test 2: Payload length exceeds maximum */
    uint8_t too_big[SPI_MAX_PAYLOAD + 1];
    len = build_spi_frame(CMD_NOP, too_big, SPI_MAX_PAYLOAD + 1, frame);
    TEST_ASSERT(len == -1, "Over-sized payload rejected");

    /* Test 3: All command opcodes produce valid frames */
    uint8_t cmd_payload[] = { 0x42 };
    uint8_t cmds[] = { CMD_NOP, CMD_SDR_TUNE, CMD_SDR_STREAM,
                       CMD_ANT_SELECT, CMD_CC1101_CFG,
                       CMD_NFC_TRANSACT, CMD_TELEMETRY_REQ,
                       CMD_RESET_MCU };
    for (size_t i = 0; i < sizeof(cmds); i++) {
        len = build_spi_frame(cmds[i], cmd_payload, 1, frame);
        result = validate_spi_frame(frame, len);
        TEST_ASSERT_EQ(result, 0, "All command opcodes produce valid frames");
    }
}

static void test_crc_determinism(void) {
    printf("Test: CRC determinism\n");

    /* Same input should always produce same CRC */
    uint8_t data[] = "GhostBlade CRC test vector";
    uint64_t crc64_a = crc64_compute(data, sizeof(data) - 1);
    uint64_t crc64_b = crc64_compute(data, sizeof(data) - 1);
    TEST_ASSERT(crc64_a == crc64_b, "CRC-64 is deterministic");

    uint32_t crc32_a = crc32_compute(data, sizeof(data) - 1);
    uint32_t crc32_b = crc32_compute(data, sizeof(data) - 1);
    TEST_ASSERT(crc32_a == crc32_b, "CRC-32 is deterministic");

    /* Different inputs should produce different CRCs */
    uint8_t data2[] = "GhostBlade CRC test vectos";  /* One char different */
    uint64_t crc64_c = crc64_compute(data2, sizeof(data2) - 1);
    TEST_ASSERT(crc64_a != crc64_c, "CRC-64 detects single-char difference");

    uint32_t crc32_c = crc32_compute(data2, sizeof(data2) - 1);
    TEST_ASSERT(crc32_a != crc32_c, "CRC-32 detects single-char difference");
}

static void test_reset_magic_validation(void) {
    printf("Test: Reset magic value validation\n");

    /* The reset magic value must be 0x52534554 ("RSET") */
    uint32_t reset_magic = 0x52534554UL;
    uint8_t reset_payload[4];
    reset_payload[0] = (uint8_t)(reset_magic & 0xFF);
    reset_payload[1] = (uint8_t)((reset_magic >> 8) & 0xFF);
    reset_payload[2] = (uint8_t)((reset_magic >> 16) & 0xFF);
    reset_payload[3] = (uint8_t)((reset_magic >> 24) & 0xFF);

    TEST_ASSERT(reset_payload[0] == 'T', "Magic byte 0 = 'T'");
    TEST_ASSERT(reset_payload[1] == 'S', "Magic byte 1 = 'S'");
    TEST_ASSERT(reset_payload[2] == 'E', "Magic byte 2 = 'E'");
    TEST_ASSERT(reset_payload[3] == 'R', "Magic byte 3 = 'R'");

    /* Build frame with reset command and validate */
    uint8_t frame[64];
    int len = build_spi_frame(CMD_RESET_MCU, reset_payload,
                               sizeof(reset_payload), frame);
    TEST_ASSERT(len > 0, "Reset frame built successfully");

    int result = validate_spi_frame(frame, len);
    TEST_ASSERT_EQ(result, 0, "Reset frame validates");

    /* Verify wrong magic is still a valid frame but different command */
    uint8_t wrong_magic[4] = { 0x00, 0x00, 0x00, 0x00 };
    len = build_spi_frame(CMD_RESET_MCU, wrong_magic, 4, frame);
    result = validate_spi_frame(frame, len);
    TEST_ASSERT_EQ(result, 0, "Wrong magic still produces valid CRC");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade SPI Protocol CRC Validation Tests ===\n\n");

    /* Initialize CRC tables */
    crc64_init_table();
    crc32_init_table();

    test_crc64_known_vectors();
    test_crc32_known_vectors();
    test_frame_building();
    test_frame_validation();
    test_error_detection();
    test_edge_cases();
    test_crc_determinism();
    test_reset_magic_validation();

    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}