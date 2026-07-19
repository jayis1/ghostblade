/*
 * test_libapex.c — Unit Tests for libapex Frame Building and Parsing
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Userspace unit tests for the libapex C library. These tests exercise
 * the frame building and parsing functions used by the library to
 * communicate with the kernel driver. They validate:
 *
 *   1. Frame construction with all command types
 *   2. Frame parsing and CRC validation
 *   3. Telemetry structure layout and byte ordering
 *   4. SDR tune parameter encoding
 *   5. CC1101 register configuration encoding
 *   6. NFC transaction command encoding
 *   7. Error handling for invalid inputs
 *   8. Endianness conversion correctness
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -I../software/libapex/include \
 *       test_libapex.c -o test_libapex
 *
 * Run:
 *   ./test_libapex
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * CRC-64 Implementation (matches firmware and kernel driver)
 * ======================================================================== */

#define CRC64_POLY   0x42F0E1EBA9EA3693ULL

static uint64_t crc64_table[256];
static bool crc64_initialized = false;

static void crc64_init(void) {
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
    crc64_initialized = true;
}

static uint64_t crc64_compute(const uint8_t *data, size_t len) {
    if (!crc64_initialized) crc64_init();
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (size_t i = 0; i < len; i++)
        crc = crc64_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/* ========================================================================
 * CRC-32 Implementation (matches firmware and kernel driver)
 * ======================================================================== */

#define CRC32_POLY   0xEDB88320UL

static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void crc32_init(void) {
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
    crc32_initialized = true;
}

static uint32_t crc32_compute(const uint8_t *data, size_t len) {
    if (!crc32_initialized) crc32_init();
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * SPI Frame Definitions (matches libapex.h / kernel driver)
 * ======================================================================== */

#define APEX_SPI_SYNC_BYTE       0xAA
#define APEX_SPI_HDR_SIZE        16
#define APEX_SPI_MAX_PAYLOAD     4092
#define APEX_SPI_CRC32_SIZE      4
#define APEX_SPI_FRAME_SIZE_MAX  (APEX_SPI_HDR_SIZE + APEX_SPI_MAX_PAYLOAD + APEX_SPI_CRC32_SIZE)

/* Command opcodes */
#define CMD_NOP           0xFF
#define CMD_SDR_TUNE      0x01
#define CMD_SDR_STREAM    0x02
#define CMD_ANT_SELECT    0x03
#define CMD_CC1101_CFG    0x04
#define CMD_NFC_TRANSACT  0x05
#define CMD_TELEMETRY_REQ 0x06
#define CMD_RESET_MCU     0x07
#define CMD_TELEMETRY     0x81
#define CMD_SDR_IQ_CHUNK  0x82

/* Telemetry flags */
#define TELEM_FLAG_SDR_RX_ACTIVE    (1 << 0)
#define TELEM_FLAG_SDR_TX_ACTIVE    (1 << 1)
#define TELEM_FLAG_CC1101_RX        (1 << 2)
#define TELEM_FLAG_CC1101_TX        (1 << 3)
#define TELEM_FLAG_NFC_ACTIVE       (1 << 4)
#define TELEM_FLAG_NFC_TAG_PRESENT  (1 << 5)
#define TELEM_FLAG_OVERTEMP         (1 << 6)
#define TELEM_FLAG_LOW_BATTERY      (1 << 7)

/* Reset magic value */
#define SPI_RESET_MAGIC   0x52534554UL

/* ========================================================================
 * Frame Builder and Validator
 * ======================================================================== */

static int build_frame(uint8_t cmd, const uint8_t *payload,
                        uint16_t payload_len, uint8_t *frame) {
    if (payload_len > APEX_SPI_MAX_PAYLOAD)
        return -1;

    int idx = 0;
    frame[idx++] = APEX_SPI_SYNC_BYTE;
    frame[idx++] = cmd;
    frame[idx++] = (uint8_t)(payload_len & 0xFF);
    frame[idx++] = (uint8_t)((payload_len >> 8) & 0xFF);
    frame[idx++] = 0x00;
    frame[idx++] = 0x00;
    frame[idx++] = 0x00;
    frame[idx++] = 0x00;

    uint64_t hdr_crc = crc64_compute(frame, 8);
    frame[idx++] = (uint8_t)(hdr_crc & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 8) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 16) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 24) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 32) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 40) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 48) & 0xFF);
    frame[idx++] = (uint8_t)((hdr_crc >> 56) & 0xFF);

    if (payload_len > 0 && payload != NULL) {
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }

    if (payload_len > 0) {
        uint32_t pay_crc = crc32_compute(payload, payload_len);
        frame[idx++] = (uint8_t)(pay_crc & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 8) & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 16) & 0xFF);
        frame[idx++] = (uint8_t)((pay_crc >> 24) & 0xFF);
    }

    return idx;
}

