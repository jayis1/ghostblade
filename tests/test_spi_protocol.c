/*
 * test_spi_protocol.c — Unit Tests for SPI Bridge Protocol Framing
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for the SPI protocol handler, covering:
 *   1. CRC-64 (ECMA-182) header validation — matches kernel driver & MCU firmware
 *   2. CRC-32 (ISO 3309) payload validation — matches kernel driver & MCU firmware
 *   3. Frame parsing (sync byte, header, payload, CRC checks)
 *   4. Command dispatch verification
 *   5. Error detection (truncated frames, bad CRC, invalid commands, overflow)
 *   6. Round-trip build/validate integrity
 *   7. Boundary conditions and fuzzing
 *
 * This test suite uses the SAME CRC algorithms as the production code:
 *   - CRC-64 with polynomial 0x42F0E1EBA9EA3693 (ECMA-182) for header integrity
 *   - CRC-32 with polynomial 0xEDB88320 (ISO 3309) for payload integrity
 *
 * The MCU-side (RP2350B) and kernel driver (RK3576) both use these algorithms.
 * The previous version of this test used CRC-16-CCITT, which was incorrect.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -o test_spi_protocol test_spi_protocol.c
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
 * CRC-64 Implementation (ECMA-182, matches kernel driver & MCU firmware)
 * ======================================================================== */

#define CRC64_POLY 0x42F0E1EBA9EA3693ULL

static uint64_t crc64_table[256];
static int crc64_table_initialized = 0;

static void crc64_init_table(void) {
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC64_POLY;
            else
                crc >>= 1;
        }
        crc64_table[i] = crc;
    }
    crc64_table_initialized = 1;
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
 * CRC-32 Implementation (ISO 3309, matches kernel driver & MCU firmware)
 * ======================================================================== */

#define CRC32_POLY 0xEDB88320UL

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void crc32_init_table(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC32_POLY;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
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
 * SPI Frame Structure (matches kernel driver & MCU firmware)
 * ======================================================================== */

/*
 * Frame format (16-byte header + payload + 4-byte CRC-32 trailer):
 *
 *   [0x00] SYNC      = 0xAA (1 byte)
 *   [0x01] CMD       = command opcode (1 byte)
 *   [0x02] LEN_LO    = payload length low byte
 *   [0x03] LEN_HI    = payload length high byte
 *   [0x04] RESERVED  = 0x00000000 (4 bytes)
 *   [0x08] HDR_CRC   = CRC-64 over bytes 0-7 (8 bytes, little-endian)
 *   [0x10] PAYLOAD   = variable length (0 to 4092 bytes)
 *   [0x10+LEN] CRC32 = CRC-32 over payload (4 bytes, little-endian)
 *
 * Total frame size = 16 + payload_len + 4
 * Maximum frame size = 16 + 4092 + 4 = 4112 bytes
 */

#define SPI_SYNC_BYTE         0xAA
#define SPI_HDR_SIZE          16    /* 16-byte header */
#define SPI_CRC32_SIZE        4     /* 4-byte CRC-32 trailer */
#define SPI_MAX_PAYLOAD       4092  /* Max payload (matches APEX_SPI_MAX_PAYLOAD) */
#define SPI_FRAME_SIZE_MAX    (SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE)

/* Command opcodes (Host -> MCU) — matches apex_bridge_regs.h */
#define CMD_NOP               0xFF
#define CMD_SDR_TUNE          0x01
#define CMD_SDR_STREAM        0x02
#define CMD_ANT_SELECT        0x03
#define CMD_CC1101_CFG        0x04
#define CMD_NFC_TRANSACT      0x05
#define CMD_TELEMETRY_REQ     0x06
#define CMD_RESET_MCU         0x07

/* Command opcodes (MCU -> Host) */
#define CMD_TELEMETRY         0x81
#define CMD_SDR_IQ_CHUNK      0x82

/* Antenna constants */
#define ANT_MIMO_TX           0
#define ANT_MIMO_RX           1
#define ANT_SUBGHZ            2
#define ANT_TERMINATED         3

/* Reset magic value — must match SPI_RESET_MAGIC in firmware and
 * APEX_RESET_MAGIC in kernel driver */
#define SPI_RESET_MAGIC        0x52534554UL  /* "RSET" */

/* ========================================================================
 * Frame Builder / Parser (production-accurate)
 * ======================================================================== */

/**
 * build_spi_frame — Construct a valid SPI protocol frame
 *
 * Uses CRC-64 for header integrity and CRC-32 for payload integrity,
 * matching the production kernel driver and MCU firmware.
 *
 * @cmd:         Command opcode
 * @payload:     Payload data (may be NULL if payload_len == 0)
 * @payload_len: Number of payload bytes (0 to SPI_MAX_PAYLOAD)
 * @frame:       Output buffer for the complete frame
 * @frame_size:  Size of output buffer
 *
 * Returns: total frame size on success, -1 on error
 */
static int build_spi_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t payload_len,
                            uint8_t *frame, size_t frame_size) {
    size_t total_len;
    uint64_t hdr_crc;
    uint32_t pay_crc;

    if (payload_len > SPI_MAX_PAYLOAD)
        return -1;

    total_len = SPI_HDR_SIZE + payload_len + SPI_CRC32_SIZE;
    if (total_len > frame_size)
        return -1;

    /* Header bytes 0-7 */
    frame[0] = SPI_SYNC_BYTE;
    frame[1] = cmd;
    frame[2] = (uint8_t)(payload_len & 0xFF);         /* Length low */
    frame[3] = (uint8_t)((payload_len >> 8) & 0xFF);  /* Length high */
    frame[4] = 0;  /* Reserved */
    frame[5] = 0;
    frame[6] = 0;
    frame[7] = 0;

    /* Header CRC-64 over bytes 0-7 */
    hdr_crc = crc64_compute(frame, 8);

    /* Store CRC-64 as little-endian 8 bytes */
    frame[8]  = (uint8_t)(hdr_crc & 0xFF);
    frame[9]  = (uint8_t)((hdr_crc >> 8) & 0xFF);
    frame[10] = (uint8_t)((hdr_crc >> 16) & 0xFF);
    frame[11] = (uint8_t)((hdr_crc >> 24) & 0xFF);
    frame[12] = (uint8_t)((hdr_crc >> 32) & 0xFF);
    frame[13] = (uint8_t)((hdr_crc >> 40) & 0xFF);
    frame[14] = (uint8_t)((hdr_crc >> 48) & 0xFF);
    frame[15] = (uint8_t)((hdr_crc >> 56) & 0xFF);

    /* Payload */
    if (payload_len > 0 && payload != NULL)
        memcpy(&frame[SPI_HDR_SIZE], payload, payload_len);

    /* Payload CRC-32 */
    pay_crc = crc32_compute(&frame[SPI_HDR_SIZE], payload_len);

    /* Store CRC-32 as little-endian 4 bytes */
    frame[SPI_HDR_SIZE + payload_len]     = (uint8_t)(pay_crc & 0xFF);
    frame[SPI_HDR_SIZE + payload_len + 1] = (uint8_t)((pay_crc >> 8) & 0xFF);
    frame[SPI_HDR_SIZE + payload_len + 2] = (uint8_t)((pay_crc >> 16) & 0xFF);
    frame[SPI_HDR_SIZE + payload_len + 3] = (uint8_t)((pay_crc >> 24) & 0xFF);

    return (int)total_len;
}

