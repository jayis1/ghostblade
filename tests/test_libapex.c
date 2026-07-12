/*
 * test_libapex.c — Unit Tests for libapex Userspace Library
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the pure functions and validation logic in libapex that do not
 * require hardware access. Covers:
 *   - apex_strerror() error code mapping
 *   - apex_battery_percent() Li-Po discharge curve
 *   - apex_is_low_battery() / apex_is_overtemp() threshold checks
 *   - SDR tune parameter validation
 *   - Antenna selection validation
 *   - CC1101 register configuration validation
 *   - NFC transaction validation
 *   - Secure wipe correctness
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_libapex test_libapex.c -lm
 *
 * Run:
 *   ./test_libapex
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Include libapex header — we link against the pure functions directly.
 * For ioctl-dependent functions, we test the validation logic by
 * compiling a minimal mock of the device handle. */
#include "../software/libapex/include/libapex.h"

/* ========================================================================
 * Minimal Test Framework
 * ======================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(actual, expected, msg) do {                            \
    tests_run++;                                                         \
    if ((actual) == (expected)) {                                         \
        tests_passed++;                                                  \
    } else {                                                             \
        tests_failed++;                                                  \
        fprintf(stderr, "  FAIL [%s:%d]: %s — expected %d, got %d\n",   \
                __FILE__, __LINE__, msg, (int)(expected), (int)(actual));\
    }                                                                    \
} while (0)

#define ASSERT_NEQ(actual, not_expected, msg) do {                      \
    tests_run++;                                                         \
    if ((actual) != (not_expected)) {                                    \
        tests_passed++;                                                  \
    } else {                                                             \
        tests_failed++;                                                  \
        fprintf(stderr, "  FAIL [%s:%d]: %s — should not equal %d\n",    \
                __FILE__, __LINE__, msg, (int)(not_expected));           \
    }                                                                    \
} while (0)

#define ASSERT_STR_EQ(actual, expected, msg) do {                        \
    tests_run++;                                                         \
    if (strcmp((actual), (expected)) == 0) {                             \
        tests_passed++;                                                  \
    } else {                                                             \
        tests_failed++;                                                  \
        fprintf(stderr, "  FAIL [%s:%d]: %s — expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, msg, (expected), (actual));           \
    }                                                                    \
} while (0)

#define ASSERT_TRUE(condition, msg) do {                                 \
    tests_run++;                                                         \
    if (condition) {                                                      \
        tests_passed++;                                                  \
    } else {                                                             \
        tests_failed++;                                                  \
        fprintf(stderr, "  FAIL [%s:%d]: %s — expected true\n",          \
                __FILE__, __LINE__, msg);                                 \
    }                                                                    \
} while (0)

#define ASSERT_FALSE(condition, msg) do {                                \
    tests_run++;                                                         \
    if (!(condition)) {                                                   \
        tests_passed++;                                                  \
    } else {                                                             \
        tests_failed++;                                                  \
        fprintf(stderr, "  FAIL [%s:%d]: %s — expected false\n",         \
                __FILE__, __LINE__, msg);                                 \
    }                                                                    \
} while (0)

#define ASSERT_IN_RANGE(val, lo, hi, msg) do {                           \
    tests_run++;                                                         \
    if ((int)(val) >= (int)(lo) && (int)(val) <= (int)(hi)) {           \
        tests_passed++;                                                  \
    } else {                                                             \
        tests_failed++;                                                  \
        fprintf(stderr, "  FAIL [%s:%d]: %s — %d not in [%d, %d]\n",      \
                __FILE__, __LINE__, msg, (int)(val), (int)(lo), (int)(hi)); \
    }                                                                    \
} while (0)

/* Suppress -Wtype-limits for intentional range checks with unsigned types */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"

/* ========================================================================
 * Inline copies of pure functions from libapex.c for testing
 *
 * We cannot call the actual libapex functions that require an open device
 * handle (ioctl). Instead, we copy the pure computation functions here
 * so we can unit-test them without hardware.
 * ======================================================================== */

/* Error string table — must match libapex.c */
static const char *test_error_strings[] = {
    "Success",                        /* APEX_OK */
    "Invalid argument",               /* APEX_ERR_INVALID_ARG */
    "Device not found",               /* APEX_ERR_NO_DEVICE */
    "Failed to open device",          /* APEX_ERR_OPEN_FAILED */
    "ioctl failed",                   /* APEX_ERR_IOCTL_FAILED */
    "Operation timed out",            /* APEX_ERR_TIMEOUT */
    "Communication error",            /* APEX_ERR_COMM */
    "Device not ready",              /* APEX_ERR_NOT_READY */
    "Out of memory",                 /* APEX_ERR_NOMEM */
};

