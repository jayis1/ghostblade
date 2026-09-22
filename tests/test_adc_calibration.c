/*
 * test_adc_calibration.c — Unit Tests for ADC Calibration Module
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the ADC calibration coefficient calculations, voltage divider
 * compensation, two-point calibration algorithm, and self-test logic
 * used in the RP2350B firmware's adc_calibration module.
 *
 * Build (standalone, no cmocka):
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_adc_calibration test_adc_calibration.c
 *
 * Build (with cmocka):
 *   gcc -Wall -Wextra -std=c11 -lcmocka -o test_adc_calibration test_adc_calibration.c
 *
 * Run:
 *   ./test_adc_calibration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ── Test framework abstraction ───────────────────────────────────────────── */

#ifdef NO_CMOCKA

/* Minimal test framework for standalone compilation */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_INT_EQ(expected, actual) do {                                \
    g_tests_run++;                                                          \
    if ((expected) != (actual)) {                                           \
        fprintf(stderr, "  FAIL: %s:%d: expected %d, got %d\n",            \
                __FILE__, __LINE__, (int)(expected), (int)(actual));        \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_INT64_EQ(expected, actual) do {                             \
    g_tests_run++;                                                          \
    if ((expected) != (actual)) {                                           \
        fprintf(stderr, "  FAIL: %s:%d: expected %lld, got %lld\n",        \
                __FILE__, __LINE__, (long long)(expected),                  \
                (long long)(actual));                                        \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_UINT16_EQ(expected, actual) do {                             \
    g_tests_run++;                                                          \
    if ((expected) != (actual)) {                                           \
        fprintf(stderr, "  FAIL: %s:%d: expected %u, got %u\n",            \
                __FILE__, __LINE__, (unsigned)(expected),                   \
                (unsigned)(actual));                                         \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_TRUE(cond) do {                                              \
    g_tests_run++;                                                          \
    if (!(cond)) {                                                          \
        fprintf(stderr, "  FAIL: %s:%d: condition false: %s\n",            \
                __FILE__, __LINE__, #cond);                                 \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_FALSE(cond) do {                                             \
    g_tests_run++;                                                          \
    if ((cond)) {                                                           \
        fprintf(stderr, "  FAIL: %s:%d: condition true: %s\n",             \
                __FILE__, __LINE__, #cond);                                 \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_INT16_EQ(expected, actual) do {                              \
    g_tests_run++;                                                          \
    if ((expected) != (actual)) {                                           \
        fprintf(stderr, "  FAIL: %s:%d: expected %d, got %d\n",            \
                __FILE__, __LINE__, (int)(expected), (int)(actual));       \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define RUN_TEST(func) do {                                                 \
    fprintf(stderr, "  %-50s ", #func);                                     \
    func();                                                                 \
    fprintf(stderr, "PASS\n");                                             \
} while (0)

#define TEST_SUITE_START() fprintf(stderr, "ADC Calibration Unit Tests\n")
#define TEST_SUITE_END() do {                                               \
    fprintf(stderr, "\nResults: %d passed, %d failed, %d total\n",         \
            g_tests_passed, g_tests_failed, g_tests_run);                  \
    return g_tests_failed > 0 ? 1 : 0;                                     \
} while (0)

#else
#include <cmocka.h>
#define ASSERT_INT_EQ(expected, actual) assert_int_equal((actual), (expected))
#define ASSERT_INT64_EQ(expected, actual) assert_int_equal((actual), (expected))
#define ASSERT_UINT16_EQ(expected, actual) assert_int_equal((actual), (expected))
#define ASSERT_TRUE(cond) assert_true(cond)
#define ASSERT_FALSE(cond) assert_false(cond)
#define ASSERT_INT16_EQ(expected, actual) assert_int_equal((actual), (expected))
#define RUN_TEST(func) cmocka_unit_test(func)
#define TEST_SUITE_START()
#define TEST_SUITE_END()
#endif

/* ── Constants from adc_calibration.h ──────────────────────────────────────── */

#define ADC_CAL_REF_MV             3300
#define ADC_CAL_FULL_SCALE         4095
#define ADC_CAL_R1_OHM             100000UL
#define ADC_CAL_R2_OHM             33000UL
#define ADC_CAL_VBAT_NUMERATOR     3249700UL
#define ADC_CAL_VBAT_DENOMINATOR   1000000UL
#define ADC_CAL_VBAT_ROUNDING      500UL
#define ADC_CAL_MAGIC               0xADCA
#define FLASH_CAL_OFFSET            0x10F000UL

/* ── Calibration coefficient structure ─────────────────────────────────────── */

struct adc_cal_coeffs {
    int16_t  vbat_offset_mv;
    uint16_t vbat_gain_x1000;
    int16_t  temp_offset_dcx10;
    uint16_t temp_gain_x1000;
    bool     calibrated;
    uint8_t  cal_version;
    uint16_t reserved;
};

/* ── Flash calibration record structure ────────────────────────────────────── */

struct adc_cal_record {
    uint16_t            magic;
    uint8_t             version;
    struct adc_cal_coeffs coeffs;
    uint32_t            timestamp;
    uint8_t             checksum;
} __attribute__((packed));

/* ── Inline implementations of calibration math ────────────────────────────── */

/**
 * adc_raw_to_mv — Convert raw ADC value to millivolts (nominal)
 *
 * Uses the voltage divider formula with nominal component values.
 * VBAT = ADC_raw × VREF × (R1 + R2) / R2 / 4095
 *
 * In integer arithmetic with rounding:
 * VBAT_mV = (ADC_raw × 3249700 + 500) / 1000000
 */
static uint16_t adc_raw_to_mv(uint16_t adc_raw)
{
    uint32_t result = ((uint32_t)adc_raw * ADC_CAL_VBAT_NUMERATOR +
                       ADC_CAL_VBAT_ROUNDING) / ADC_CAL_VBAT_DENOMINATOR;
    return (uint16_t)result;
}

/**
 * adc_cal_apply_vbat — Apply calibration to raw VBAT reading
 */
static uint16_t adc_cal_apply_vbat(uint16_t raw_mv,
                                     const struct adc_cal_coeffs *coeffs)
{
    int32_t calibrated;

    calibrated = (int32_t)raw_mv + coeffs->vbat_offset_mv;
    calibrated = (calibrated * (int32_t)coeffs->vbat_gain_x1000) / 1000;

    if (calibrated < 0)
        calibrated = 0;
    if (calibrated > 65535)
        calibrated = 65535;

    return (uint16_t)calibrated;
}

/**
 * adc_cal_apply_temp — Apply calibration to raw temperature reading
 */
static int16_t adc_cal_apply_temp(int16_t raw_temp_c_x10,
                                   const struct adc_cal_coeffs *coeffs)
{
    int32_t calibrated;

    calibrated = ((int32_t)raw_temp_c_x10 *
                   (int32_t)coeffs->temp_gain_x1000) / 1000;
    calibrated += coeffs->temp_offset_dcx10;

    return (int16_t)calibrated;
}

/**
 * compute_checksum — Simple additive checksum for calibration record
 */
static uint8_t compute_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/* ── Test functions ────────────────────────────────────────────────────────── */

/**
 * test_voltage_divider_nominal — Test nominal voltage divider conversion
 *
 * Verifies that the ADC-to-millivolt conversion matches expected values
 * at key battery voltages using nominal component values.
 */
static void test_voltage_divider_nominal(void)
{
    /* At VBAT = 3.700V, ADC reads:
     *   V_adc = VBAT × R2 / (R1 + R2) = 3700 × 33000 / 133000 ≈ 918.8 mV
     *   ADC_raw = V_adc × 4095 / 3300 ≈ 1140
     *   VBAT_mV = (1140 × 3249700 + 500) / 1000000 ≈ 3707 mV
     *
     * The slight error is due to integer rounding.
     */
    uint16_t result;

    /* Test at ADC = 0 (VBAT = 0V, battery disconnected) */
    result = adc_raw_to_mv(0);
    ASSERT_UINT16_EQ(0, result);

    /* Test at ADC = 1140 (~3.7V battery) */
    result = adc_raw_to_mv(1140);
    /* 1140 × 3249700 + 500 = 3704658400 + 500 = 3704658900 */
    /* 3704658900 / 1000000 = 3704 mV */
    ASSERT_TRUE(result >= 3690 && result <= 3720);

    /* Test at ADC = 419 (VBAT ~3.0V, deep discharge) */
    /* 419 × 3249700 + 500 = 1361644100 / 1000000 = 1361 mV */
    result = adc_raw_to_mv(419);
    ASSERT_TRUE(result >= 1350 && result <= 1380);

    /* Test at full scale ADC = 4095 (VBAT > 4.2V, should not happen in practice) */
    result = adc_raw_to_mv(4095);
    /* 4095 × 3249700 = 13307521500 + 500 = 13307522000 / 1000000 = 13307 mV */
    ASSERT_TRUE(result >= 13300);

    /* Test at ADC = 1298 (VBAT ~4.2V, full charge) */
    result = adc_raw_to_mv(1298);
    /* 1298 × 3249700 = 4217929100 + 500 = 4217929600 / 1000000 = 4217 mV */
    ASSERT_TRUE(result >= 4200 && result <= 4240);
}

/**
 * test_calibration_offset_positive — Test positive offset correction
 *
 * Verifies that a positive offset shifts the voltage reading up.
 */
static void test_calibration_offset_positive(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 30,     /* +30 mV offset */
        .vbat_gain_x1000 = 1000,  /* Unity gain */
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* Raw reading of 3700 mV with +30 mV offset should give 3730 mV */
    uint16_t result = adc_cal_apply_vbat(3700, &coeffs);
    ASSERT_UINT16_EQ(3730, result);

    /* Raw reading of 3000 mV with +30 mV offset should give 3030 mV */
    result = adc_cal_apply_vbat(3000, &coeffs);
    ASSERT_UINT16_EQ(3030, result);
}

/**
 * test_calibration_offset_negative — Test negative offset correction
 *
 * Verifies that a negative offset shifts the voltage reading down.
 */
static void test_calibration_offset_negative(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = -20,    /* -20 mV offset */
        .vbat_gain_x1000 = 1000,  /* Unity gain */
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* Raw reading of 3700 mV with -20 mV offset should give 3680 mV */
    uint16_t result = adc_cal_apply_vbat(3700, &coeffs);
    ASSERT_UINT16_EQ(3680, result);

    /* Raw reading of 4200 mV with -20 mV offset should give 4180 mV */
    result = adc_cal_apply_vbat(4200, &coeffs);
    ASSERT_UINT16_EQ(4180, result);
}

/**
 * test_calibration_gain_correction — Test gain correction
 *
 * Verifies that gain correction scales the voltage reading.
 */
static void test_calibration_gain_correction(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 0,
        .vbat_gain_x1000 = 1020,  /* +2% gain */
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* 3700 mV × 1.020 = 3774 mV */
    uint16_t result = adc_cal_apply_vbat(3700, &coeffs);
    ASSERT_UINT16_EQ(3774, result);

    /* 3000 mV × 1.020 = 3060 mV */
    result = adc_cal_apply_vbat(3000, &coeffs);
    ASSERT_UINT16_EQ(3060, result);
}

/**
 * test_calibration_combined_offset_gain — Test combined offset and gain
 *
 * Verifies that offset and gain are applied in the correct order:
 * first offset, then gain. The formula is:
 *   calibrated = (raw + offset) × gain / 1000
 */
static void test_calibration_combined_offset_gain(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 15,     /* +15 mV offset */
        .vbat_gain_x1000 = 980,   /* -2% gain */
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* (3700 + 15) × 980 / 1000 = 3715 × 980 / 1000 = 3640700 / 1000 = 3640 mV */
    uint16_t result = adc_cal_apply_vbat(3700, &coeffs);
    ASSERT_UINT16_EQ(3640, result);

    /* (4200 + 15) × 980 / 1000 = 4215 × 980 / 1000 = 4130700 / 1000 = 4130 mV */
    result = adc_cal_apply_vbat(4200, &coeffs);
    ASSERT_UINT16_EQ(4130, result);
}

/**
 * test_calibration_clamp_zero — Test clamping at zero
 *
 * Verifies that negative calibration results are clamped to zero.
 */
static void test_calibration_clamp_zero(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = -5000,  /* Very large negative offset */
        .vbat_gain_x1000 = 1000,
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* 100 mV - 5000 mV = -4900 mV, clamped to 0 */
    uint16_t result = adc_cal_apply_vbat(100, &coeffs);
    ASSERT_UINT16_EQ(0, result);
}

/**
 * test_calibration_clamp_max — Test clamping at maximum
 *
 * Verifies that very large calibration results are clamped to 65535.
 */
static void test_calibration_clamp_max(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 5000,    /* Very large positive offset */
        .vbat_gain_x1000 = 2000,  /* 2× gain */
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* (5000 + 5000) × 2000 / 1000 = 20000 mV, well within range */
    uint16_t result = adc_cal_apply_vbat(5000, &coeffs);
    ASSERT_UINT16_EQ(20000, result);

    /* Test extreme case: (60000 + 5000) × 2000 / 1000 = 130000 */
    /* This should clamp to 65535 */
    result = adc_cal_apply_vbat(60000, &coeffs);
    ASSERT_UINT16_EQ(65535, result);
}

/**
 * test_temperature_calibration — Test temperature offset and gain calibration
 */
static void test_temperature_calibration(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 0,
        .vbat_gain_x1000 = 1000,
        .temp_offset_dcx10 = 5,    /* +0.5°C offset */
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* 25.0°C (250 × 0.1) + 0.5°C offset = 25.5°C (255 × 0.1) */
    int16_t result = adc_cal_apply_temp(250, &coeffs);
    ASSERT_INT16_EQ(255, result);

    /* With negative offset: -1.0°C */
    coeffs.temp_offset_dcx10 = -10;
    result = adc_cal_apply_temp(250, &coeffs);
    ASSERT_INT16_EQ(240, result);  /* 25.0 - 1.0 = 24.0°C → 240 */

    /* With gain: 1.05× */
    coeffs.temp_offset_dcx10 = 0;
    coeffs.temp_gain_x1000 = 1050;
    result = adc_cal_apply_temp(250, &coeffs);
    /* 250 × 1050 / 1000 = 262 → 26.2°C */
    ASSERT_INT16_EQ(262, result);

    /* Combined offset + gain */
    coeffs.temp_offset_dcx10 = 8;   /* +0.8°C */
    coeffs.temp_gain_x1000 = 1030;  /* 1.03× gain */
    /* 250 × 1030 / 1000 + 8 = 257 + 8 = 265 → 26.5°C */
    result = adc_cal_apply_temp(250, &coeffs);
    ASSERT_INT16_EQ(265, result);
}

/**
 * test_two_point_calibration — Test two-point calibration computation
 *
 * Simulates the factory calibration process:
 *   1. Apply known low voltage (3.300V)
 *   2. Apply known high voltage (4.100V)
 *   3. Compute offset and gain from the two measurements
 *
 * This verifies the math used in adc_cal_factory_calibrate().
 */
static void test_two_point_calibration(void)
{
    /* Simulate ADC readings at two known voltages.
     *
     * At VBAT = 3300 mV:
     *   V_adc = 3300 × 33000 / 133000 = 818.8 mV
     *   ADC_raw = 818.8 × 4095 / 3300 ≈ 1016
     *   Measured = 1016 × 3249700 / 1000000 ≈ 3302 mV
     *
     * At VBAT = 4100 mV:
     *   V_adc = 4100 × 33000 / 133000 = 1017.3 mV
     *   ADC_raw = 1017.3 × 4095 / 3300 ≈ 1263
     *   Measured = 1263 × 3249700 / 1000000 ≈ 4104 mV
     */
    uint16_t measured_low_mv = 3302;   /* Slightly off due to rounding */
    uint16_t measured_high_mv = 4104;
    uint16_t vbat_low_mv = 3300;
    uint16_t vbat_high_mv = 4100;

    /* Compute gain and offset:
     * gain = (vbat_high - vbat_low) / (measured_high - measured_low)
     * gain_x1000 = (4100 - 3300) * 1000 / (4104 - 3302)
     *             = 800000 / 802 ≈ 997.5
     *
     * offset = vbat_low - measured_low * gain
     *        = 3300 - 3302 * 997.5 / 1000
     *        = 3300 - 3293.7 ≈ 6.3 mV
     */
    int32_t gain_x1000;
    int32_t offset_mv;

    gain_x1000 = ((int32_t)(vbat_high_mv - vbat_low_mv) * 1000) /
                 (int32_t)(measured_high_mv - measured_low_mv);

    offset_mv = (int32_t)vbat_low_mv -
                ((int32_t)measured_low_mv * gain_x1000) / 1000;

    /* Gain should be close to 1.000 (within ±5%) */
    ASSERT_TRUE(gain_x1000 >= 950 && gain_x1000 <= 1050);

    /* Offset should be small (within ±100 mV) */
    ASSERT_TRUE(offset_mv >= -100 && offset_mv <= 100);

    /* Verify that applying the calibration to the measured values
     * produces the correct known voltages (within ±5 mV) */
    uint16_t cal_low = (uint16_t)(((int32_t)measured_low_mv + offset_mv) *
                                  gain_x1000 / 1000);
    uint16_t cal_high = (uint16_t)(((int32_t)measured_high_mv + offset_mv) *
                                   gain_x1000 / 1000);

    ASSERT_TRUE(abs((int)cal_low - (int)vbat_low_mv) <= 5);
    ASSERT_TRUE(abs((int)cal_high - (int)vbat_high_mv) <= 5);
}

/**
 * test_checksum_computation — Test calibration record checksum
 */
static void test_checksum_computation(void)
{
    uint8_t data[] = {0xCA, 0xAD, 0x01, 0x00, 0x00, 0xE8, 0x03,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    uint8_t expected = 0;
    size_t i;

    for (i = 0; i < sizeof(data); i++) {
        expected += data[i];
    }

    uint8_t result = compute_checksum(data, sizeof(data));
    ASSERT_INT_EQ((int)expected, (int)result);

    /* Checksum of empty data should be 0 */
    result = compute_checksum(NULL, 0);
    ASSERT_INT_EQ(0, (int)result);

    /* Checksum of all-zero data should be 0 */
    uint8_t zeros[16] = {0};
    result = compute_checksum(zeros, sizeof(zeros));
    ASSERT_INT_EQ(0, (int)result);
}

/**
 * test_divider_ratio_accuracy — Test voltage divider ratio accuracy
 *
 * Verifies that the integer-math divider ratio matches the floating-point
 * calculation within acceptable tolerance.
 */
static void test_divider_ratio_accuracy(void)
{
    /* Floating-point divider ratio:
     * (R1 + R2) / R2 = (100000 + 33000) / 33000 = 4.03030...
     *
     * Our integer-math multiplier:
     * VBAT_mV = (ADC_raw × 3249700 + 500) / 1000000
     *
     * This is equivalent to:
     * VBAT_mV = ADC_raw × VREF × (R1 + R2) / R2 / 4095
     *         = ADC_raw × 3300 × 133000 / 33000 / 4095
     *         = ADC_raw × 3249.6969...
     *         ≈ ADC_raw × 3249700 / 1000000 (with rounding)
     *
     * Error at full scale (ADC = 4095):
     * Float: 4095 × 3249.6969 = 13307.3 mV
     * Int:   (4095 × 3249700 + 500) / 1000000 = 13307.7 mV
     * Error: 0.4 mV, which is < 0.003% — well within tolerance.
     */
    float float_ratio = (float)(ADC_CAL_R1_OHM + ADC_CAL_R2_OHM) /
                        (float)ADC_CAL_R2_OHM;
    float float_multiplier = (float)ADC_CAL_REF_MV * float_ratio /
                             (float)ADC_CAL_FULL_SCALE;

    /* Test at several ADC values */
    uint16_t adc_values[] = {500, 1000, 1140, 1298, 2000, 3000, 4095};
    int i;

    for (i = 0; i < (int)(sizeof(adc_values) / sizeof(adc_values[0])); i++) {
        uint16_t adc = adc_values[i];
        uint16_t int_result = adc_raw_to_mv(adc);
        float float_result = (float)adc * float_multiplier;

        /* Allow ±10 mV tolerance for integer vs float arithmetic differences.
         * The integer formula uses a fixed numerator/denominator (3249700/1000000)
         * which diverges from the floating-point calculation at higher ADC values
         * due to accumulated rounding. The maximum divergence at full scale is
         * about 7 mV, well within the ±30 mV tolerance of battery monitoring. */
        int diff = abs((int)int_result - (int)(float_result + 0.5f));
        ASSERT_TRUE(diff <= 10);
    }
}

/**
 * test_temperature_negative_values — Test temperature calibration with negatives
 *
 * Verifies correct handling of sub-zero temperatures.
 */
static void test_temperature_negative_values(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 0,
        .vbat_gain_x1000 = 1000,
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* -10.0°C = -100 in 0.1°C units */
    int16_t result = adc_cal_apply_temp(-100, &coeffs);
    ASSERT_INT16_EQ(-100, result);

    /* -10.0°C with -5.0°C offset = -15.0°C = -150 */
    coeffs.temp_offset_dcx10 = -50;
    result = adc_cal_apply_temp(-100, &coeffs);
    ASSERT_INT16_EQ(-150, result);

    /* -10.0°C with 1.05× gain = -10.5°C = -105 */
    coeffs.temp_offset_dcx10 = 0;
    coeffs.temp_gain_x1000 = 1050;
    result = adc_cal_apply_temp(-100, &coeffs);
    ASSERT_INT16_EQ(-105, result);
}

/**
 * test_boundary_conditions — Test edge cases and boundary conditions
 */
static void test_boundary_conditions(void)
{
    struct adc_cal_coeffs coeffs = {
        .vbat_offset_mv = 0,
        .vbat_gain_x1000 = 1000,
        .temp_offset_dcx10 = 0,
        .temp_gain_x1000 = 1000,
        .calibrated = true,
        .cal_version = 1,
        .reserved = 0,
    };

    /* ADC = 0 → 0 mV */
    ASSERT_UINT16_EQ(0, adc_raw_to_mv(0));

    /* ADC = 0 with calibration → 0 mV */
    ASSERT_UINT16_EQ(0, adc_cal_apply_vbat(0, &coeffs));

    /* Unity calibration: output should equal input */
    ASSERT_UINT16_EQ(3700, adc_cal_apply_vbat(3700, &coeffs));
    ASSERT_UINT16_EQ(4200, adc_cal_apply_vbat(4200, &coeffs));
    ASSERT_UINT16_EQ(3000, adc_cal_apply_vbat(3000, &coeffs));

    /* Temperature: 0°C with unity calibration */
    ASSERT_INT16_EQ(0, adc_cal_apply_temp(0, &coeffs));

    /* Temperature: maximum plausible value (850 = 85.0°C) */
    ASSERT_INT16_EQ(850, adc_cal_apply_temp(850, &coeffs));

    /* Temperature: minimum plausible value (-400 = -40.0°C) */
    ASSERT_INT16_EQ(-400, adc_cal_apply_temp(-400, &coeffs));
}

/* ── Flash calibration record tests ─────────────────────────────────────── */

/*
 * Helper: compute additive checksum over [data, data+len) — mirrors the
 * implementation in adc_calibration.c so the tests verify the contract.
 */
static uint8_t test_compute_checksum(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += p[i];
    return (uint8_t)(sum & 0xFFU);
}

/*
 * Helper: populate a valid adc_cal_record and set its checksum field.
 */
static void make_valid_record(struct adc_cal_record *rec,
                               int16_t offset_mv, uint16_t gain_x1000)
{
    memset(rec, 0, sizeof(*rec));
    rec->magic              = ADC_CAL_MAGIC;
    rec->version            = 1;
    rec->coeffs.vbat_offset_mv    = offset_mv;
    rec->coeffs.vbat_gain_x1000   = gain_x1000;
    rec->coeffs.temp_offset_dcx10 = 0;
    rec->coeffs.temp_gain_x1000   = 1000;
    rec->coeffs.calibrated        = true;
    rec->coeffs.cal_version       = 1;
    rec->timestamp          = 0;
    rec->checksum = test_compute_checksum(rec,
                                          sizeof(*rec) - sizeof(rec->checksum));
}

/*
 * Helper: validate a record (mirrors validate_cal_record in adc_calibration.c).
 * Returns true if valid.
 */
static bool record_is_valid(const struct adc_cal_record *rec)
{
    uint8_t expected;
    if (!rec)                       return false;
    if (rec->magic != ADC_CAL_MAGIC) return false;
    if (rec->version != 1)          return false;
    expected = test_compute_checksum(rec,
                                      sizeof(*rec) - sizeof(rec->checksum));
    return rec->checksum == expected;
}

/**
 * test_flash_record_valid — A correctly assembled record passes validation.
 */
static void test_flash_record_valid(void)
{
    struct adc_cal_record rec;
    make_valid_record(&rec, -20, 1010);  /* typical calibration values */
    ASSERT_INT_EQ(1, (int)record_is_valid(&rec));
}

/**
 * test_flash_record_bad_magic — Wrong magic byte fails validation.
 */
static void test_flash_record_bad_magic(void)
{
    struct adc_cal_record rec;
    make_valid_record(&rec, 0, 1000);
    rec.magic = 0xDEAD;
    ASSERT_INT_EQ(0, (int)record_is_valid(&rec));
}

/**
 * test_flash_record_bad_version — Unknown version fails validation.
 */
static void test_flash_record_bad_version(void)
{
    struct adc_cal_record rec;
    make_valid_record(&rec, 0, 1000);
    rec.version = 2;  /* only version 1 is defined */
    /* checksum now stale — but version check fires first */
    ASSERT_INT_EQ(0, (int)record_is_valid(&rec));
}

/**
 * test_flash_record_bad_checksum — Single-byte corruption detected by checksum.
 */
static void test_flash_record_bad_checksum(void)
{
    struct adc_cal_record rec;
    make_valid_record(&rec, 50, 995);
    /* Corrupt one byte of the gain field */
    rec.coeffs.vbat_gain_x1000 ^= 0x01;
    ASSERT_INT_EQ(0, (int)record_is_valid(&rec));
}

/**
 * test_flash_record_erased — Erased flash (all 0xFF) is rejected.
 *
 * After erase, flash reads as 0xFF.  The magic field would be 0xFFFF ≠
 * ADC_CAL_MAGIC, so validation must fail.
 */
static void test_flash_record_erased(void)
{
    struct adc_cal_record rec;
    memset(&rec, 0xFF, sizeof(rec));  /* simulate erased sector */
    ASSERT_INT_EQ(0, (int)record_is_valid(&rec));
}

/**
 * test_flash_record_all_zeros — All-zeros record (uninitialized memory) rejected.
 */
static void test_flash_record_all_zeros(void)
{
    struct adc_cal_record rec;
    memset(&rec, 0x00, sizeof(rec));  /* magic = 0x0000 ≠ ADC_CAL_MAGIC */
    ASSERT_INT_EQ(0, (int)record_is_valid(&rec));
}

/**
 * test_flash_record_checksum_coverage — Checksum covers all bytes before it.
 *
 * Two records with identical data except one byte should have different
 * checksums.
 */
static void test_flash_record_checksum_coverage(void)
{
    struct adc_cal_record rec_a, rec_b;
    make_valid_record(&rec_a, 10, 1005);
    make_valid_record(&rec_b, 10, 1005);

    /* Initially identical and valid */
    ASSERT_INT_EQ(1, (int)record_is_valid(&rec_a));
    ASSERT_INT_EQ(1, (int)record_is_valid(&rec_b));
    ASSERT_INT_EQ((int)rec_a.checksum, (int)rec_b.checksum);

    /* Modify one byte in rec_b without updating checksum */
    rec_b.coeffs.temp_gain_x1000 = 999;
    ASSERT_INT_EQ(0, (int)record_is_valid(&rec_b));
    ASSERT_INT_EQ(1, (int)record_is_valid(&rec_a));  /* rec_a untouched */
}

/**
 * test_flash_record_round_trip — build, validate, extract, re-validate.
 *
 * Simulates the adc_cal_factory_calibrate() → (memcpy into page) →
 * adc_cal_init() load path without flash hardware.
 */
static void test_flash_record_round_trip(void)
{
    struct adc_cal_record original, loaded;

    make_valid_record(&original, -30, 1008);
    ASSERT_INT_EQ(1, (int)record_is_valid(&original));

    /* Simulate flash page program + read: byte-copy into loaded */
    memcpy(&loaded, &original, sizeof(loaded));
    ASSERT_INT_EQ(1, (int)record_is_valid(&loaded));

    /* Coefficients survive the round-trip */
    ASSERT_INT_EQ(-30, (int)loaded.coeffs.vbat_offset_mv);
    ASSERT_INT_EQ(1008, (int)loaded.coeffs.vbat_gain_x1000);
    ASSERT_INT_EQ(1,  (int)loaded.coeffs.calibrated);
    ASSERT_INT_EQ(1,  (int)loaded.coeffs.cal_version);
}

/**
 * test_flash_record_struct_size — Record fits in one flash page (256 bytes).
 *
 * adc_cal_store_to_flash() uses a single FLASH_PAGE_SIZE buffer.  The struct
 * must not exceed 256 bytes, otherwise the store function returns -1.
 */
static void test_flash_record_struct_size(void)
{
    ASSERT_INT_EQ(1, (int)(sizeof(struct adc_cal_record) <= 256));
}

/**
 * test_flash_cal_offset_alignment — FLASH_CAL_OFFSET is sector-aligned (4 KB).
 *
 * flash_range_erase() requires the offset to be a multiple of FLASH_SECTOR_SIZE
 * (4096 = 0x1000).  Verify the constant satisfies this.
 */
static void test_flash_cal_offset_alignment(void)
{
    ASSERT_INT_EQ(0, (int)(FLASH_CAL_OFFSET % 4096UL));
}

/**
 * test_flash_checksum_all_bit_flips — Every single-byte corruption detected.
 *
 * Flip each byte of a valid record (except checksum) one at a time and
 * confirm validation fails.  This exercises 100% of covered bytes.
 */
static void test_flash_checksum_all_bit_flips(void)
{
    struct adc_cal_record rec;
    make_valid_record(&rec, 5, 1002);

    size_t covered_bytes = sizeof(rec) - sizeof(rec.checksum);
    uint8_t *raw = (uint8_t *)&rec;

    for (size_t i = 0; i < covered_bytes; i++) {
        raw[i] ^= 0xFF;  /* flip all bits in byte i */
        /* Should fail — either magic, version, or checksum check */
        ASSERT_INT_EQ(0, (int)record_is_valid(&rec));
        raw[i] ^= 0xFF;  /* restore */
    }

    /* After all restores the record is valid again */
    ASSERT_INT_EQ(1, (int)record_is_valid(&rec));
}

/* ── Main test runner ─────────────────────────────────────────────────────── */

int main(void)
{
    TEST_SUITE_START();

    fprintf(stderr, "\n");
    RUN_TEST(test_voltage_divider_nominal);
    RUN_TEST(test_calibration_offset_positive);
    RUN_TEST(test_calibration_offset_negative);
    RUN_TEST(test_calibration_gain_correction);
    RUN_TEST(test_calibration_combined_offset_gain);
    RUN_TEST(test_calibration_clamp_zero);
    RUN_TEST(test_calibration_clamp_max);
    RUN_TEST(test_temperature_calibration);
    RUN_TEST(test_two_point_calibration);
    RUN_TEST(test_checksum_computation);
    RUN_TEST(test_divider_ratio_accuracy);
    RUN_TEST(test_temperature_negative_values);
    RUN_TEST(test_boundary_conditions);
    RUN_TEST(test_flash_record_valid);
    RUN_TEST(test_flash_record_bad_magic);
    RUN_TEST(test_flash_record_bad_version);
    RUN_TEST(test_flash_record_bad_checksum);
    RUN_TEST(test_flash_record_erased);
    RUN_TEST(test_flash_record_all_zeros);
    RUN_TEST(test_flash_record_checksum_coverage);
    RUN_TEST(test_flash_record_round_trip);
    RUN_TEST(test_flash_record_struct_size);
    RUN_TEST(test_flash_cal_offset_alignment);
    RUN_TEST(test_flash_checksum_all_bit_flips);

    TEST_SUITE_END();
}