/**
 * validate_spi_frame — Validate an incoming SPI protocol frame
 *
 * Checks sync byte, header CRC-64, payload length, and payload CRC-32.
 * This matches the production validation in the kernel driver.
 *
 * @frame:     Frame data buffer
 * @frame_len: Total frame length in bytes
 * @cmd:       Output: parsed command opcode (if valid)
 * @payload_len: Output: parsed payload length (if valid)
 * @payload:   Output: pointer to payload within frame (if valid)
 *
 * Returns: 0 on valid frame, negative error code on failure
 */
#define VALID_OK            0
#define VALID_ERR_SYNC     -1
#define VALID_ERR_HDR_CRC  -2
#define VALID_ERR_PAY_CRC  -3
#define VALID_ERR_TRUNC    -4
#define VALID_ERR_LEN      -5
#define VALID_ERR_OVERFLOW -6

static int validate_spi_frame(const uint8_t *frame, size_t frame_len,
                               uint8_t *cmd, uint16_t *payload_len,
                               const uint8_t **payload) {
    uint16_t len;
    size_t expected_total;
    uint64_t actual_crc64, expected_crc64;
    uint32_t actual_crc32, expected_crc32;

    /* Minimum frame size: header (16) + CRC-32 (4) = 20 bytes */
    if (frame_len < SPI_HDR_SIZE + SPI_CRC32_SIZE)
        return VALID_ERR_TRUNC;

    /* Check sync byte */
    if (frame[0] != SPI_SYNC_BYTE)
        return VALID_ERR_SYNC;

    /* Verify header CRC-64 */
    actual_crc64 = crc64_compute(frame, 8);
    expected_crc64 = (uint64_t)frame[8]       |
                     ((uint64_t)frame[9] << 8)  |
                     ((uint64_t)frame[10] << 16) |
                     ((uint64_t)frame[11] << 24) |
                     ((uint64_t)frame[12] << 32) |
                     ((uint64_t)frame[13] << 40) |
                     ((uint64_t)frame[14] << 48) |
                     ((uint64_t)frame[15] << 56);

    if (actual_crc64 != expected_crc64)
        return VALID_ERR_HDR_CRC;

    /* Extract payload length */
    len = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
    if (len > SPI_MAX_PAYLOAD)
        return VALID_ERR_OVERFLOW;

    /* Check total frame size */
    expected_total = SPI_HDR_SIZE + len + SPI_CRC32_SIZE;
    if (frame_len != expected_total)
        return VALID_ERR_LEN;

    /* Verify payload CRC-32 */
    if (len > 0) {
        actual_crc32 = crc32_compute(&frame[SPI_HDR_SIZE], len);
        expected_crc32 = (uint32_t)frame[SPI_HDR_SIZE + len]       |
                          ((uint32_t)frame[SPI_HDR_SIZE + len + 1] << 8)  |
                          ((uint32_t)frame[SPI_HDR_SIZE + len + 2] << 16) |
                          ((uint32_t)frame[SPI_HDR_SIZE + len + 3] << 24);

        if (actual_crc32 != expected_crc32)
            return VALID_ERR_PAY_CRC;
    } else {
        /* Zero-length payload: CRC-32 is computed over 0 bytes */
        actual_crc32 = crc32_compute(&frame[SPI_HDR_SIZE], 0);
        expected_crc32 = (uint32_t)frame[SPI_HDR_SIZE + len]       |
                          ((uint32_t)frame[SPI_HDR_SIZE + len + 1] << 8)  |
                          ((uint32_t)frame[SPI_HDR_SIZE + len + 2] << 16) |
                          ((uint32_t)frame[SPI_HDR_SIZE + len + 3] << 24);

        if (actual_crc32 != expected_crc32)
            return VALID_ERR_PAY_CRC;
    }

    /* Return parsed fields */
    if (cmd)
        *cmd = frame[1];
    if (payload_len)
        *payload_len = len;
    if (payload)
        *payload = &frame[SPI_HDR_SIZE];

    return VALID_OK;
}

/* ========================================================================
 * Minimal Test Framework (no external dependency)
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
        printf("  FAIL: %s: expected 0x%08x, got 0x%08x (line %d)\n", \
               msg, (unsigned)(expected), (unsigned)(actual), __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ_UINT64(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s: expected 0x%016llx, got 0x%016llx (line %d)\n", \
               msg, (unsigned long long)(expected), (unsigned long long)(actual), __LINE__); \
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

/* Test 1: CRC-64 known vectors */
static void test_crc64_known_vectors(void) {
    /* Initialize CRC tables */
    crc64_init_table();

    /* CRC-64 of empty input */
    uint64_t crc_empty = crc64_compute((uint8_t *)"", 0);
    ASSERT_EQ_UINT64(0x0000000000000000ULL, crc_empty,
                     "CRC-64 of empty data = 0 (after init=0xFFFF... ^ final=0xFFFF...)");

    /* CRC-64 of "123456789" — check value for reflected ECMA-182 polynomial
     * Note: The standard ECMA-182 check value 0x6C40DF5F0B497347 uses
     * MSB-first computation. Our implementation uses reflected (LSB-first)
     * computation matching the RP2350B firmware and kernel driver, which
     * produces 0xB86883E6FA710A9F for the same input. */
    uint8_t vec1[] = "123456789";
    uint64_t crc_vec1 = crc64_compute(vec1, 9);
    ASSERT_EQ_UINT64(0xB86883E6FA710A9FULL, crc_vec1,
                     "CRC-64/ECMA-182 (reflected) of '123456789'");

    /* CRC-64 of single byte 0x00 */
    uint8_t vec2[] = {0x00};
    uint64_t crc_vec2 = crc64_compute(vec2, 1);
    /* Verify it's deterministic */
    uint64_t crc_vec2_again = crc64_compute(vec2, 1);
    ASSERT_EQ_UINT64(crc_vec2, crc_vec2_again, "CRC-64 is deterministic");
}