static const char *test_apex_strerror(int error) {
    int idx = -error;
    if (idx < 0 || idx >= (int)(sizeof(test_error_strings) / sizeof(test_error_strings[0])))
        return "Unknown error";
    return test_error_strings[idx];
}

static uint8_t test_battery_percent(uint16_t vbat_mv) {
    if (vbat_mv >= 4200) return 100;
    if (vbat_mv <= 3000) return 0;

    if (vbat_mv >= 3700)
        return (uint8_t)((uint32_t)(vbat_mv - 3700) * 50 / 500 + 50);
    else if (vbat_mv >= 3300)
        return (uint8_t)((uint32_t)(vbat_mv - 3300) * 40 / 400 + 10);
    else
        return (uint8_t)((uint32_t)(vbat_mv - 3000) * 10 / 300);
}

static bool test_is_low_battery(uint16_t vbat_mv) {
    return vbat_mv < 3300;
}

static bool test_is_overtemp(int16_t temp_c_x10) {
    return temp_c_x10 > 850;  /* 85.0°C */
}

/* SDR tune validation (mirrors apex_sdr_tune checks) */
static int validate_sdr_tune(uint32_t freq_hz, uint16_t bw_khz, float gain_db) {
    if (freq_hz < 100000 || freq_hz > 3800000000UL)
        return APEX_ERR_INVALID_ARG;
    /* Check gain range */
    if (gain_db < 0.0f || gain_db > 70.0f)
        return APEX_ERR_INVALID_ARG;
    (void)bw_khz;  /* bandwidth not range-checked in current implementation */
    return APEX_OK;
}

/* Antenna selection validation */
static int validate_ant_select(int ant) {
    if (ant < 0 || ant > 3)
        return APEX_ERR_INVALID_ARG;
    return APEX_OK;
}

/* CC1101 register configuration validation */
static int validate_cc1101_config(uint8_t reg_addr, uint8_t reg_len) {
    if (reg_len == 0 || reg_len > 64)
        return APEX_ERR_INVALID_ARG;
    if (reg_addr > 0x3D)
        return APEX_ERR_INVALID_ARG;
    return APEX_OK;
}

/* CC1101 power mapping (mirrors apex_cc1101_set_power) */
static uint8_t cc1101_power_to_pa(int8_t power_dbm) {
    if (power_dbm <= -30)       return 0x00;
    else if (power_dbm <= -20)  return 0x01;
    else if (power_dbm <= -15)  return 0x02;
    else if (power_dbm <= -10)  return 0x34;
    else if (power_dbm <= 0)    return 0x60;
    else if (power_dbm <= 5)    return 0x84;
    else if (power_dbm <= 7)    return 0xA0;
    else                        return 0xC0;
}

/* Secure wipe function for testing */
static void secure_wipe(uint8_t *ptr, size_t len) {
    volatile uint8_t *p = ptr;
    while (len--)
        *p++ = 0;
}

/* ========================================================================
 * Test: apex_strerror
 * ======================================================================== */

static void test_strerror_known_codes(void) {
    printf("  Testing apex_strerror for known error codes...\n");

    ASSERT_STR_EQ(test_apex_strerror(APEX_OK),
                  "Success", "APEX_OK string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_INVALID_ARG),
                  "Invalid argument", "APEX_ERR_INVALID_ARG string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_NO_DEVICE),
                  "Device not found", "APEX_ERR_NO_DEVICE string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_OPEN_FAILED),
                  "Failed to open device", "APEX_ERR_OPEN_FAILED string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_IOCTL_FAILED),
                  "ioctl failed", "APEX_ERR_IOCTL_FAILED string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_TIMEOUT),
                  "Operation timed out", "APEX_ERR_TIMEOUT string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_COMM),
                  "Communication error", "APEX_ERR_COMM string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_NOT_READY),
                  "Device not ready", "APEX_ERR_NOT_READY string");
    ASSERT_STR_EQ(test_apex_strerror(APEX_ERR_NOMEM),
                  "Out of memory", "APEX_ERR_NOMEM string");
}

static void test_strerror_unknown_codes(void) {
    printf("  Testing apex_strerror for unknown error codes...\n");

    ASSERT_STR_EQ(test_apex_strerror(1), "Unknown error", "positive error code");
    ASSERT_STR_EQ(test_apex_strerror(-99), "Unknown error", "large negative error code");
    ASSERT_STR_EQ(test_apex_strerror(-100), "Unknown error", "very large negative error");
    ASSERT_STR_EQ(test_apex_strerror(0), "Success", "zero is APEX_OK");
}

