/*
 * test_battery_monitor.c — Unit Tests for Battery Monitor Calculations
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the battery voltage-to-percentage conversion, brownout detection,
 * and ADC-to-voltage calculations used in the RP2350B firmware's
 * battery_monitor module.
 *
 * Build (standalone, no cmocka):
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_battery_monitor test_battery_monitor.c
 *
 * Build (with cmocka):
 *   gcc -Wall -Wextra -std=c11 -lcmocka -o test_battery_monitor test_battery_monitor.c
 *
 * Run:
 *   ./test_battery_monitor
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

#define ASSERT_TRUE(cond) do {                                              \
    g_tests_run++;                                                          \
    if (!(cond)) {                                                          \
        fprintf(stderr, "  FAIL: %s:%d: assertion failed: %s\n",            \
                __FILE__, __LINE__, #cond);                                 \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_INT_GE(actual, minimum) do {                                 \
    g_tests_run++;                                                          \
    if ((actual) < (minimum)) {                                             \
        fprintf(stderr, "  FAIL: %s:%d: %d < %d\n",                        \
                __FILE__, __LINE__, (int)(actual), (int)(minimum));         \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_INT_LE(actual, maximum) do {                                 \
    g_tests_run++;                                                          \
    if ((actual) > (maximum)) {                                             \
        fprintf(stderr, "  FAIL: %s:%d: %d > %d\n",                        \
                __FILE__, __LINE__, (int)(actual), (int)(maximum));         \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define RUN_TEST(func) do {                                                \
    printf("Running: %s\n", #func);                                       \
    func();                                                                \
} while (0)

#define TEST_RESULTS() do {                                                \
    printf("\n=== Results: %d/%d passed, %d failed ===\n",                \
           g_tests_passed, g_tests_run, g_tests_failed);                   \
    return g_tests_failed > 0 ? 1 : 0;                                     \
} while (0)

#else
#include <cmocka.h>
/* Use cmocka macros */
#define ASSERT_INT_EQ(expected, actual) assert_int_equal((actual), (expected))
#define ASSERT_TRUE(cond) assert_true(cond)
#define ASSERT_FALSE(cond) assert_false(cond)
#define ASSERT_INT_GE(actual, minimum) assert_true((actual) >= (minimum))
#define ASSERT_INT_LE(actual, maximum) assert_true((actual) <= (maximum))
#endif

/* ── Battery monitor constants (mirrored from battery_monitor.h) ──────────── */

/*
 * Voltage divider: R1=100kΩ (VBAT → ADC0), R2=33kΩ (ADC0 → GND)
 * Divider ratio = (R1 + R2) / R2 = 133000 / 33000 = 4.0303
 * VBAT_mV = ADC_raw × 3300 × 133000 / (33000 × 4095)
 * Simplified: VBAT_mV ≈ (ADC_raw × 3249 + 500) / 1000
 */
#define BATT_ADC_REF_MV             3300
#define BATT_ADC_RESOLUTION         4095
#define BATT_R1_OHM                 100000UL
#define BATT_R2_OHM                 33000UL
#define BATT_DIVIDER_NUMERATOR      (BATT_R1_OHM + BATT_R2_OHM)  /* 133000 */
#define BATT_DIVIDER_DENOMINATOR    BATT_R2_OHM                    /* 33000 */
#define BATT_BROWNOUT_THRESHOLD_MV  2800
#define BATT_BROWNOUT_HYSTERESIS_MV 200
#define BATT_BROWNOUT_RECOVERY_MV   (BATT_BROWNOUT_THRESHOLD_MV + BATT_BROWNOUT_HYSTERESIS_MV)  /* 3000 */
#define BATT_FULL_MV                 4200
#define BATT_EMPTY_MV                3000

/* ── Reimplemented battery_monitor functions for testing ─────────────────── */

/**
 * adc_raw_to_vbat_mv — Convert raw ADC reading to battery voltage
 *
 * With a voltage divider of R1=100kΩ and R2=33kΩ:
 *   V_ADC = VBAT × R2 / (R1 + R2) = VBAT × 33/133
 *   VBAT_mV = ADC_raw × 3300 × 133000 / (33000 × 4095)
 *   Simplified: VBAT_mV ≈ (ADC_raw × 29260 + 4504) / 9009
 *
 * Using the same fixed-point arithmetic as the production code:
 *   VBAT_mV = (ADC_raw × 29260 + 4504) / 9009
 *   (where 29260 = 3300 × 133000 / 15000, 9009 = 33000 × 4095 / 15000)
 */