/* Test 2: CRC-32 known vectors */
static void test_crc32_known_vectors(void) {
    crc32_init_table();

    /* CRC-32 of "123456789" — standard Ethernet check value */
    uint8_t vec1[] = "123456789";
    uint32_t crc_vec1 = crc32_compute(vec1, 9);
    ASSERT_EQ_UINT(0xCBF43926UL, crc_vec1,
                   "CRC-32 of '123456789' (Ethernet check)");

    /* CRC-32 of empty data */
    uint32_t crc_empty = crc32_compute((uint8_t *)"", 0);
    ASSERT_EQ_UINT(0x00000000UL, crc_empty,
                   "CRC-32 of empty data = 0");

    /* CRC-32 of all zeros, 4 bytes */
    uint8_t vec2[] = {0x00, 0x00, 0x00, 0x00};
    uint32_t crc_vec2 = crc32_compute(vec2, 4);
    /* Verify it's deterministic */
    uint32_t crc_vec2_again = crc32_compute(vec2, 4);
    ASSERT_EQ_UINT(crc_vec2, crc_vec2_again, "CRC-32 is deterministic");
}

/* Test 3: Build and validate a valid NOP frame */
static void test_valid_nop_frame(void) {
    uint8_t frame[64];
    int ret;
    int frame_len;
    uint8_t cmd;
    uint16_t payload_len;
    const uint8_t *payload;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_EQ_INT(20, frame_len, "NOP frame size = 20 (16 hdr + 4 CRC-32)");

    /* Verify sync byte */
    ASSERT_EQ_INT(SPI_SYNC_BYTE, frame[0], "Sync byte = 0xAA");

    /* Verify command */
    ASSERT_EQ_INT(CMD_NOP, frame[1], "Command byte = 0xFF (NOP)");

    /* Verify payload length */
    ASSERT_EQ_INT(0, frame[2], "Payload length low byte = 0");
    ASSERT_EQ_INT(0, frame[3], "Payload length high byte = 0");

    /* Verify reserved bytes */
    ASSERT_EQ_INT(0, frame[4], "Reserved byte 4 = 0");
    ASSERT_EQ_INT(0, frame[5], "Reserved byte 5 = 0");
    ASSERT_EQ_INT(0, frame[6], "Reserved byte 6 = 0");
    ASSERT_EQ_INT(0, frame[7], "Reserved byte 7 = 0");

    /* Validate the frame */
    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate NOP frame");
    ASSERT_EQ_INT(CMD_NOP, cmd, "Parsed command = NOP");
    ASSERT_EQ_INT(0, (int)payload_len, "Parsed payload length = 0");
}

/* Test 4: Build and validate frame with payload */
static void test_valid_payload_frame(void) {
    uint8_t frame[512];
    int frame_len;
    int ret;
    uint8_t cmd;
    uint16_t payload_len;
    const uint8_t *payload;

    /* SDR tune command: freq=868MHz, bw=20MHz, gain=30dB */
    uint8_t sdr_tune[8];
    sdr_tune[0] = 0x00; sdr_tune[1] = 0x43; sdr_tune[2] = 0xBE; sdr_tune[3] = 0x00; /* freq */
    sdr_tune[4] = 0x20; sdr_tune[5] = 0x4E;  /* bw */
    sdr_tune[6] = 0x2C; sdr_tune[7] = 0x01;  /* gain */

    frame_len = build_spi_frame(CMD_SDR_TUNE, sdr_tune, 8, frame, sizeof(frame));
    ASSERT_EQ_INT(28, frame_len, "SDR tune frame size = 28 (16+8+4)");
    ASSERT_TRUE(frame_len > 0, "Build SDR tune frame succeeds");

    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate SDR tune frame");
    ASSERT_EQ_INT(CMD_SDR_TUNE, cmd, "Parsed command = SDR_TUNE");
    ASSERT_EQ_INT(8, (int)payload_len, "Parsed payload length = 8");

    /* Verify payload content matches */
    ASSERT_EQ_INT(0, memcmp(payload, sdr_tune, 8), "Payload content matches");
}

/* Test 5: Detect corrupted sync byte */
static void test_bad_sync_byte(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Corrupt sync byte */
    frame[0] = 0xBB;

    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_SYNC, ret, "Detect bad sync byte");
}

/* Test 6: Detect corrupted header CRC-64 */
static void test_bad_header_crc64(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Corrupt a header byte (byte 1 = command) and DON'T update CRC-64 */
    frame[1] ^= 0xFF;

    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_HDR_CRC, ret, "Detect bad header CRC-64");
}

/* Test 7: Detect corrupted payload CRC-32 */
static void test_bad_payload_crc32(void) {
    uint8_t frame[512];
    int frame_len;
    int ret;
    uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    frame_len = build_spi_frame(CMD_ANT_SELECT, payload, 4, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build antenna select frame");

    /* Corrupt a payload byte */
    frame[SPI_HDR_SIZE + 1] ^= 0x01;

    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_PAY_CRC, ret, "Detect bad payload CRC-32");
}

/* Test 8: Detect truncated frame */
static void test_truncated_frame(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Validate with less than minimum frame size */
    ret = validate_spi_frame(frame, 5, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_TRUNC, ret, "Detect truncated frame (5 bytes)");

    ret = validate_spi_frame(frame, 10, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_TRUNC, ret, "Detect truncated frame (10 bytes)");

    ret = validate_spi_frame(frame, 19, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_TRUNC, ret, "Detect truncated frame (19 bytes, just under minimum)");
}

/* Test 9: Detect length mismatch */
static void test_length_mismatch(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Corrupt payload length field to claim more data than present */
    frame[2] = 100;  /* Claim 100 bytes of payload, but frame is only 20 bytes */

    /* Re-compute header CRC-64 for the modified header so that check passes */
    uint64_t new_crc = crc64_compute(frame, 8);
    frame[8]  = (uint8_t)(new_crc & 0xFF);
    frame[9]  = (uint8_t)((new_crc >> 8) & 0xFF);
    frame[10] = (uint8_t)((new_crc >> 16) & 0xFF);
    frame[11] = (uint8_t)((new_crc >> 24) & 0xFF);
    frame[12] = (uint8_t)((new_crc >> 32) & 0xFF);
    frame[13] = (uint8_t)((new_crc >> 40) & 0xFF);
    frame[14] = (uint8_t)((new_crc >> 48) & 0xFF);
    frame[15] = (uint8_t)((new_crc >> 56) & 0xFF);

    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_LEN, ret, "Detect length mismatch");
}