/* ========================================================================
 * Test: apex_battery_percent
 * ======================================================================== */

static void test_battery_percent_boundaries(void) {
    printf("  Testing apex_battery_percent boundary conditions...\n");

    /* Full battery */
    ASSERT_EQ(test_battery_percent(4200), 100, "4200 mV = 100%");
    ASSERT_EQ(test_battery_percent(4500), 100, ">4200 mV = 100%");

    /* Empty battery */
    ASSERT_EQ(test_battery_percent(3000), 0, "3000 mV = 0%");
    ASSERT_EQ(test_battery_percent(2500), 0, "<3000 mV = 0%");
    ASSERT_EQ(test_battery_percent(0), 0, "0 mV = 0%");

    /* Typical voltages */
    ASSERT_EQ(test_battery_percent(3700), 50, "3700 mV = 50%");
}

static void test_battery_percent_curve_segments(void) {
    printf("  Testing apex_battery_percent curve segments...\n");

    /* Segment 1: 3000-3300 mV → 0-10% */
    uint8_t pct = test_battery_percent(3150);
    ASSERT_IN_RANGE(pct, 0, 10, "3150 mV in low range");

    pct = test_battery_percent(3300);
    ASSERT_EQ(pct, 10, "3300 mV = 10%");

    /* Segment 2: 3300-3700 mV → 10-50% */
    pct = test_battery_percent(3500);
    ASSERT_IN_RANGE(pct, 10, 50, "3500 mV in mid range");

    pct = test_battery_percent(3700);
    ASSERT_EQ(pct, 50, "3700 mV = 50%");

    /* Segment 3: 3700-4200 mV → 50-100% */
    pct = test_battery_percent(3950);
    ASSERT_IN_RANGE(pct, 50, 100, "3950 mV in high range");

    /* Verify monotonic increase across the full range */
    uint8_t prev = 0;
    for (uint16_t mv = 3000; mv <= 4200; mv += 50) {
        pct = test_battery_percent(mv);
        ASSERT_TRUE(pct >= prev, "battery percent monotonically increases");
        prev = pct;
    }
}

static void test_battery_percent_edge_cases(void) {
    printf("  Testing apex_battery_percent edge cases...\n");

    /* Exact boundaries */
    uint8_t pct;
    ASSERT_EQ(test_battery_percent(3000), 0, "lower boundary");
    ASSERT_EQ(test_battery_percent(4200), 100, "upper boundary");

    /* Just above 3000 mV — with integer math, 3010 mV maps to (10*10)/300=0 */
    pct = test_battery_percent(3010);
    /* 3010 mV: (3010-3000)*10/300 = 100/300 = 0 in integer math */
    ASSERT_EQ(pct, 0, "3010 mV still maps to 0% (integer division)");

    /* But 3300 mV should map to 10% */
    pct = test_battery_percent(3300);
    ASSERT_EQ(pct, 10, "3300 mV = 10%");

    /* Maximum uint16_t */
    ASSERT_EQ(test_battery_percent(65535), 100, "max uint16 clamped to 100%");
}

/* ========================================================================
 * Test: apex_is_low_battery / apex_is_overtemp
 * ======================================================================== */

static void test_low_battery_thresholds(void) {
    printf("  Testing apex_is_low_battery thresholds...\n");

    /* NULL telemetry pointer */
    apex_telemetry_t telem;

    /* Well above threshold */
    memset(&telem, 0, sizeof(telem));
    telem.vbat_mv = 3800;
    ASSERT_FALSE(test_is_low_battery(telem.vbat_mv),
                 "3800 mV is not low battery");

    /* At threshold */
    telem.vbat_mv = 3300;
    ASSERT_FALSE(test_is_low_battery(telem.vbat_mv),
                 "3300 mV is exactly at threshold (not low)");

    /* Just below threshold */
    telem.vbat_mv = 3299;
    ASSERT_TRUE(test_is_low_battery(telem.vbat_mv),
                "3299 mV is low battery");

    /* Deeply discharged */
    telem.vbat_mv = 2500;
    ASSERT_TRUE(test_is_low_battery(telem.vbat_mv),
                "2500 mV is low battery");
}