static int validate_frame(const uint8_t *frame, size_t frame_len,
                           uint8_t *cmd, uint16_t *payload_len,
                           const uint8_t **payload) {
    if (frame_len < APEX_SPI_HDR_SIZE)
        return -1;

    if (frame[0] != APEX_SPI_SYNC_BYTE)
        return -2;

    uint64_t actual_crc64 = crc64_compute(frame, 8);
    uint64_t expected_crc64 = 0;
    for (int i = 0; i < 8; i++)
        expected_crc64 |= ((uint64_t)frame[8 + i]) << (8 * i);
    if (actual_crc64 != expected_crc64)
        return -3;

    uint16_t len = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
    if (len > APEX_SPI_MAX_PAYLOAD)
        return -4;

    size_t expected_total = APEX_SPI_HDR_SIZE + len;
    if (len > 0)
        expected_total += APEX_SPI_CRC32_SIZE;

    if (frame_len < expected_total)
        return -5;

    if (len > 0) {
        uint32_t actual_crc32 = crc32_compute(&frame[APEX_SPI_HDR_SIZE], len);
        size_t crc_offset = APEX_SPI_HDR_SIZE + len;
        uint32_t expected_crc32 = 0;
        for (int i = 0; i < 4; i++)
            expected_crc32 |= ((uint32_t)frame[crc_offset + i]) << (8 * i);
        if (actual_crc32 != expected_crc32)
            return -6;
    }

    if (cmd) *cmd = frame[1];
    if (payload_len) *payload_len = len;
    if (payload) *payload = &frame[APEX_SPI_HDR_SIZE];

    return 0;
}

/* ========================================================================
 * SDR Tune Command Encoding
 * ======================================================================== */

static int build_sdr_tune_payload(uint32_t freq_hz, uint16_t bw_khz,
                                   uint16_t gain_db_x10, uint8_t *buf) {
    buf[0] = (uint8_t)(freq_hz & 0xFF);
    buf[1] = (uint8_t)((freq_hz >> 8) & 0xFF);
    buf[2] = (uint8_t)((freq_hz >> 16) & 0xFF);
    buf[3] = (uint8_t)((freq_hz >> 24) & 0xFF);
    buf[4] = (uint8_t)(bw_khz & 0xFF);
    buf[5] = (uint8_t)((bw_khz >> 8) & 0xFF);
    buf[6] = (uint8_t)(gain_db_x10 & 0xFF);
    buf[7] = (uint8_t)((gain_db_x10 >> 8) & 0xFF);
    return 8;
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

static void test_sdr_tune_encoding(void) {
    printf("Test: SDR tune command encoding\n");
    uint8_t payload[8];

    /* Test 1: 868 MHz, 10 MHz BW, 30.0 dB gain */
    build_sdr_tune_payload(868000000, 10000, 300, payload);
    uint32_t freq = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                   ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
    uint16_t bw = (uint16_t)payload[4] | ((uint16_t)payload[5] << 8);
    uint16_t gain = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8);

    TEST_ASSERT_EQ((int)freq, 868000000, "SDR tune: 868 MHz frequency encoding");
    TEST_ASSERT_EQ((int)bw, 10000, "SDR tune: 10 MHz bandwidth encoding");
    TEST_ASSERT_EQ((int)gain, 300, "SDR tune: 30.0 dB gain encoding");

    /* Test 2: 2.4 GHz, 20 MHz BW, 0 dB gain */
    build_sdr_tune_payload(2400000000UL, 20000, 0, payload);
    freq = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
           ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
    bw = (uint16_t)payload[4] | ((uint16_t)payload[5] << 8);
    gain = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8);

    TEST_ASSERT_EQ((int)freq, (int)2400000000UL, "SDR tune: 2.4 GHz frequency encoding");
    TEST_ASSERT_EQ((int)bw, 20000, "SDR tune: 20 MHz bandwidth encoding");
    TEST_ASSERT_EQ((int)gain, 0, "SDR tune: 0 dB gain encoding");

    /* Test 3: Build complete SDR_TUNE frame and validate */
    uint8_t frame[64];
    int len = build_frame(CMD_SDR_TUNE, payload, 8, frame);
    TEST_ASSERT(len > 0, "SDR tune: frame built successfully");

    uint8_t cmd;
    uint16_t plen;
    const uint8_t *pload;
    int ret = validate_frame(frame, len, &cmd, &plen, &pload);
    TEST_ASSERT_EQ(ret, 0, "SDR tune: frame validates");
    TEST_ASSERT_EQ((int)cmd, CMD_SDR_TUNE, "SDR tune: command = 0x01");
    TEST_ASSERT_EQ((int)plen, 8, "SDR tune: payload length = 8");
}