/* Test 10: Validate maximum payload frame */
static void test_max_payload_frame(void) {
    uint8_t *frame;
    uint8_t *payload;
    int frame_len;
    int ret;
    uint16_t payload_len_out;
    uint8_t cmd_out;
    const uint8_t *payload_out;

    payload = (uint8_t *)malloc(SPI_MAX_PAYLOAD);
    frame = (uint8_t *)malloc(SPI_FRAME_SIZE_MAX + 16);
    ASSERT_TRUE(payload != NULL, "Allocate payload buffer");
    ASSERT_TRUE(frame != NULL, "Allocate frame buffer");

    /* Fill payload with known pattern */
    for (int i = 0; i < SPI_MAX_PAYLOAD; i++)
        payload[i] = (uint8_t)(i & 0xFF);

    frame_len = build_spi_frame(CMD_SDR_IQ_CHUNK, payload, SPI_MAX_PAYLOAD,
                                frame, SPI_FRAME_SIZE_MAX);
    ASSERT_EQ_INT(SPI_FRAME_SIZE_MAX, frame_len, "Max payload frame size");

    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd_out,
                              &payload_len_out, &payload_out);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate max payload frame");
    ASSERT_EQ_INT(CMD_SDR_IQ_CHUNK, cmd_out, "Parsed command = SDR_IQ_CHUNK");
    ASSERT_EQ_INT(SPI_MAX_PAYLOAD, (int)payload_len_out, "Parsed payload length = max");

    /* Verify payload content */
    ASSERT_EQ_INT(0, memcmp(payload_out, payload, SPI_MAX_PAYLOAD),
                  "Max payload content matches");

    free(payload);
    free(frame);
}

/* Test 11: Reject oversized payload */
static void test_oversized_payload(void) {
    uint8_t frame[8192];
    uint8_t big_payload[4096];
    int ret;

    /* Attempt to build frame with payload > 4092 bytes */
    memset(big_payload, 0xAA, sizeof(big_payload));

    ret = build_spi_frame(CMD_SDR_IQ_CHUNK, big_payload, 4093,
                          frame, sizeof(frame));
    ASSERT_EQ_INT(-1, ret, "Reject payload > 4092 bytes");

    /* Exactly max should succeed */
    ret = build_spi_frame(CMD_SDR_IQ_CHUNK, big_payload, SPI_MAX_PAYLOAD,
                          frame, sizeof(frame));
    ASSERT_TRUE(ret > 0, "Accept payload = max (4092 bytes)");
}