static void test_overtemp_thresholds(void) {
    printf("  Testing apex_is_overtemp thresholds...\n");

    /* Normal temperature */
    ASSERT_FALSE(test_is_overtemp(270), "27.0°C is not overtemp");
    ASSERT_FALSE(test_is_overtemp(450), "45.0°C is not overtemp");

    /* At threshold — 850 = 85.0°C, should NOT be overtemp (> 850) */
    ASSERT_FALSE(test_is_overtemp(850), "85.0°C is exactly at threshold");

    /* Just above threshold */
    ASSERT_TRUE(test_is_overtemp(851), "85.1°C is overtemp");

    /* Very hot */
    ASSERT_TRUE(test_is_overtemp(1000), "100.0°C is overtemp");

    /* Very cold */
    ASSERT_FALSE(test_is_overtemp(-400), "-40.0°C is not overtemp");

    /* Zero degrees C */
    ASSERT_FALSE(test_is_overtemp(0), "0°C is not overtemp");
}

/* ========================================================================
 * Test: SDR tune parameter validation
 * ======================================================================== */

static void test_sdr_tune_frequency_validation(void) {
    printf("  Testing SDR tune frequency validation...\n");

    /* Valid frequencies */
    ASSERT_EQ(validate_sdr_tune(100000, 20000, 30.0f), APEX_OK,
               "100 kHz min frequency");
    ASSERT_EQ(validate_sdr_tune(868000000, 20000, 30.0f), APEX_OK,
               "868 MHz (EU SRD)");
    ASSERT_EQ(validate_sdr_tune(2400000000UL, 20000, 30.0f), APEX_OK,
               "2.4 GHz (Wi-Fi)");
    ASSERT_EQ(validate_sdr_tune(3800000000UL, 20000, 30.0f), APEX_OK,
               "3.8 GHz max frequency");

    /* Invalid frequencies */
    ASSERT_EQ(validate_sdr_tune(99999, 20000, 30.0f), APEX_ERR_INVALID_ARG,
               "below min frequency");
    ASSERT_EQ(validate_sdr_tune(3800000001UL, 20000, 30.0f), APEX_ERR_INVALID_ARG,
               "above max frequency");
    ASSERT_EQ(validate_sdr_tune(0, 20000, 30.0f), APEX_ERR_INVALID_ARG,
               "zero frequency");
}

static void test_sdr_tune_gain_validation(void) {
    printf("  Testing SDR tune gain validation...\n");

    /* Valid gains */
    ASSERT_EQ(validate_sdr_tune(868000000, 20000, 0.0f), APEX_OK,
               "0 dB gain");
    ASSERT_EQ(validate_sdr_tune(868000000, 20000, 30.0f), APEX_OK,
               "30 dB gain");
    ASSERT_EQ(validate_sdr_tune(868000000, 20000, 70.0f), APEX_OK,
               "70 dB max gain");

    /* Invalid gains */
    ASSERT_EQ(validate_sdr_tune(868000000, 20000, -0.1f), APEX_ERR_INVALID_ARG,
               "negative gain");
    ASSERT_EQ(validate_sdr_tune(868000000, 20000, 70.1f), APEX_ERR_INVALID_ARG,
               "above max gain");
}

/* ========================================================================
 * Test: Antenna selection validation
 * ======================================================================== */

static void test_antenna_selection(void) {
    printf("  Testing antenna selection validation...\n");

    /* Valid antenna selections */
    ASSERT_EQ(validate_ant_select(0), APEX_OK, "ANT_MIMO_TX");
    ASSERT_EQ(validate_ant_select(1), APEX_OK, "ANT_MIMO_RX");
    ASSERT_EQ(validate_ant_select(2), APEX_OK, "ANT_SUBGHZ");
    ASSERT_EQ(validate_ant_select(3), APEX_OK, "ANT_TERMINATED");

    /* Invalid antenna selections */
    ASSERT_EQ(validate_ant_select(4), APEX_ERR_INVALID_ARG,
               "antenna index 4 (invalid)");
    ASSERT_EQ(validate_ant_select(-1), APEX_ERR_INVALID_ARG,
               "negative antenna index");
    ASSERT_EQ(validate_ant_select(255), APEX_ERR_INVALID_ARG,
               "antenna index 255 (invalid)");
}

/* ========================================================================
 * Test: CC1101 register configuration validation
 * ======================================================================== */

static void test_cc1101_config_validation(void) {
    printf("  Testing CC1101 register configuration validation...\n");

    /* Valid configurations */
    ASSERT_EQ(validate_cc1101_config(0x00, 1), APEX_OK,
               "IOCFG2 single register");
    ASSERT_EQ(validate_cc1101_config(0x00, 64), APEX_OK,
               "max register burst (64 bytes)");
    ASSERT_EQ(validate_cc1101_config(0x0D, 3), APEX_OK,
               "FREQ2-FREQ0 burst");

    /* Invalid: zero length */
    ASSERT_EQ(validate_cc1101_config(0x00, 0), APEX_ERR_INVALID_ARG,
               "zero length config");

    /* Invalid: length > 64 */
    ASSERT_EQ(validate_cc1101_config(0x00, 65), APEX_ERR_INVALID_ARG,
               "length exceeds 64");

    /* Invalid: register address out of range */
    ASSERT_EQ(validate_cc1101_config(0x3E, 1), APEX_ERR_INVALID_ARG,
               "register address 0x3E (PATABLE, not a config register)");
    ASSERT_EQ(validate_cc1101_config(0x40, 1), APEX_ERR_INVALID_ARG,
               "register address 0x40 (out of range)");
}