static uint16_t adc_raw_to_vbat_mv(uint16_t adc_raw)
{
    uint32_t vbat = ((uint32_t)adc_raw * 29260UL + 4504UL) / 9009UL;
    return (uint16_t)vbat;
}

/**
 * battery_voltage_to_percent — Li-Po discharge curve
 *
 * Piecewise linear approximation:
 *   4200 mV → 100%
 *   3700 mV → 50%
 *   3300 mV → 10%
 *   3000 mV → 0%
 */
static uint8_t battery_voltage_to_percent(uint16_t vbat_mv)
{
    if (vbat_mv >= BATT_FULL_MV)
        return 100;
    if (vbat_mv <= BATT_EMPTY_MV)
        return 0;

    if (vbat_mv >= 3700) {
        /* 3700–4200 mV → 50–100% (linear) */
        uint32_t mv_above = vbat_mv - 3700;
        return (uint8_t)(50 + (mv_above * 50) / 500);
    } else if (vbat_mv >= 3300) {
        /* 3300–3700 mV → 10–50% (linear) */
        uint32_t mv_above = vbat_mv - 3300;
        return (uint8_t)(10 + (mv_above * 40) / 400);
    } else {
        /* 3000–3300 mV → 0–10% (linear) */
        uint32_t mv_above = vbat_mv - BATT_EMPTY_MV;
        return (uint8_t)((mv_above * 10) / 300);
    }
}

/**
 * battery_is_brownout — Hysteresis brownout detection
 */
static bool battery_is_brownout(uint16_t vbat_mv, bool *brownout_active)
{
    if (*brownout_active) {
        /* Recovery: need voltage above recovery threshold */
        if (vbat_mv > BATT_BROWNOUT_RECOVERY_MV) {
            *brownout_active = false;
            return false;
        }
        return true;
    } else {
        /* Entry: voltage below brownout threshold */
        if (vbat_mv < BATT_BROWNOUT_THRESHOLD_MV) {
            *brownout_active = true;
            return true;
        }
        return false;
    }
}

/* ── Test: ADC raw value to voltage conversion ───────────────────────────── */

static void test_adc_raw_to_vbat_zero(void)
{
    /* ADC raw 0 → VBAT = 0 mV */
    ASSERT_INT_EQ(0, adc_raw_to_vbat_mv(0));
}

static void test_adc_raw_to_vbat_midrange(void)
{
    /* Midrange ADC: 2048 (approximately half of 4095)
     * With R1=100kΩ, R2=33kΩ divider:
     *   VBAT = (2048 × 29260 + 4504) / 9009 ≈ 6653 mV
     * This is above the Li-Po range but tests the math. */
    uint16_t vbat = adc_raw_to_vbat_mv(2048);
    /* Allow wide tolerance for integer rounding */
    ASSERT_INT_GE(vbat, 6500);
    ASSERT_INT_LE(vbat, 6800);
}

static void test_adc_raw_to_vbat_fullscale(void)
{
    /* Full-scale ADC: 4095 → VBAT = (4095 × 29260 + 4504) / 9009 ≈ 13288 mV
     * (this is above the Li-Po range but tests the math) */
    uint16_t vbat = adc_raw_to_vbat_mv(4095);
    ASSERT_INT_GE(vbat, 13200);
    ASSERT_INT_LE(vbat, 13400);
}

static void test_adc_raw_to_vbat_typical_3v7(void)
{
    /* VBAT = 3.7V → V_ADC = 3700 × 33/133 ≈ 918.8 mV → ADC ≈ 1140
     * VBAT_mV = (1140 × 29260 + 4504) / 9009 ≈ 3705 */
    uint16_t adc_3v7 = (uint16_t)((918UL * 4095) / 3300);  /* V_ADC at 3.7V */
    uint16_t vbat = adc_raw_to_vbat_mv(adc_3v7);
    /* Allow ±50 mV tolerance for rounding */
    ASSERT_INT_GE(vbat, 3650);
    ASSERT_INT_LE(vbat, 3750);
}