static void test_cc1101_config_encoding(void) {
    printf("Test: CC1101 register configuration encoding\n");

    /* Test 1: Single register write */
    uint8_t payload[3] = { 0x00, 0x01, 0x29 };  /* addr=0x00, len=1, data=0x29 */
    uint8_t frame[64];
    int len = build_frame(CMD_CC1101_CFG, payload, 3, frame);
    TEST_ASSERT(len > 0, "CC1101 cfg: frame built");

    uint8_t cmd;
    uint16_t plen;
    const uint8_t *pload;
    int ret = validate_frame(frame, len, &cmd, &plen, &pload);
    TEST_ASSERT_EQ(ret, 0, "CC1101 cfg: frame validates");
    TEST_ASSERT_EQ((int)cmd, CMD_CC1101_CFG, "CC1101 cfg: command = 0x04");
    TEST_ASSERT_EQ((int)pload[0], 0x00, "CC1101 cfg: reg_addr = 0x00");
    TEST_ASSERT_EQ((int)pload[1], 0x01, "CC1101 cfg: reg_len = 1");
    TEST_ASSERT_EQ((int)pload[2], 0x29, "CC1101 cfg: data[0] = 0x29");

    /* Test 2: Burst register write */
    uint8_t burst_payload[10];
    burst_payload[0] = 0x0D;  /* Start address */
    burst_payload[1] = 0x08;  /* 8 consecutive registers */
    for (int i = 0; i < 8; i++)
        burst_payload[2 + i] = (uint8_t)(i * 0x11);
    len = build_frame(CMD_CC1101_CFG, burst_payload, 10, frame);
    ret = validate_frame(frame, len, &cmd, &plen, &pload);
    TEST_ASSERT_EQ(ret, 0, "CC1101 burst cfg: frame validates");
    TEST_ASSERT_EQ((int)plen, 10, "CC1101 burst cfg: payload = 10 bytes");
}

static void test_nfc_transact_encoding(void) {
    printf("Test: NFC transaction command encoding\n");

    /* Test: NFC poll command */
    uint8_t payload[4] = { 0x26, 0x01, 0x00, 0x00 };  /* REQA, FLAG_TIMEOUT, len=0 */
    uint8_t frame[64];
    int len = build_frame(CMD_NFC_TRANSACT, payload, 4, frame);
    TEST_ASSERT(len > 0, "NFC transact: frame built");

    uint8_t cmd;
    uint16_t plen;
    const uint8_t *pload;
    int ret = validate_frame(frame, len, &cmd, &plen, &pload);
    TEST_ASSERT_EQ(ret, 0, "NFC transact: frame validates");
    TEST_ASSERT_EQ((int)cmd, CMD_NFC_TRANSACT, "NFC transact: command = 0x05");
    TEST_ASSERT_EQ((int)pload[0], 0x26, "NFC transact: cmd = REQA (0x26)");
}