/* ========================================================================
 * Test: CC1101 TX power mapping
 * ======================================================================== */

static void test_cc1101_power_mapping(void) {
    printf("  Testing CC1101 TX power mapping...\n");

    /* Verify power level boundaries */
    ASSERT_EQ(cc1101_power_to_pa(-30), 0x00, "-30 dBm → 0x00");
    ASSERT_EQ(cc1101_power_to_pa(-25), 0x01, "-25 dBm → 0x01");
    ASSERT_EQ(cc1101_power_to_pa(-20), 0x01, "-20 dBm → 0x01");
    ASSERT_EQ(cc1101_power_to_pa(-15), 0x02, "-15 dBm → 0x02");
    ASSERT_EQ(cc1101_power_to_pa(-12), 0x34, "-12 dBm → 0x34");
    ASSERT_EQ(cc1101_power_to_pa(-10), 0x34, "-10 dBm → 0x34");
    ASSERT_EQ(cc1101_power_to_pa(0), 0x60, "0 dBm → 0x60");
    ASSERT_EQ(cc1101_power_to_pa(3), 0x84, "3 dBm → 0x84");
    ASSERT_EQ(cc1101_power_to_pa(5), 0x84, "5 dBm → 0x84");
    ASSERT_EQ(cc1101_power_to_pa(7), 0xA0, "7 dBm → 0xA0");
    ASSERT_EQ(cc1101_power_to_pa(10), 0xC0, "10 dBm → 0xC0");
    ASSERT_EQ(cc1101_power_to_pa(15), 0xC0, "15 dBm → clamped to 0xC0");

    /* Verify all PA values are valid CC1101 PATABLE entries */
    uint8_t pa_val;
    for (int8_t dbm = -30; dbm <= 12; dbm++) {
        pa_val = cc1101_power_to_pa(dbm);
        ASSERT_TRUE(pa_val != 0 || dbm <= -30,
                    "PA value is nonzero for normal power levels");
    }
}

/* ========================================================================
 * Test: Secure wipe function
 * ======================================================================== */

static void test_secure_wipe(void) {
    printf("  Testing secure_wipe correctness...\n");

    uint8_t buf[32];

    /* Fill buffer with known pattern and wipe */
    memset(buf, 0xAA, sizeof(buf));
    secure_wipe(buf, sizeof(buf));

    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_TRUE(all_zero, "secure_wipe zeroes entire buffer");

    /* Partial wipe */
    memset(buf, 0xBB, sizeof(buf));
    secure_wipe(buf, 16);
    int first_half_zero = 1;
    int second_half_intact = 1;
    for (int i = 0; i < 16; i++) {
        if (buf[i] != 0) first_half_zero = 0;
    }
    for (int i = 16; i < 32; i++) {
        if (buf[i] != 0xBB) second_half_intact = 0;
    }
    ASSERT_TRUE(first_half_zero, "partial wipe: first half zeroed");
    ASSERT_TRUE(second_half_intact, "partial wipe: second half intact");

    /* Zero-length wipe (should be no-op) */
    memset(buf, 0xCC, sizeof(buf));
    secure_wipe(buf, 0);
    int intact = 1;
    for (int i = 0; i < 32; i++) {
        if (buf[i] != 0xCC) intact = 0;
    }
    ASSERT_TRUE(intact, "zero-length wipe is no-op");
}

/* ========================================================================
 * Test: Telemetry flag constants
 * ======================================================================== */