static void test_adc_raw_to_vbat_low_battery(void)
{
    /* VBAT = 3.0V → V_ADC = 3000 × 33/133 ≈ 744.4 mV → ADC ≈ 924
     * VBAT_mV = (924 × 29260 + 4504) / 9009 ≈ 3001 */
    uint16_t adc_3v0 = (uint16_t)((744UL * 4095) / 3300);
    uint16_t vbat = adc_raw_to_vbat_mv(adc_3v0);
    ASSERT_INT_GE(vbat, 2950);
    ASSERT_INT_LE(vbat, 3050);
}

/* ── Test: Battery voltage to percentage ──────────────────────────────────── */

static void test_battery_percent_full(void)
{
    ASSERT_INT_EQ(100, battery_voltage_to_percent(4200));
    ASSERT_INT_EQ(100, battery_voltage_to_percent(4500));  /* above max */
}

static void test_battery_percent_empty(void)
{
    ASSERT_INT_EQ(0, battery_voltage_to_percent(3000));
    ASSERT_INT_EQ(0, battery_voltage_to_percent(2500));  /* below min */
}

static void test_battery_percent_50_percent(void)
{
    /* 3700 mV → 50% */
    ASSERT_INT_EQ(50, battery_voltage_to_percent(3700));
}

static void test_battery_percent_10_percent(void)
{
    /* 3300 mV → 10% */
    ASSERT_INT_EQ(10, battery_voltage_to_percent(3300));
}

static void test_battery_percent_midpoints(void)
{
    /* Test interpolation in each segment */

    /* Top segment: 3950 mV → 50 + (3950-3700)*50/500 = 50+25 = 75% */
    uint8_t pct_3950 = battery_voltage_to_percent(3950);
    ASSERT_INT_GE(pct_3950, 73);
    ASSERT_INT_LE(pct_3950, 77);

    /* Middle segment: 3500 mV → 10 + (3500-3300)*40/400 = 10+20 = 30% */
    uint8_t pct_3500 = battery_voltage_to_percent(3500);
    ASSERT_INT_GE(pct_3500, 28);
    ASSERT_INT_LE(pct_3500, 32);

    /* Bottom segment: 3150 mV → 0 + (3150-3000)*10/300 = 5% */
    uint8_t pct_3150 = battery_voltage_to_percent(3150);
    ASSERT_INT_GE(pct_3150, 3);
    ASSERT_INT_LE(pct_3150, 7);
}

static void test_battery_percent_monotonic(void)
{
    /* Battery percentage must be monotonically increasing with voltage */
    uint8_t prev = 0;
    for (uint16_t mv = 3000; mv <= 4200; mv += 50) {
        uint8_t pct = battery_voltage_to_percent(mv);
        ASSERT_TRUE(pct >= prev);
        prev = pct;
    }
}

static void test_battery_percent_boundary_values(void)
{
    /* Test exact boundaries */
    ASSERT_INT_EQ(0, battery_voltage_to_percent(3000));
    ASSERT_INT_EQ(10, battery_voltage_to_percent(3300));
    ASSERT_INT_EQ(50, battery_voltage_to_percent(3700));
    ASSERT_INT_EQ(100, battery_voltage_to_percent(4200));
}

/* ── Test: Brownout detection with hysteresis ─────────────────────────────── */

static void test_brownout_normal_voltage(void)
{
    bool brownout = false;
    /* 3800 mV — well above both thresholds */
    ASSERT_FALSE(battery_is_brownout(3800, &brownout));
    ASSERT_FALSE(brownout);
}

static void test_brownout_enter(void)
{
    bool brownout = false;
    /* 2700 mV — below 2800 mV threshold */
    ASSERT_TRUE(battery_is_brownout(2700, &brownout));
    ASSERT_TRUE(brownout);
}

static void test_brownout_hysteresis_hold(void)
{
    bool brownout = true;  /* Already in brownout */
    /* 2900 mV — above threshold (2800) but below recovery (3000) */
    ASSERT_TRUE(battery_is_brownout(2900, &brownout));
    ASSERT_TRUE(brownout);
}

static void test_brownout_hysteresis_exit(void)
{
    bool brownout = true;  /* Already in brownout */
    /* 3100 mV — above recovery threshold (3000) */
    ASSERT_FALSE(battery_is_brownout(3100, &brownout));
    ASSERT_FALSE(brownout);
}