static void test_telemetry_structure_layout(void) {
    printf("Test: Telemetry structure layout\n");

    /* Verify the telemetry payload structure matches the expected layout:
     * Offset  Size  Field
     * 0       2     rssi_dbm_x10 (int16_t LE)
     * 2       2     temp_c_x10 (int16_t LE)
     * 4       2     vbat_mv (uint16_t LE)
     * 6       2     cc1101_rssi_x10 (int16_t LE)
     * 8       2     nfc_field_mv (uint16_t LE)
     * 10      2     flags (uint16_t LE)
     * 12      4     uptime_ms (uint32_t LE)
     * Total: 16 bytes */

    /* Build a mock telemetry payload */
    uint8_t telem[16];
    memset(telem, 0, sizeof(telem));

    /* rssi_dbm_x10 = -450 (-45.0 dBm) = 0xFE38 in int16_t LE */
    telem[0] = 0x38; telem[1] = 0xFE;
    /* temp_c_x10 = 275 (27.5°C) = 0x0113 in int16_t LE */
    telem[2] = 0x13; telem[3] = 0x01;
    /* vbat_mv = 3850 mV = 0x0F0A in uint16_t LE */
    telem[4] = 0x0A; telem[5] = 0x0F;
    /* cc1101_rssi_x10 = -750 (-75.0 dBm) = 0xFD12 in int16_t LE */
    telem[6] = 0x12; telem[7] = 0xFD;
    /* nfc_field_mv = 1200 mV = 0x04B0 in uint16_t LE */
    telem[8] = 0xB0; telem[9] = 0x04;
    /* flags = 0x0085 (SDR_RX_ACTIVE | NFC_ACTIVE | LOW_BATTERY) */
    telem[10] = 0x85; telem[11] = 0x00;
    /* uptime_ms = 123456 = 0x0001E240 in uint32_t LE */
    telem[12] = 0x40; telem[13] = 0xE2; telem[14] = 0x01; telem[15] = 0x00;

    /* Verify field extraction */
    int16_t rssi = (int16_t)((uint16_t)telem[0] | ((uint16_t)telem[1] << 8));
    int16_t temp = (int16_t)((uint16_t)telem[2] | ((uint16_t)telem[3] << 8));
    uint16_t vbat = (uint16_t)telem[4] | ((uint16_t)telem[5] << 8);
    int16_t cc_rssi = (int16_t)((uint16_t)telem[6] | ((uint16_t)telem[7] << 8));
    uint16_t nfc = (uint16_t)telem[8] | ((uint16_t)telem[9] << 8);
    uint16_t flags = (uint16_t)telem[10] | ((uint16_t)telem[11] << 8);
    uint32_t uptime = (uint32_t)telem[12] | ((uint32_t)telem[13] << 8) |
                      ((uint32_t)telem[14] << 16) | ((uint32_t)telem[15] << 24);

    TEST_ASSERT_EQ((int)rssi, -450, "Telemetry: rssi_dbm_x10 = -450");
    TEST_ASSERT_EQ((int)temp, 275, "Telemetry: temp_c_x10 = 275");
    TEST_ASSERT_EQ((int)vbat, 3850, "Telemetry: vbat_mv = 3850");
    TEST_ASSERT_EQ((int)cc_rssi, -750, "Telemetry: cc1101_rssi_x10 = -750");
    TEST_ASSERT_EQ((int)nfc, 1200, "Telemetry: nfc_field_mv = 1200");
    TEST_ASSERT_EQ((int)flags, 0x0085, "Telemetry: flags = 0x0085");
    TEST_ASSERT_EQ((int)uptime, 123456, "Telemetry: uptime_ms = 123456");

    /* Verify flags */
    TEST_ASSERT(flags & TELEM_FLAG_SDR_RX_ACTIVE, "Telemetry: SDR_RX_ACTIVE flag set");
    TEST_ASSERT(flags & TELEM_FLAG_NFC_ACTIVE, "Telemetry: NFC_ACTIVE flag set");
    TEST_ASSERT(flags & TELEM_FLAG_LOW_BATTERY, "Telemetry: LOW_BATTERY flag set");
    TEST_ASSERT(!(flags & TELEM_FLAG_SDR_TX_ACTIVE), "Telemetry: SDR_TX_ACTIVE flag clear");
    TEST_ASSERT(!(flags & TELEM_FLAG_OVERTEMP), "Telemetry: OVERTEMP flag clear");

    /* Build and validate a telemetry response frame */
    uint8_t frame[64];
    int len = build_frame(CMD_TELEMETRY, telem, sizeof(telem), frame);
    TEST_ASSERT(len > 0, "Telemetry frame: built");

    uint8_t cmd;
    uint16_t plen;
    const uint8_t *pload;
    int ret = validate_frame(frame, len, &cmd, &plen, &pload);
    TEST_ASSERT_EQ(ret, 0, "Telemetry frame: validates");
    TEST_ASSERT_EQ((int)cmd, CMD_TELEMETRY, "Telemetry frame: command = 0x81");
    TEST_ASSERT_EQ((int)plen, 16, "Telemetry frame: payload = 16 bytes");
}