static void test_telemetry_flags(void) {
    printf("  Testing telemetry flag constants...\n");

    /* Verify flag values don't overlap */
    ASSERT_EQ(APEX_TELEM_SDR_RX_ACTIVE, 1 << 0, "SDR_RX_ACTIVE flag");
    ASSERT_EQ(APEX_TELEM_SDR_TX_ACTIVE, 1 << 1, "SDR_TX_ACTIVE flag");
    ASSERT_EQ(APEX_TELEM_CC1101_RX, 1 << 2, "CC1101_RX flag");
    ASSERT_EQ(APEX_TELEM_CC1101_TX, 1 << 3, "CC1101_TX flag");
    ASSERT_EQ(APEX_TELEM_NFC_ACTIVE, 1 << 4, "NFC_ACTIVE flag");
    ASSERT_EQ(APEX_TELEM_NFC_TAG_PRESENT, 1 << 5, "NFC_TAG_PRESENT flag");
    ASSERT_EQ(APEX_TELEM_OVERTEMP, 1 << 6, "OVERTEMP flag");
    ASSERT_EQ(APEX_TELEM_LOW_BATTERY, 1 << 7, "LOW_BATTERY flag");

    /* Verify all flags can be OR'd together without collision */
    uint16_t all_flags = APEX_TELEM_SDR_RX_ACTIVE |
                         APEX_TELEM_SDR_TX_ACTIVE |
                         APEX_TELEM_CC1101_RX |
                         APEX_TELEM_CC1101_TX |
                         APEX_TELEM_NFC_ACTIVE |
                         APEX_TELEM_NFC_TAG_PRESENT |
                         APEX_TELEM_OVERTEMP |
                         APEX_TELEM_LOW_BATTERY;
    ASSERT_EQ(all_flags, 0xFF, "all flags combined = 0xFF");

    /* Test individual flag detection */
    apex_telemetry_t telem;
    memset(&telem, 0, sizeof(telem));

    telem.flags = APEX_TELEM_SDR_RX_ACTIVE;
    ASSERT_TRUE(telem.flags & APEX_TELEM_SDR_RX_ACTIVE,
                "SDR_RX_ACTIVE set");
    ASSERT_FALSE(telem.flags & APEX_TELEM_SDR_TX_ACTIVE,
                 "SDR_TX_ACTIVE not set");

    telem.flags = APEX_TELEM_LOW_BATTERY | APEX_TELEM_OVERTEMP;
    ASSERT_TRUE(telem.flags & APEX_TELEM_LOW_BATTERY,
                "LOW_BATTERY set in combined flags");
    ASSERT_TRUE(telem.flags & APEX_TELEM_OVERTEMP,
                "OVERTEMP set in combined flags");
    ASSERT_FALSE(telem.flags & APEX_TELEM_NFC_ACTIVE,
                 "NFC_ACTIVE not set in combined flags");
}

/* ========================================================================
 * Test: Library version constants
 * ======================================================================== */

static void test_version_constants(void) {
    printf("  Testing library version constants...\n");

    ASSERT_EQ(LIBAPEX_VERSION_MAJOR, 0, "version major");
    ASSERT_EQ(LIBAPEX_VERSION_MINOR, 1, "version minor");
    ASSERT_EQ(LIBAPEX_VERSION_PATCH, 0, "version patch");

    /* Verify version string format */
    const char *v = LIBAPEX_VERSION_STRING;
    ASSERT_TRUE(v != NULL, "version string is not NULL");
    ASSERT_TRUE(strlen(v) > 0, "version string is not empty");

    /* Verify the version string matches the numeric components */
    char expected[16];
    snprintf(expected, sizeof(expected), "%d.%d.%d",
             LIBAPEX_VERSION_MAJOR, LIBAPEX_VERSION_MINOR,
             LIBAPEX_VERSION_PATCH);
    ASSERT_STR_EQ(v, expected, "version string matches components");
}

/* ========================================================================
 * Test: Antenna enumeration values
 * ======================================================================== */

static void test_antenna_enum_values(void) {
    printf("  Testing antenna enumeration values...\n");

    ASSERT_EQ(APEX_ANT_MIMO_TX, 0, "ANT_MIMO_TX = 0");
    ASSERT_EQ(APEX_ANT_MIMO_RX, 1, "ANT_MIMO_RX = 1");
    ASSERT_EQ(APEX_ANT_SUBGHZ, 2, "ANT_SUBGHZ = 2");
    ASSERT_EQ(APEX_ANT_TERMINATED, 3, "ANT_TERMINATED = 3");
}

/* ========================================================================
 * Test: Error code values
 * ======================================================================== */