static void test_brownout_full_cycle(void)
{
    bool brownout = false;

    /* Start at normal voltage */
    ASSERT_FALSE(battery_is_brownout(3800, &brownout));
    ASSERT_FALSE(brownout);

    /* Voltage drops below threshold (2800 mV) */
    ASSERT_TRUE(battery_is_brownout(2700, &brownout));
    ASSERT_TRUE(brownout);

    /* Voltage recovers partially but not enough (still below 3000 mV) */
    ASSERT_TRUE(battery_is_brownout(2900, &brownout));
    ASSERT_TRUE(brownout);

    /* Voltage recovers above hysteresis (3000 mV) */
    ASSERT_FALSE(battery_is_brownout(3100, &brownout));
    ASSERT_FALSE(brownout);

    /* Normal operation */
    ASSERT_FALSE(battery_is_brownout(3800, &brownout));
    ASSERT_FALSE(brownout);
}

static void test_brownout_exact_threshold(void)
{
    bool brownout = false;

    /* Exactly at threshold (2800 mV) — not below, so not brownout */
    ASSERT_FALSE(battery_is_brownout(2800, &brownout));
    ASSERT_FALSE(brownout);

    /* 1 mV below threshold — triggers brownout */
    ASSERT_TRUE(battery_is_brownout(2799, &brownout));
    ASSERT_TRUE(brownout);
}

static void test_brownout_exact_recovery(void)
{
    bool brownout = true;

    /* Exactly at recovery (3000 mV) — not above, stays in brownout */
    ASSERT_TRUE(battery_is_brownout(3000, &brownout));
    ASSERT_TRUE(brownout);

    /* 1 mV above recovery — exits brownout */
    ASSERT_FALSE(battery_is_brownout(3001, &brownout));
    ASSERT_FALSE(brownout);
}

static void test_brownout_no_oscillation(void)
{
    bool brownout = false;

    /* Simulate voltage oscillating around threshold */
    ASSERT_FALSE(battery_is_brownout(2850, &brownout));  /* above threshold */
    ASSERT_TRUE(battery_is_brownout(2750, &brownout));   /* below → enter brownout */
    ASSERT_TRUE(brownout);
    ASSERT_TRUE(battery_is_brownout(2850, &brownout));    /* above but in hysteresis */
    ASSERT_TRUE(brownout);
    ASSERT_TRUE(battery_is_brownout(2750, &brownout));    /* still in brownout */
    ASSERT_TRUE(brownout);
    ASSERT_FALSE(battery_is_brownout(3100, &brownout));   /* above recovery → exit */
    ASSERT_FALSE(brownout);
    ASSERT_FALSE(battery_is_brownout(2850, &brownout));    /* normal */
    ASSERT_FALSE(brownout);
}

/* ── Test: ADC quantization edge cases ───────────────────────────────────── */

static void test_adc_quantization_noise(void)
{
    /* Small ADC changes should produce small voltage changes */
    uint16_t v1 = adc_raw_to_vbat_mv(2000);
    uint16_t v2 = adc_raw_to_vbat_mv(2001);
    int32_t diff = (int32_t)v2 - (int32_t)v1;
    /* 1 LSB at midrange ≈ 1.6 mV — should be less than 5 mV */
    ASSERT_TRUE(diff >= 0 && diff < 5);
}

static void test_adc_no_overflow(void)
{
    /* Maximum ADC value should not overflow uint16_t */
    uint16_t vbat = adc_raw_to_vbat_mv(4095);
    /* With R1=100k/R2=33k divider: VBAT = (4095 * 29260 + 4504) / 9009 ≈ 13288 mV
     * This is above the Li-Po range but tests that uint16_t doesn't overflow */
    ASSERT_INT_GE(vbat, 13200);
    ASSERT_INT_LE(vbat, 13400);
}

static void test_adc_resolution(void)
{
    /* Verify that each ADC LSB corresponds to approximately 3.2 mV
     * of battery voltage at midrange (with R1=100k/R2=33k divider)
     * 100 LSB at ~6.5 mV/LSB → ~650 mV total range */
    uint16_t v_low = adc_raw_to_vbat_mv(2000);
    uint16_t v_high = adc_raw_to_vbat_mv(2100);
    uint32_t delta_mv = v_high - v_low;
    /* With 4.03x divider, 100 LSB ≈ 325 mV */
    ASSERT_INT_GE(delta_mv, 200);  /* at least 200 mV for 100 LSB */
    ASSERT_INT_LE(delta_mv, 450);  /* at most 450 mV for 100 LSB */
}

/* ── Test: Temperature sensor calculations ────────────────────────────────── */