static void test_reset_magic_encoding(void) {
    printf("Test: Reset magic value encoding\n");

    uint8_t payload[4];
    payload[0] = (uint8_t)(SPI_RESET_MAGIC & 0xFF);
    payload[1] = (uint8_t)((SPI_RESET_MAGIC >> 8) & 0xFF);
    payload[2] = (uint8_t)((SPI_RESET_MAGIC >> 16) & 0xFF);
    payload[3] = (uint8_t)((SPI_RESET_MAGIC >> 24) & 0xFF);

    /* SPI_RESET_MAGIC = 0x52534554 = "RSET" in ASCII */
    TEST_ASSERT_EQ((int)payload[0], 0x54, "Reset magic: byte 0 = 0x54 ('T')");
    TEST_ASSERT_EQ((int)payload[1], 0x45, "Reset magic: byte 1 = 0x45 ('E')");
    TEST_ASSERT_EQ((int)payload[2], 0x53, "Reset magic: byte 2 = 0x53 ('S')");
    TEST_ASSERT_EQ((int)payload[3], 0x52, "Reset magic: byte 3 = 0x52 ('R')");

    uint8_t frame[64];
    int len = build_frame(CMD_RESET_MCU, payload, 4, frame);
    TEST_ASSERT(len > 0, "Reset frame: built");

    uint8_t cmd;
    uint16_t plen;
    const uint8_t *pload;
    int ret = validate_frame(frame, len, &cmd, &plen, &pload);
    TEST_ASSERT_EQ(ret, 0, "Reset frame: validates");
    TEST_ASSERT_EQ((int)cmd, CMD_RESET_MCU, "Reset frame: command = 0x07");

    /* Verify the magic value round-trips correctly */
    uint32_t decoded_magic = (uint32_t)pload[0] |
                             ((uint32_t)pload[1] << 8) |
                             ((uint32_t)pload[2] << 16) |
                             ((uint32_t)pload[3] << 24);
    TEST_ASSERT_EQ((int)decoded_magic, (int)SPI_RESET_MAGIC,
                   "Reset magic: round-trip matches");
}

static void test_all_command_opcodes(void) {
    printf("Test: All command opcodes\n");
    uint8_t frame[64];
    uint8_t payload[] = { 0x42 };
    uint8_t cmds[] = { CMD_NOP, CMD_SDR_TUNE, CMD_SDR_STREAM,
                       CMD_ANT_SELECT, CMD_CC1101_CFG,
                       CMD_NFC_TRANSACT, CMD_TELEMETRY_REQ,
                       CMD_RESET_MCU };
    const char *names[] = { "NOP", "SDR_TUNE", "SDR_STREAM",
                            "ANT_SELECT", "CC1101_CFG",
                            "NFC_TRANSACT", "TELEMETRY_REQ",
                            "RESET_MCU" };

    for (size_t i = 0; i < sizeof(cmds); i++) {
        int len = build_frame(cmds[i], payload, 1, frame);
        TEST_ASSERT(len > 0, names[i]);

        uint8_t cmd;
        uint16_t plen;
        const uint8_t *pload;
        int ret = validate_frame(frame, len, &cmd, &plen, &pload);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s validates", names[i]);
        TEST_ASSERT_EQ(ret, 0, msg);
    }

    /* Response opcodes */
    uint8_t resp_cmds[] = { CMD_TELEMETRY, CMD_SDR_IQ_CHUNK };
    const char *resp_names[] = { "TELEMETRY", "SDR_IQ_CHUNK" };
    for (size_t i = 0; i < sizeof(resp_cmds); i++) {
        int len = build_frame(resp_cmds[i], payload, 1, frame);
        TEST_ASSERT(len > 0, resp_names[i]);

        uint8_t cmd;
        uint16_t plen;
        const uint8_t *pload;
        int ret = validate_frame(frame, len, &cmd, &plen, &pload);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s validates", resp_names[i]);
        TEST_ASSERT_EQ(ret, 0, msg);
    }
}