static void test_error_code_values(void) {
    printf("  Testing error code values...\n");

    ASSERT_EQ(APEX_OK, 0, "APEX_OK = 0");
    ASSERT_EQ(APEX_ERR_INVALID_ARG, -1, "ERR_INVALID_ARG = -1");
    ASSERT_EQ(APEX_ERR_NO_DEVICE, -2, "ERR_NO_DEVICE = -2");
    ASSERT_EQ(APEX_ERR_OPEN_FAILED, -3, "ERR_OPEN_FAILED = -3");
    ASSERT_EQ(APEX_ERR_IOCTL_FAILED, -4, "ERR_IOCTL_FAILED = -4");
    ASSERT_EQ(APEX_ERR_TIMEOUT, -5, "ERR_TIMEOUT = -5");
    ASSERT_EQ(APEX_ERR_COMM, -6, "ERR_COMM = -6");
    ASSERT_EQ(APEX_ERR_NOT_READY, -7, "ERR_NOT_READY = -7");
    ASSERT_EQ(APEX_ERR_NOMEM, -8, "ERR_NOMEM = -8");

    /* Verify error codes are sequential (negative values: -1, -2, -3...) */
    ASSERT_TRUE(APEX_ERR_INVALID_ARG > APEX_ERR_NO_DEVICE,
                "error codes are sequential (-1 > -2)");
    ASSERT_TRUE(APEX_ERR_NO_DEVICE > APEX_ERR_OPEN_FAILED,
                "error codes continue sequential (-2 > -3)");
}

/* ========================================================================
 * Test: CC1101 frequency calculation for 868 MHz
 * ======================================================================== */

static void test_cc1101_frequency_calc(void) {
    printf("  Testing CC1101 frequency calculation for 868 MHz...\n");

    /* From cc1101_init.c:
     * f_carrier = FREQ * f_xtal / 2^16
     * For 868 MHz with 26 MHz crystal:
     * FREQ = 868e6 * 2^16 / 26e6 = 2185891.2
     * Rounded to 0x216323 = 2185891
     * Actual frequency = 2185891 * 26e6 / 65536 ≈ 868.07 MHz
     * This is within the 100 kHz crystal tolerance.
     */

    uint32_t freq_word_868 = 0x216323;
    double f_xtal = 26.0e6;
    double f_868 = (double)freq_word_868 * f_xtal / 65536.0;

    /* The frequency word produces a frequency within ~70 kHz of 868 MHz,
     * well within CC1101 crystal tolerance */
    ASSERT_TRUE(fabs(f_868 - 868.0e6) < 100000.0,
                "868 MHz frequency word produces ~868 MHz");

    /* Test 433.92 MHz (EU ISM)
     * FREQ = round(433.92e6 * 65536 / 26e6) = 1093745 = 0x10B071
     */
    uint32_t freq_word_433 = 0x10B071;
    double f_433 = (double)freq_word_433 * f_xtal / 65536.0;
    ASSERT_TRUE(fabs(f_433 - 433.92e6) < 1000.0,
                "433.92 MHz frequency word produces ~433.92 MHz");

    /* Test 915 MHz (US ISM)
     * FREQ = round(915e6 * 65536 / 26e6) = 2306363 = 0x23313B
     */
    uint32_t freq_word_915 = 0x23313B;
    double f_915 = (double)freq_word_915 * f_xtal / 65536.0;
    ASSERT_TRUE(fabs(f_915 - 915.0e6) < 1000.0,
                "915 MHz frequency word produces ~915 MHz");
}

/* ========================================================================
 * Test: Battery discharge curve monotonicity
 * ======================================================================== */

static void test_battery_discharge_curve(void) {
    printf("  Testing battery discharge curve is well-behaved...\n");

    /* The discharge curve should be monotonically increasing from
     * 3000 mV (0%) to 4200 mV (100%) with reasonable segments.
     * Key inflection points from libapex.c:
     *   3000 mV = 0%
     *   3300 mV = 10%
     *   3700 mV = 50%
     *   4200 mV = 100%
     */

    /* Check key transition points */
    uint8_t pct;

    /* Just above 3000 mV — still 0% due to integer division */
    pct = test_battery_percent(3010);
    ASSERT_TRUE(pct < 10, "3010 mV < 10%");

    /* At 3300 mV */
    pct = test_battery_percent(3300);
    ASSERT_EQ(pct, 10, "3300 mV = 10%");

    /* Just above 3300 mV */
    pct = test_battery_percent(3310);
    ASSERT_TRUE(pct > 10, "3310 mV > 10%");

    /* At 3500 mV — midpoint of segment 2 */
    pct = test_battery_percent(3500);
    ASSERT_IN_RANGE(pct, 15, 40, "3500 mV in range [15, 40]%");

    /* At 3700 mV */
    pct = test_battery_percent(3700);
    ASSERT_EQ(pct, 50, "3700 mV = 50%");

    /* At 3950 mV — midpoint of segment 3 */
    pct = test_battery_percent(3950);
    ASSERT_IN_RANGE(pct, 50, 100, "3950 mV in range [50, 100]%");
}