/**
 * The RP2350B internal temperature sensor uses the formula:
 *   T(°C) = 27 - (ADC_raw - VBE_27C_ADC) / slope
 * where VBE_27C_ADC is the ADC reading at 27°C and slope is ~-2 mV/°C.
 *
 * We test the conversion math here.
 */
static int16_t temp_adc_to_c_x10(uint16_t adc_raw, uint16_t vbe_27c_adc)
{
    /* T = 27 - (ADC - VBE27) * 3300 / (slope_mV * 4095)
     * slope = -2 mV/°C → 20 in 0.1°C units per mV
     * T_x10 = 270 - (ADC - VBE27) * 3300 * 10 / (2 * 4095)
     */
    int32_t delta = (int32_t)adc_raw - (int32_t)vbe_27c_adc;
    int32_t temp_x10 = 270 - (delta * 33000) / (2 * 4095);
    return (int16_t)temp_x10;
}

static void test_temp_at_27c(void)
{
    /* At exactly 27°C, ADC should equal VBE27 reference */
    uint16_t vbe_27c = 1800;  /* Typical ~1.45V at 27°C */
    int16_t temp = temp_adc_to_c_x10(vbe_27c, vbe_27c);
    ASSERT_INT_EQ(270, temp);  /* 27.0°C */
}

static void test_temp_hot(void)
{
    /* At 85°C (58°C above 27°C), ADC should be lower by ~2mV/°C * 58 = 116mV
     * ADC decrease = 116mV * 4095 / 3300 ≈ 144 LSB */
    uint16_t vbe_27c = 1800;
    uint16_t adc_85c = vbe_27c - 144;  /* ~1656 */
    int16_t temp = temp_adc_to_c_x10(adc_85c, vbe_27c);
    ASSERT_INT_GE(temp, 800);   /* ≥ 80°C */
    ASSERT_INT_LE(temp, 900);   /* ≤ 90°C */
}

static void test_temp_cold(void)
{
    /* At -40°C (67°C below 27°C), ADC should be higher by ~2mV/°C * 67 = 134mV
     * ADC increase = 134mV * 4095 / 3300 ≈ 166 LSB */
    uint16_t vbe_27c = 1800;
    uint16_t adc_n40c = vbe_27c + 166;  /* ~1966 */
    int16_t temp = temp_adc_to_c_x10(adc_n40c, vbe_27c);
    ASSERT_INT_GE(temp, -500);  /* ≥ -50°C */
    ASSERT_INT_LE(temp, -300);  /* ≤ -30°C */
}

/* ── Main test runner ────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Battery Monitor Unit Tests ===\n\n");

    printf("--- ADC to Voltage Conversion ---\n");
    RUN_TEST(test_adc_raw_to_vbat_zero);
    RUN_TEST(test_adc_raw_to_vbat_midrange);
    RUN_TEST(test_adc_raw_to_vbat_fullscale);
    RUN_TEST(test_adc_raw_to_vbat_typical_3v7);
    RUN_TEST(test_adc_raw_to_vbat_low_battery);

    printf("\n--- Battery Percentage ---\n");
    RUN_TEST(test_battery_percent_full);
    RUN_TEST(test_battery_percent_empty);
    RUN_TEST(test_battery_percent_50_percent);
    RUN_TEST(test_battery_percent_10_percent);
    RUN_TEST(test_battery_percent_midpoints);
    RUN_TEST(test_battery_percent_monotonic);
    RUN_TEST(test_battery_percent_boundary_values);

    printf("\n--- Brownout Detection ---\n");
    RUN_TEST(test_brownout_normal_voltage);
    RUN_TEST(test_brownout_enter);
    RUN_TEST(test_brownout_hysteresis_hold);
    RUN_TEST(test_brownout_hysteresis_exit);
    RUN_TEST(test_brownout_full_cycle);
    RUN_TEST(test_brownout_exact_threshold);
    RUN_TEST(test_brownout_exact_recovery);
    RUN_TEST(test_brownout_no_oscillation);

    printf("\n--- ADC Quantization ---\n");
    RUN_TEST(test_adc_quantization_noise);
    RUN_TEST(test_adc_no_overflow);
    RUN_TEST(test_adc_resolution);

    printf("\n--- Temperature Sensor ---\n");
    RUN_TEST(test_temp_at_27c);
    RUN_TEST(test_temp_hot);
    RUN_TEST(test_temp_cold);

    TEST_RESULTS();
}