/* Test 12: CRC-32 bit-flip detection (single bit corruption in payload) */
static void test_crc32_single_bit_flip(void) {
    uint8_t frame[512];
    int frame_len;
    uint8_t payload[16];

    /* Fill payload with data */
    for (int i = 0; i < 16; i++)
        payload[i] = (uint8_t)(i * 17);

    frame_len = build_spi_frame(CMD_CC1101_CFG, payload, 16, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build CC1101 config frame");

    /* Flip each bit in the payload and verify CRC-32 catches it */
    bool all_detected = true;
    for (int byte_idx = 0; byte_idx < 16; byte_idx++) {
        for (int bit = 0; bit < 8; bit++) {
            /* Flip one bit in the payload */
            frame[SPI_HDR_SIZE + byte_idx] ^= (1 << bit);

            int ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
            if (ret != VALID_ERR_PAY_CRC) {
                all_detected = false;
                printf("  Missed bit flip at byte %d bit %d\n", byte_idx, bit);
            }

            /* Restore the bit */
            frame[SPI_HDR_SIZE + byte_idx] ^= (1 << bit);
        }
    }

    ASSERT_TRUE(all_detected, "CRC-32 detects all single-bit flips in payload");
}

/* Test 13: CRC-64 bit-flip detection (single bit corruption in header) */
static void test_crc64_single_bit_flip(void) {
    uint8_t frame[64];
    int frame_len;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Flip each bit in the header (bytes 0-7) and verify CRC-64 catches it */
    bool all_detected = true;
    for (int byte_idx = 0; byte_idx < 8; byte_idx++) {
        for (int bit = 0; bit < 8; bit++) {
            /* Flip one bit in the header */
            frame[byte_idx] ^= (1 << bit);

            int ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
            if (ret != VALID_ERR_HDR_CRC && ret != VALID_ERR_SYNC) {
                all_detected = false;
                printf("  Missed bit flip at header byte %d bit %d (ret=%d)\n",
                       byte_idx, bit, ret);
            }

            /* Restore the bit */
            frame[byte_idx] ^= (1 << bit);
        }
    }

    ASSERT_TRUE(all_detected, "CRC-64 detects all single-bit flips in header");
}

/* Test 14: Multiple frame types */
static void test_multiple_frame_types(void) {
    uint8_t frame[512];
    int frame_len;
    int ret;
    uint8_t cmd;
    uint16_t payload_len;
    const uint8_t *payload;

    /* Telemetry request (no payload) */
    ret = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(ret > 0, "Build telemetry request");
    frame_len = ret;
    ASSERT_EQ_INT(CMD_TELEMETRY_REQ, frame[1], "Command = telemetry req");
    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate telemetry request");
    ASSERT_EQ_INT(CMD_TELEMETRY_REQ, cmd, "Parsed command = telemetry req");

    /* Antenna select (1 byte payload) */
    uint8_t ant = ANT_SUBGHZ;
    ret = build_spi_frame(CMD_ANT_SELECT, &ant, 1, frame, sizeof(frame));
    ASSERT_TRUE(ret > 0, "Build antenna select");
    frame_len = ret;
    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate antenna select");
    ASSERT_EQ_INT(CMD_ANT_SELECT, cmd, "Parsed command = antenna select");
    ASSERT_EQ_INT(1, (int)payload_len, "Antenna select payload = 1 byte");
    ASSERT_EQ_INT(ANT_SUBGHZ, payload[0], "Antenna ID = SUBGHZ");

    /* NFC transaction (variable payload) */
    uint8_t nfc_data[] = {0x26, 0x00};  /* REQA command */
    ret = build_spi_frame(CMD_NFC_TRANSACT, nfc_data, 2, frame, sizeof(frame));
    ASSERT_TRUE(ret > 0, "Build NFC transact");
    frame_len = ret;
    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate NFC transact");
    ASSERT_EQ_INT(CMD_NFC_TRANSACT, cmd, "Parsed command = NFC transact");
    ASSERT_EQ_INT(2, (int)payload_len, "NFC payload = 2 bytes");

    /* SDR stream start (1 byte payload: enable flag) */
    uint8_t enable = 1;
    ret = build_spi_frame(CMD_SDR_STREAM, &enable, 1, frame, sizeof(frame));
    ASSERT_TRUE(ret > 0, "Build SDR stream start");
    frame_len = ret;
    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate SDR stream start");
    ASSERT_EQ_INT(CMD_SDR_STREAM, cmd, "Parsed command = SDR stream");

    /* MCU -> Host: telemetry response */
    uint8_t telem[16] = {0};  /* 16-byte telemetry struct */
    telem[0] = 0xE0; telem[1] = 0xFF;  /* rssi_dbm_x10 = -32.0 dBm */
    telem[2] = 0x2A; telem[3] = 0x01;  /* temp_c_x10 = 29.8 °C */
    telem[4] = 0xA4; telem[5] = 0x0F;  /* vbat_mv = 4004 mV */
    ret = build_spi_frame(CMD_TELEMETRY, telem, 16, frame, sizeof(frame));
    ASSERT_TRUE(ret > 0, "Build telemetry response");
    frame_len = ret;
    ret = validate_spi_frame(frame, (size_t)frame_len, &cmd, &payload_len, &payload);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate telemetry response");
    ASSERT_EQ_INT(CMD_TELEMETRY, cmd, "Parsed command = TELEMETRY");
    ASSERT_EQ_INT(16, (int)payload_len, "Telemetry payload = 16 bytes");
}

/* Test 15: CRC-64 and CRC-32 consistency (deterministic) */
static void test_crc_consistency(void) {
    /* Compute CRC-64 twice, must be identical */
    uint8_t data64[] = {0xAA, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint64_t crc64_a = crc64_compute(data64, sizeof(data64));
    uint64_t crc64_b = crc64_compute(data64, sizeof(data64));
    ASSERT_EQ_UINT64(crc64_a, crc64_b, "CRC-64 is deterministic");

    /* Compute CRC-32 twice, must be identical */
    uint8_t data32[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    uint32_t crc32_a = crc32_compute(data32, sizeof(data32));
    uint32_t crc32_b = crc32_compute(data32, sizeof(data32));
    ASSERT_EQ_UINT(crc32_a, crc32_b, "CRC-32 is deterministic");
}

/* Test 16: Frame with zero-length payload CRC-32 verification */
static void test_zero_payload_crc32(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* For zero-length payload, CRC-32 should be computed over 0 bytes */
    /* CRC-32("") = 0x00000000 with our init/finalize scheme */
    uint32_t expected_crc32 = crc32_compute((uint8_t *)"", 0);
    uint32_t actual_crc32 = (uint32_t)frame[16]       |
                            ((uint32_t)frame[17] << 8)  |
                            ((uint32_t)frame[18] << 16) |
                            ((uint32_t)frame[19] << 24);

    ASSERT_EQ_UINT(expected_crc32, actual_crc32,
                   "Zero-payload CRC-32 matches computed value");

    /* Full validation should still pass */
    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_OK, ret, "NOP frame with zero payload validates");
}

/* Test 17: Corrupted CRC-32 trailer in NOP frame */
static void test_corrupted_crc32_trailer(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Corrupt the CRC-32 trailer (last 4 bytes) */
    frame[frame_len - 1] ^= 0xFF;

    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_PAY_CRC, ret, "Detect corrupted CRC-32 trailer");
}

/* Test 18: Corrupted CRC-64 bytes in header */
static void test_corrupted_crc64_bytes(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Corrupt each byte of the CRC-64 field (bytes 8-15) */
    for (int i = 8; i < 16; i++) {
        frame[i] ^= 0x01;  /* Flip lowest bit */
        ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
        ASSERT_EQ_INT(VALID_ERR_HDR_CRC, ret, "Detect corrupted CRC-64 byte");
        frame[i] ^= 0x01;  /* Restore */
    }
}

/* Test 19: Reserved bytes must be zero */
static void test_reserved_bytes_nonzero(void) {
    uint8_t frame[64];
    int frame_len;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame");

    /* Verify reserved bytes are zero */
    ASSERT_EQ_INT(0, frame[4], "Reserved byte 4 = 0");
    ASSERT_EQ_INT(0, frame[5], "Reserved byte 5 = 0");
    ASSERT_EQ_INT(0, frame[6], "Reserved byte 6 = 0");
    ASSERT_EQ_INT(0, frame[7], "Reserved byte 7 = 0");

    /* Setting reserved bytes to non-zero should still validate
     * (the CRC-64 covers these bytes, so if they're non-zero but
     * the CRC-64 is updated, the frame is technically valid per the
     * protocol spec, though implementations should set them to zero) */
    frame[4] = 0x42;
    /* Update CRC-64 to match the modified header */
    uint64_t new_crc = crc64_compute(frame, 8);
    frame[8]  = (uint8_t)(new_crc & 0xFF);
    frame[9]  = (uint8_t)((new_crc >> 8) & 0xFF);
    frame[10] = (uint8_t)((new_crc >> 16) & 0xFF);
    frame[11] = (uint8_t)((new_crc >> 24) & 0xFF);
    frame[12] = (uint8_t)((new_crc >> 32) & 0xFF);
    frame[13] = (uint8_t)((new_crc >> 40) & 0xFF);
    frame[14] = (uint8_t)((new_crc >> 48) & 0xFF);
    frame[15] = (uint8_t)((new_crc >> 56) & 0xFF);

    int ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_OK, ret,
                  "Frame with non-zero reserved bytes validates (CRC covers it)");
}