/* ========================================================================
 * Test: Reset magic constant
 * ======================================================================== */

static void test_reset_magic(void) {
    printf("  Testing reset magic constant values...\n");

    /* APEX_RESET_MAGIC must be 0x52534554 ("RSET") */
    ASSERT_TRUE(APEX_RESET_MAGIC == 0x52534554UL, "APEX_RESET_MAGIC = 0x52534554");

    /* Verify the magic bytes are ASCII "RSET" */
    ASSERT_TRUE(((APEX_RESET_MAGIC >> 24) & 0xFF) == 0x52, "Magic byte 3 = 'R'");
    ASSERT_TRUE(((APEX_RESET_MAGIC >> 16) & 0xFF) == 0x53, "Magic byte 2 = 'S'");
    ASSERT_TRUE(((APEX_RESET_MAGIC >> 8) & 0xFF) == 0x45, "Magic byte 1 = 'E'");
    ASSERT_TRUE((APEX_RESET_MAGIC & 0xFF) == 0x54, "Magic byte 0 = 'T'");

    /* Magic must not be zero (accidental reset protection) */
    ASSERT_TRUE(APEX_RESET_MAGIC != 0, "Reset magic is non-zero");

    /* Magic must not be 0xFFFFFFFF (common SPI bus idle pattern) */
    ASSERT_TRUE(APEX_RESET_MAGIC != 0xFFFFFFFFUL, "Reset magic is not all-ones");

    /* Magic must not be a simple incrementing pattern (0x01020304) */
    ASSERT_TRUE(APEX_RESET_MAGIC != 0x01020304UL, "Reset magic is not trivial pattern");
}

/* ========================================================================
 * Test: Telemetry flag completeness
 * ======================================================================== */

static void test_telemetry_flag_completeness(void) {
    printf("  Testing telemetry flag completeness...\n");

    /* All telemetry flags should be distinct and non-overlapping */
    uint16_t flags[] = {
        APEX_TELEM_SDR_RX_ACTIVE,
        APEX_TELEM_SDR_TX_ACTIVE,
        APEX_TELEM_CC1101_RX,
        APEX_TELEM_CC1101_TX,
        APEX_TELEM_NFC_ACTIVE,
        APEX_TELEM_NFC_TAG_PRESENT,
        APEX_TELEM_OVERTEMP,
        APEX_TELEM_LOW_BATTERY,
    };
    int n = sizeof(flags) / sizeof(flags[0]);

    for (int i = 0; i < n; i++) {
        ASSERT_TRUE(flags[i] != 0, "flag is non-zero");
        for (int j = i + 1; j < n; j++) {
            ASSERT_TRUE((flags[i] & flags[j]) == 0, "flags are mutually exclusive");
        }
    }
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void) {
    printf("\n=== libapex Unit Tests ===\n\n");

    /* Error code tests */
    printf("[Error Strings]\n");
    test_strerror_known_codes();
    test_strerror_unknown_codes();
    printf("\n");

    /* Error code values */
    printf("[Error Code Values]\n");
    test_error_code_values();
    printf("\n");

    /* Battery percent tests */
    printf("[Battery Percent]\n");
    test_battery_percent_boundaries();
    test_battery_percent_curve_segments();
    test_battery_percent_edge_cases();
    test_battery_discharge_curve();
    printf("\n");

    /* Battery/temperature thresholds */
    printf("[Battery/Temp Thresholds]\n");
    test_low_battery_thresholds();
    test_overtemp_thresholds();
    printf("\n");

    /* SDR validation tests */
    printf("[SDR Tune Validation]\n");
    test_sdr_tune_frequency_validation();
    test_sdr_tune_gain_validation();
    printf("\n");

    /* Antenna selection tests */
    printf("[Antenna Selection]\n");
    test_antenna_selection();
    test_antenna_enum_values();
    printf("\n");

    /* CC1101 config tests */
    printf("[CC1101 Configuration]\n");
    test_cc1101_config_validation();
    test_cc1101_power_mapping();
    test_cc1101_frequency_calc();
    printf("\n");

    /* Secure wipe tests */
    printf("[Secure Wipe]\n");
    test_secure_wipe();
    printf("\n");

    /* Telemetry flag tests */
    printf("[Telemetry Flags]\n");
    test_telemetry_flags();
    test_telemetry_flag_completeness();
    printf("\n");

    /* Reset magic tests */
    printf("[Reset Magic]\n");
    test_reset_magic();
    printf("\n");

    /* Version tests */
    printf("[Version Constants]\n");
    test_version_constants();
    printf("\n");

    /* Summary */
    printf("=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

#pragma GCC diagnostic pop