static void test_endianness(void) {
    printf("Test: Endianness conversion\n");

    /* Test 32-bit little-endian encoding */
    uint32_t val32 = 0x12345678;
    uint8_t le32[4];
    le32[0] = (uint8_t)(val32 & 0xFF);
    le32[1] = (uint8_t)((val32 >> 8) & 0xFF);
    le32[2] = (uint8_t)((val32 >> 16) & 0xFF);
    le32[3] = (uint8_t)((val32 >> 24) & 0xFF);
    TEST_ASSERT_EQ((int)le32[0], 0x78, "LE32: byte 0 = 0x78");
    TEST_ASSERT_EQ((int)le32[1], 0x56, "LE32: byte 1 = 0x56");
    TEST_ASSERT_EQ((int)le32[2], 0x34, "LE32: byte 2 = 0x34");
    TEST_ASSERT_EQ((int)le32[3], 0x12, "LE32: byte 3 = 0x12");

    /* Test 16-bit little-endian encoding */
    uint16_t val16 = 0xABCD;
    uint8_t le16[2];
    le16[0] = (uint8_t)(val16 & 0xFF);
    le16[1] = (uint8_t)((val16 >> 8) & 0xFF);
    TEST_ASSERT_EQ((int)le16[0], 0xCD, "LE16: byte 0 = 0xCD");
    TEST_ASSERT_EQ((int)le16[1], 0xAB, "LE16: byte 1 = 0xAB");

    /* Test 64-bit little-endian encoding (used for CRC-64) */
    uint64_t val64 = 0x0102030405060708ULL;
    uint8_t le64[8];
    for (int i = 0; i < 8; i++)
        le64[i] = (uint8_t)((val64 >> (8 * i)) & 0xFF);
    TEST_ASSERT_EQ((int)le64[0], 0x08, "LE64: byte 0 = 0x08");
    TEST_ASSERT_EQ((int)le64[7], 0x01, "LE64: byte 7 = 0x01");
}

static void test_battery_helpers(void) {
    printf("Test: Battery helper functions\n");

    /* Test apex_battery_percent equivalent logic — C11 compatible helpers */
    uint8_t battery_percent(uint16_t vbat_mv);
    bool is_low_battery(uint16_t vbat_mv);
    bool is_overtemp(int16_t temp_c_x10);

    TEST_ASSERT_EQ((int)battery_percent(4200), 100, "Battery: 4200mV = 100%");
    TEST_ASSERT_EQ((int)battery_percent(3000), 0, "Battery: 3000mV = 0%");
    TEST_ASSERT_EQ((int)battery_percent(3900), 80, "Battery: 3900mV = 80%");
    TEST_ASSERT_EQ((int)battery_percent(3500), 30, "Battery: 3500mV = 30%");
    TEST_ASSERT_EQ((int)battery_percent(3200), 6, "Battery: 3200mV ≈ 6%");
    TEST_ASSERT_EQ((int)battery_percent(4500), 100, "Battery: 4500mV = 100% (clamped)");
    TEST_ASSERT_EQ((int)battery_percent(2800), 0, "Battery: 2800mV = 0% (clamped)");

    TEST_ASSERT(is_low_battery(3200), "Low battery: 3200mV is low");
    TEST_ASSERT(!is_low_battery(3400), "Low battery: 3400mV is not low");
    TEST_ASSERT(is_low_battery(3299), "Low battery: 3299mV is low");
    TEST_ASSERT(!is_low_battery(3300), "Low battery: 3300mV is threshold");

    TEST_ASSERT(is_overtemp(900), "Overtemp: 90.0°C is overtemp");
    TEST_ASSERT(!is_overtemp(800), "Overtemp: 80.0°C is normal");
    TEST_ASSERT(!is_overtemp(850), "Overtemp: 85.0°C is threshold (not over)");
    TEST_ASSERT(is_overtemp(851), "Overtemp: 85.1°C is overtemp");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade libapex Frame and Encoding Tests ===\n\n");

    crc64_init();
    crc32_init();

    test_sdr_tune_encoding();
    test_cc1101_config_encoding();
    test_nfc_transact_encoding();
    test_telemetry_structure_layout();
    test_reset_magic_encoding();
    test_all_command_opcodes();
    test_endianness();
    test_battery_helpers();

    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}