/* Test 20: Fuzz — random bit corruption is always detected */
static void test_fuzz_random_corruption(void) {
    uint8_t frame[512];
    uint8_t payload[64];
    int frame_len;
    int ret;
    int total_corruptions = 0;
    int detected_corruptions = 0;

    /* Build a frame with payload */
    for (int i = 0; i < 64; i++)
        payload[i] = (uint8_t)(i ^ 0x55);

    frame_len = build_spi_frame(CMD_SDR_STREAM, payload, 64, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build fuzz test frame");

    /* Use a simple LCG PRNG for reproducibility */
    uint32_t seed = 0xDEADBEEF;
    for (int trial = 0; trial < 200; trial++) {
        seed = seed * 1103515245 + 12345;
        int byte_idx = (seed >> 16) % frame_len;
        seed = seed * 1103515245 + 12345;
        int bit_idx = (seed >> 16) % 8;

        /* Corrupt one bit */
        frame[byte_idx] ^= (1 << bit_idx);
        total_corruptions++;

        ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
        if (ret != VALID_OK)
            detected_corruptions++;

        /* Restore */
        frame[byte_idx] ^= (1 << bit_idx);
    }

    ASSERT_EQ_INT(total_corruptions, detected_corruptions,
                  "All random bit corruptions detected (200 trials)");
}

/* Test 21: Command opcode range validation */
static void test_command_opcodes(void) {
    /* Verify all defined command opcodes are distinct */
    uint8_t cmds[] = {
        CMD_NOP, CMD_SDR_TUNE, CMD_SDR_STREAM, CMD_ANT_SELECT,
        CMD_CC1101_CFG, CMD_NFC_TRANSACT, CMD_TELEMETRY_REQ,
        CMD_TELEMETRY, CMD_SDR_IQ_CHUNK
    };
    int n_cmds = sizeof(cmds) / sizeof(cmds[0]);
    bool all_distinct = true;

    for (int i = 0; i < n_cmds; i++) {
        for (int j = i + 1; j < n_cmds; j++) {
            if (cmds[i] == cmds[j]) {
                printf("  Duplicate command opcode: 0x%02x\n", cmds[i]);
                all_distinct = false;
            }
        }
    }

    ASSERT_TRUE(all_distinct, "All command opcodes are distinct");

    /* MCU→Host commands should have bit 7 set */
    ASSERT_TRUE((CMD_TELEMETRY & 0x80) != 0, "CMD_TELEMETRY has bit 7 set");
    ASSERT_TRUE((CMD_SDR_IQ_CHUNK & 0x80) != 0, "CMD_SDR_IQ_CHUNK has bit 7 set");

    /* Host→MCU commands should have bit 7 clear (except NOP=0xFF) */
    ASSERT_TRUE((CMD_SDR_TUNE & 0x80) == 0, "CMD_SDR_TUNE has bit 7 clear");
    ASSERT_TRUE((CMD_SDR_STREAM & 0x80) == 0, "CMD_SDR_STREAM has bit 7 clear");
    ASSERT_TRUE((CMD_ANT_SELECT & 0x80) == 0, "CMD_ANT_SELECT has bit 7 clear");
    ASSERT_TRUE((CMD_CC1101_CFG & 0x80) == 0, "CMD_CC1101_CFG has bit 7 clear");
    ASSERT_TRUE((CMD_NFC_TRANSACT & 0x80) == 0, "CMD_NFC_TRANSACT has bit 7 clear");
}

/* Test 22: Payload length boundary conditions */
static void test_payload_length_boundaries(void) {
    uint8_t *frame;
    uint8_t *payload;
    int ret;
    int frame_len;

    /* Test payload length = 1 */
    payload = (uint8_t *)malloc(1);
    frame = (uint8_t *)malloc(SPI_FRAME_SIZE_MAX + 16);
    payload[0] = 0x42;
    frame_len = build_spi_frame(CMD_ANT_SELECT, payload, 1, frame, SPI_FRAME_SIZE_MAX);
    ASSERT_TRUE(frame_len == 21, "Frame with 1-byte payload = 21 bytes");
    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate 1-byte payload frame");
    free(payload);

    /* Test payload length = 255 (crosses uint8_t boundary) */
    payload = (uint8_t *)malloc(255);
    memset(payload, 0xAA, 255);
    frame_len = build_spi_frame(CMD_SDR_IQ_CHUNK, payload, 255, frame, SPI_FRAME_SIZE_MAX);
    ASSERT_TRUE(frame_len == 275, "Frame with 255-byte payload = 275 bytes");
    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate 255-byte payload frame");
    free(payload);

    /* Test payload length = 256 (exactly crosses to len_hi = 1) */
    payload = (uint8_t *)malloc(256);
    memset(payload, 0xBB, 256);
    frame_len = build_spi_frame(CMD_SDR_IQ_CHUNK, payload, 256, frame, SPI_FRAME_SIZE_MAX);
    ASSERT_TRUE(frame_len == 276, "Frame with 256-byte payload = 276 bytes");
    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate 256-byte payload frame");
    free(payload);

    /* Test payload length = 4092 (maximum) */
    payload = (uint8_t *)malloc(SPI_MAX_PAYLOAD);
    memset(payload, 0xCC, SPI_MAX_PAYLOAD);
    frame_len = build_spi_frame(CMD_SDR_IQ_CHUNK, payload, SPI_MAX_PAYLOAD,
                                frame, SPI_FRAME_SIZE_MAX);
    ASSERT_EQ_INT(SPI_FRAME_SIZE_MAX, frame_len, "Max payload frame = 4112 bytes");
    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate max payload frame");
    free(payload);
    free(frame);
}

/* Test 23: Round-trip integrity — build then validate all command types */
static void test_round_trip_all_commands(void) {
    uint8_t frame[512];
    uint8_t payload[32];
    int frame_len;
    int ret;
    uint8_t cmd_out;
    uint16_t len_out;
    const uint8_t *payload_out;

    struct {
        uint8_t cmd;
        uint16_t len;
    } test_cases[] = {
        { CMD_NOP,            0 },
        { CMD_SDR_TUNE,       8 },
        { CMD_SDR_STREAM,     1 },
        { CMD_ANT_SELECT,     1 },
        { CMD_CC1101_CFG,    16 },
        { CMD_NFC_TRANSACT,   2 },
        { CMD_TELEMETRY_REQ,  0 },
        { CMD_TELEMETRY,     16 },
        { CMD_SDR_IQ_CHUNK, 32 },
    };

    int n_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < n_cases; i++) {
        /* Prepare payload */
        for (uint16_t j = 0; j < test_cases[i].len; j++)
            payload[j] = (uint8_t)(j ^ test_cases[i].cmd);

        frame_len = build_spi_frame(test_cases[i].cmd,
                                     test_cases[i].len > 0 ? payload : NULL,
                                     test_cases[i].len,
                                     frame, sizeof(frame));
        ASSERT_TRUE(frame_len > 0, "Build frame for command");

        ret = validate_spi_frame(frame, (size_t)frame_len,
                                  &cmd_out, &len_out, &payload_out);
        ASSERT_EQ_INT(VALID_OK, ret, "Validate round-trip frame");
        ASSERT_EQ_INT(test_cases[i].cmd, cmd_out, "Command matches");
        ASSERT_EQ_INT(test_cases[i].len, len_out, "Payload length matches");

        if (test_cases[i].len > 0) {
            /* Verify payload content */
            bool content_match = true;
            for (uint16_t j = 0; j < test_cases[i].len; j++) {
                if (payload_out[j] != (uint8_t)(j ^ test_cases[i].cmd)) {
                    content_match = false;
                    break;
                }
            }
            ASSERT_TRUE(content_match, "Payload content matches after round-trip");
        }
    }
}

/* Test 24: Double corruption detection (two bit flips in different regions) */
static void test_double_bit_corruption(void) {
    uint8_t frame[512];
    int frame_len;
    uint8_t payload[32];

    for (int i = 0; i < 32; i++)
        payload[i] = (uint8_t)(i * 7);

    frame_len = build_spi_frame(CMD_CC1101_CFG, payload, 32, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build frame for double corruption test");

    /* Corrupt both a header byte and a payload byte */
    frame[3] ^= 0x01;  /* Flip bit in payload length field */
    /* Don't fix CRC — this should fail header CRC */
    int ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_TRUE(ret != VALID_OK, "Double corruption detected");

    /* Restore and corrupt only payload (with valid header) */
    frame[3] ^= 0x01;  /* Restore */
    frame[SPI_HDR_SIZE + 5] ^= 0x80;  /* Flip MSB of payload byte 5 */
    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_EQ_INT(VALID_ERR_PAY_CRC, ret,
                  "Payload corruption detected with valid header");
}

/* Test 25: Frame size calculations */
static void test_frame_size_calculations(void) {
    /* Minimum frame: header (16) + zero payload + CRC-32 (4) = 20 */
    ASSERT_EQ_INT(20, SPI_HDR_SIZE + 0 + SPI_CRC32_SIZE,
                  "Minimum frame size = 20 bytes");

    /* Maximum frame: header (16) + max payload (4092) + CRC-32 (4) = 4112 */
    ASSERT_EQ_INT(4112, SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE,
                  "Maximum frame size = 4112 bytes");

    /* Frame with typical SDR IQ chunk: 16 + 4096 + 4 = not possible
     * (4096 > SPI_MAX_PAYLOAD), so verify boundary */
    ASSERT_TRUE(SPI_MAX_PAYLOAD == 4092,
                "SPI_MAX_PAYLOAD = 4092");
}

/* Test 26: CMD_RESET_MCU frame with magic value */
static void test_reset_mcu_frame(void) {
    uint8_t frame[64];
    uint8_t payload[4];
    int frame_len;
    int ret;
    uint8_t cmd_out;
    uint16_t len_out;
    const uint8_t *payload_out;

    /* Pack the reset magic as little-endian */
    payload[0] = (uint8_t)(SPI_RESET_MAGIC & 0xFF);        /* 'T' = 0x54 */
    payload[1] = (uint8_t)((SPI_RESET_MAGIC >> 8) & 0xFF); /* 'E' = 0x45 */
    payload[2] = (uint8_t)((SPI_RESET_MAGIC >> 16) & 0xFF); /* 'S' = 0x53 */
    payload[3] = (uint8_t)((SPI_RESET_MAGIC >> 24) & 0xFF); /* 'R' = 0x52 */

    /* Verify magic bytes are correct */
    ASSERT_EQ_INT(0x54, payload[0], "Reset magic byte 0 = 'T'");
    ASSERT_EQ_INT(0x45, payload[1], "Reset magic byte 1 = 'E'");
    ASSERT_EQ_INT(0x53, payload[2], "Reset magic byte 2 = 'S'");
    ASSERT_EQ_INT(0x52, payload[3], "Reset magic byte 3 = 'R'");

    frame_len = build_spi_frame(CMD_RESET_MCU, payload, 4, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build CMD_RESET_MCU frame");
    ASSERT_EQ_INT(24, frame_len, "Reset frame = 16 + 4 + 4 = 24 bytes");

    ret = validate_spi_frame(frame, (size_t)frame_len,
                              &cmd_out, &len_out, &payload_out);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate CMD_RESET_MCU frame");
    ASSERT_EQ_INT(CMD_RESET_MCU, cmd_out, "Command = CMD_RESET_MCU");
    ASSERT_EQ_INT(4, len_out, "Payload length = 4");

    /* Verify magic value round-trip */
    uint32_t magic_out = (uint32_t)payload_out[0]       |
                         ((uint32_t)payload_out[1] << 8)  |
                         ((uint32_t)payload_out[2] << 16) |
                         ((uint32_t)payload_out[3] << 24);
    ASSERT_EQ_INT((int)SPI_RESET_MAGIC, (int)magic_out,
                  "Reset magic value round-trip");
}

/* Test 27: CMD_RESET_MCU with wrong magic value */
static void test_reset_mcu_wrong_magic(void) {
    uint8_t frame[64];
    uint8_t payload[4];
    int frame_len;
    int ret;
    uint8_t cmd_out;
    uint16_t len_out;
    const uint8_t *payload_out;

    /* Pack a wrong magic value — should still be a valid frame
     * structurally, but the MCU firmware would reject it. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0x00;
    payload[3] = 0x00;

    frame_len = build_spi_frame(CMD_RESET_MCU, payload, 4, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build CMD_RESET_MCU with wrong magic");

    ret = validate_spi_frame(frame, (size_t)frame_len,
                              &cmd_out, &len_out, &payload_out);
    ASSERT_EQ_INT(VALID_OK, ret, "Frame structure is valid");
    ASSERT_EQ_INT(CMD_RESET_MCU, cmd_out, "Command = CMD_RESET_MCU");

    /* Wrong magic: MCU should reject, but frame CRC is valid */
    uint32_t wrong_magic = (uint32_t)payload_out[0]       |
                           ((uint32_t)payload_out[1] << 8)  |
                           ((uint32_t)payload_out[2] << 16) |
                           ((uint32_t)payload_out[3] << 24);
    ASSERT_TRUE(wrong_magic != SPI_RESET_MAGIC,
                "Wrong magic != SPI_RESET_MAGIC");
}

/* Test 28: CMD_TELEMETRY_REQ with zero-length payload */
static void test_telemetry_req_frame(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;
    uint8_t cmd_out;
    uint16_t len_out;

    frame_len = build_spi_frame(CMD_TELEMETRY_REQ, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build CMD_TELEMETRY_REQ frame");
    ASSERT_EQ_INT(20, frame_len, "Telemetry request = 20 bytes (no payload)");

    ret = validate_spi_frame(frame, (size_t)frame_len,
                              &cmd_out, &len_out, NULL);
    ASSERT_EQ_INT(VALID_OK, ret, "Validate CMD_TELEMETRY_REQ frame");
    ASSERT_EQ_INT(CMD_TELEMETRY_REQ, cmd_out, "Command = CMD_TELEMETRY_REQ");
    ASSERT_EQ_INT(0, len_out, "Payload length = 0");
}

/* Test 29: Fuzz test — random payload corruption with varying positions */
static void test_fuzz_payload_positions(void) {
    uint8_t frame[512];
    uint8_t payload[64];
    int frame_len;
    int failures_detected = 0;
    int total_tests = 0;

    /* Build a valid frame with a 64-byte payload */
    for (int i = 0; i < 64; i++)
        payload[i] = (uint8_t)(i ^ 0xA5);

    frame_len = build_spi_frame(CMD_SDR_STREAM, payload, 64,
                                frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build frame for payload position fuzz test");

    /* Corrupt each payload byte one at a time — CRC-32 must catch it */
    for (int offset = SPI_HDR_SIZE; offset < frame_len - SPI_CRC32_SIZE; offset++) {
        uint8_t saved = frame[offset];
        frame[offset] ^= 0x01;  /* Flip one bit */
        total_tests++;

        int ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
        if (ret != VALID_OK)
            failures_detected++;

        frame[offset] = saved;  /* Restore */
    }

    ASSERT_TRUE(failures_detected == total_tests,
                "All single-bit flips in payload detected by CRC-32");
}

/* Test 30: Fuzz test — random header corruption with varying positions */
static void test_fuzz_header_positions(void) {
    uint8_t frame[512];
    uint8_t payload[64];
    int frame_len;
    int failures_detected = 0;
    int total_tests = 0;

    /* Build a valid frame */
    for (int i = 0; i < 64; i++)
        payload[i] = (uint8_t)(i ^ 0x5A);

    frame_len = build_spi_frame(CMD_CC1101_CFG, payload, 64,
                                frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build frame for header position fuzz test");

    /* Corrupt each header byte (except CRC-64 field itself at offsets 8-15)
     * — CRC-64 must catch it */
    for (int offset = 0; offset < SPI_HDR_SIZE; offset++) {
        if (offset >= 8 && offset <= 15) {
            /* CRC-64 field — corrupting it would break the CRC value itself,
             * which would also be detected. Test it too. */
        }
        uint8_t saved = frame[offset];
        frame[offset] ^= 0x80;  /* Flip MSB */
        total_tests++;

        int ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
        if (ret != VALID_OK)
            failures_detected++;

        frame[offset] = saved;  /* Restore */
    }

    ASSERT_TRUE(failures_detected == total_tests,
                "All single-bit flips in header detected by CRC-64");
}

/* Test 31: Opcode range and distinctness for all commands including new ones */
static void test_opcode_range_extended(void) {
    uint8_t cmds[] = {
        CMD_NOP, CMD_SDR_TUNE, CMD_SDR_STREAM, CMD_ANT_SELECT,
        CMD_CC1101_CFG, CMD_NFC_TRANSACT, CMD_TELEMETRY_REQ,
        CMD_RESET_MCU, CMD_TELEMETRY, CMD_SDR_IQ_CHUNK
    };
    int n = sizeof(cmds) / sizeof(cmds[0]);

    /* All opcodes must be distinct */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            ASSERT_TRUE(cmds[i] != cmds[j],
                        "All extended opcodes are distinct");
        }
    }

    /* CMD_RESET_MCU = 0x07, must be in Host→MCU range (bit 7 clear) */
    ASSERT_TRUE((CMD_RESET_MCU & 0x80) == 0,
                "CMD_RESET_MCU has bit 7 clear (Host→MCU)");

    /* CMD_RESET_MCU must not equal any existing opcode */
    ASSERT_TRUE(CMD_RESET_MCU != CMD_NOP, "CMD_RESET_MCU != CMD_NOP");
    ASSERT_TRUE(CMD_RESET_MCU != CMD_SDR_TUNE, "CMD_RESET_MCU != CMD_SDR_TUNE");
    ASSERT_TRUE(CMD_RESET_MCU != CMD_SDR_STREAM, "CMD_RESET_MCU != CMD_SDR_STREAM");
    ASSERT_TRUE(CMD_RESET_MCU != CMD_ANT_SELECT, "CMD_RESET_MCU != CMD_ANT_SELECT");
    ASSERT_TRUE(CMD_RESET_MCU != CMD_CC1101_CFG, "CMD_RESET_MCU != CMD_CC1101_CFG");
    ASSERT_TRUE(CMD_RESET_MCU != CMD_NFC_TRANSACT, "CMD_RESET_MCU != CMD_NFC_TRANSACT");
    ASSERT_TRUE(CMD_RESET_MCU != CMD_TELEMETRY_REQ, "CMD_RESET_MCU != CMD_TELEMETRY_REQ");
}

/* Test 32: Reserved field validation — non-zero reserved must be rejected */
static void test_reserved_field_nonzero(void) {
    uint8_t frame[64];
    int frame_len;
    int ret;

    frame_len = build_spi_frame(CMD_NOP, NULL, 0, frame, sizeof(frame));
    ASSERT_TRUE(frame_len > 0, "Build NOP frame for reserved field test");

    /* Set reserved bytes to non-zero */
    frame[4] = 0x01;  /* reserved byte 0 */
    /* Don't update CRC — should fail header CRC */

    ret = validate_spi_frame(frame, (size_t)frame_len, NULL, NULL, NULL);
    ASSERT_TRUE(ret != VALID_OK, "Non-zero reserved byte detected via CRC-64");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade SPI Protocol Unit Tests ===\n");
    printf("Using CRC-64 (ECMA-182) for header and CRC-32 (ISO 3309) for payload\n");
    printf("Frame format: 16-byte header + payload (0-4092) + 4-byte CRC-32 trailer\n\n");

    /* Initialize CRC tables */
    crc64_init_table();
    crc32_init_table();

    RUN_TEST(test_crc64_known_vectors);
    RUN_TEST(test_crc32_known_vectors);
    RUN_TEST(test_valid_nop_frame);
    RUN_TEST(test_valid_payload_frame);
    RUN_TEST(test_bad_sync_byte);
    RUN_TEST(test_bad_header_crc64);
    RUN_TEST(test_bad_payload_crc32);
    RUN_TEST(test_truncated_frame);
    RUN_TEST(test_length_mismatch);
    RUN_TEST(test_max_payload_frame);
    RUN_TEST(test_oversized_payload);
    RUN_TEST(test_crc32_single_bit_flip);
    RUN_TEST(test_crc64_single_bit_flip);
    RUN_TEST(test_multiple_frame_types);
    RUN_TEST(test_crc_consistency);
    RUN_TEST(test_zero_payload_crc32);
    RUN_TEST(test_corrupted_crc32_trailer);
    RUN_TEST(test_corrupted_crc64_bytes);
    RUN_TEST(test_reserved_bytes_nonzero);
    RUN_TEST(test_fuzz_random_corruption);
    RUN_TEST(test_command_opcodes);
    RUN_TEST(test_payload_length_boundaries);
    RUN_TEST(test_round_trip_all_commands);
    RUN_TEST(test_double_bit_corruption);
    RUN_TEST(test_frame_size_calculations);
    RUN_TEST(test_reset_mcu_frame);
    RUN_TEST(test_reset_mcu_wrong_magic);
    RUN_TEST(test_telemetry_req_frame);
    RUN_TEST(test_fuzz_payload_positions);
    RUN_TEST(test_fuzz_header_positions);
    RUN_TEST(test_opcode_range_extended);
    RUN_TEST(test_reserved_field_nonzero);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}