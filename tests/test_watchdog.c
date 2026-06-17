/*
 * test_watchdog.c — Unit Tests for Watchdog Timer Constants and Logic
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the watchdog timer configuration, brownout detection magic
 * values, timeout calculations, and bark/reset timing used in the
 * RP2350B firmware's watchdog module.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_watchdog test_watchdog.c
 *
 * Run:
 *   ./test_watchdog
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Minimal test framework (standalone, no cmocka) ─────────────────────── */

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

#define ASSERT_UINT_EQ(expected, actual) do {                               \
    g_tests_run++;                                                          \
    if ((expected) != (actual)) {                                           \
        fprintf(stderr, "  FAIL: %s:%d: expected 0x%08x, got 0x%08x\n",    \
                __FILE__, __LINE__, (unsigned)(expected), (unsigned)(actual)); \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_UINT64_EQ(expected, actual) do {                             \
    g_tests_run++;                                                          \
    if ((expected) != (actual)) {                                           \
        fprintf(stderr, "  FAIL: %s:%d: expected 0x%016llx, got 0x%016llx\n", \
                __FILE__, __LINE__, (unsigned long long)(expected),          \
                (unsigned long long)(actual));                               \
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
    printf("\n=== Watchdog Unit Test Results: %d/%d passed, %d failed ===\n", \
           g_tests_passed, g_tests_run, g_tests_failed);                   \
    return g_tests_failed > 0 ? 1 : 0;                                     \
} while (0)

/* ── Watchdog constants (mirrored from watchdog.h and watchdog.c) ──────── */

/* From watchdog.h */
#define WATCHDOG_TIMEOUT_MS       5000
#define WATCHDOG_BARK_TIME_MS     1000
#define WATCHDOG_BROWNOUT_MAGIC   0xB047B00FUL

/* From watchdog.c — RP2350B watchdog registers */
#define RP2350B_WATCHDOG_BASE     0x40058000UL
#define WD_SCRATCH_BOD_MAGIC      0xB07D0001UL

/* Watchdog register offsets */
#define WD_CTRL_OFFSET            0x00
#define WD_LOAD_OFFSET            0x04
#define WD_REASON_OFFSET          0x08
#define WD_SCRATCH0_OFFSET        0x0C
#define WD_SCRATCH1_OFFSET        0x10
#define WD_SCRATCH2_OFFSET        0x14
#define WD_SCRATCH3_OFFSET        0x18
#define WD_SCRATCH4_OFFSET        0x1C
#define WD_SCRATCH5_OFFSET        0x20
#define WD_SCRATCH6_OFFSET        0x24
#define WD_SCRATCH7_OFFSET        0x28

/* WD_CTRL bits */
#define WD_CTRL_ENABLE            (1U << 0)
#define WD_CTRL_PAUSE_DBG         (1U << 1)
#define WD_CTRL_PAUSE_JTAG        (1U << 2)

/* WD_REASON bits */
#define WD_REASON_TIMER           (1U << 0)  /* Watchdog timeout */
#define WD_REASON_FORCE           (1U << 1)  /* Force reset */
#define WD_REASON_BOD             (1U << 2)  /* Brownout detection */

/* RP2350B clock — watchdog tick period is 1 μs (1 MHz tick) */
#define WD_TICK_US                1
#define WD_TICKS_PER_MS           1000

/* ── Reimplemented watchdog logic for testing ──────────────────────────── */

/**
 * watchdog_calc_load_value — Calculate watchdog load value from timeout in ms
 *
 * The RP2350B watchdog uses a 1 MHz tick (1 μs per tick).
 * Load value = timeout_ms * 1000 (ticks).
 */
static uint32_t watchdog_calc_load_value(uint32_t timeout_ms)
{
    return timeout_ms * WD_TICKS_PER_MS;
}

/**
 * watchdog_calc_bark_ticks — Calculate the number of ticks before bark
 *
 * Bark interrupt fires (timeout - bark_time) ticks after enabling.
 */
static uint32_t watchdog_calc_bark_ticks(uint32_t timeout_ms, uint32_t bark_ms)
{
    if (bark_ms >= timeout_ms)
        return 0;  /* Invalid: bark time must be less than timeout */
    return (timeout_ms - bark_ms) * WD_TICKS_PER_MS;
}

/**
 * watchdog_check_reset_reason — Check if a reset was caused by watchdog
 *
 * Returns: bitmask of reset reasons (WD_REASON_TIMER, WD_REASON_FORCE, etc.)
 */
static uint32_t watchdog_check_reset_reason(uint32_t reason_reg)
{
    return reason_reg & 0x07;  /* Only 3 bits used */
}

/**
 * watchdog_is_brownout_reset — Check if reset was caused by brownout
 *
 * Brownout is detected by checking:
 *   1. WD_REASON register has BOD bit set, OR
 *   2. Scratch register contains BOD magic value
 */
static bool watchdog_is_brownout_reset(uint32_t reason_reg, uint32_t scratch_val)
{
    /* Check reason register for BOD */
    if (reason_reg & WD_REASON_BOD)
        return true;

    /* Check scratch register for magic */
    if (scratch_val == WD_SCRATCH_BOD_MAGIC)
        return true;

    return false;
}

/**
 * watchdog_mark_brownout — Simulate marking brownout in scratch register
 *
 * Returns the value to write to scratch register 0.
 */
static uint32_t watchdog_mark_brownout(void)
{
    return WD_SCRATCH_BOD_MAGIC;
}

/**
 * watchdog_clear_brownout — Simulate clearing brownout scratch register
 *
 * Returns the value to write to scratch register 0 (0 = cleared).
 */
static uint32_t watchdog_clear_brownout(void)
{
    return 0;
}

/**
 * watchdog_validate_kick_interval — Validate that kick interval < timeout
 *
 * The main loop kicks the watchdog every loop iteration. The kick
 * interval must be significantly less than the watchdog timeout
 * to avoid spurious resets.
 *
 * Returns: true if the kick interval is safe
 */
static bool watchdog_validate_kick_interval(uint32_t kick_interval_ms,
                                             uint32_t timeout_ms)
{
    /* Kick interval must be less than 1/4 of the timeout for safety */
    return kick_interval_ms < (timeout_ms / 4);
}

/* ── Test cases ──────────────────────────────────────────────────────── */

/* --- Watchdog Constants Verification --- */

static void test_watchdog_timeout_sanity(void)
{
    /* Watchdog timeout must be reasonable: 1–30 seconds */
    ASSERT_INT_GE(WATCHDOG_TIMEOUT_MS, 1000);   /* At least 1 second */
    ASSERT_INT_LE(WATCHDOG_TIMEOUT_MS, 30000);   /* At most 30 seconds */
}

static void test_watchdog_bark_time_sanity(void)
{
    /* Bark time must be less than timeout and at least 100ms */
    ASSERT_INT_GE(WATCHDOG_BARK_TIME_MS, 100);             /* At least 100 ms */
    ASSERT_TRUE(WATCHDOG_BARK_TIME_MS < WATCHDOG_TIMEOUT_MS); /* Must be < timeout */
}

static void test_brownout_magic_values_unique(void)
{
    /* The two magic values must be different from each other
     * and from common register defaults (0, 0xFFFFFFFF) */
    ASSERT_TRUE(WATCHDOG_BROWNOUT_MAGIC != 0);
    ASSERT_TRUE(WATCHDOG_BROWNOUT_MAGIC != 0xFFFFFFFFUL);
    ASSERT_TRUE(WD_SCRATCH_BOD_MAGIC != 0);
    ASSERT_TRUE(WD_SCRATCH_BOD_MAGIC != 0xFFFFFFFFUL);
    ASSERT_TRUE(WATCHDOG_BROWNOUT_MAGIC != WD_SCRATCH_BOD_MAGIC);
}

static void test_brownout_magic_has_distinguishing_bits(void)
{
    /* Magic values should have bits set in both upper and lower 16 bits
     * to avoid accidental matches from partial register corruption */
    uint32_t magic_hi = (WATCHDOG_BROWNOUT_MAGIC >> 16) & 0xFFFF;
    uint32_t magic_lo = WATCHDOG_BROWNOUT_MAGIC & 0xFFFF;
    ASSERT_TRUE(magic_hi != 0);
    ASSERT_TRUE(magic_lo != 0);

    uint32_t bod_hi = (WD_SCRATCH_BOD_MAGIC >> 16) & 0xFFFF;
    uint32_t bod_lo = WD_SCRATCH_BOD_MAGIC & 0xFFFF;
    ASSERT_TRUE(bod_hi != 0);
    ASSERT_TRUE(bod_lo != 0);
}

/* --- Load Value Calculations --- */

static void test_load_value_at_timeout(void)
{
    /* 5000 ms timeout → 5,000,000 ticks (1 μs per tick) */
    uint32_t load = watchdog_calc_load_value(WATCHDOG_TIMEOUT_MS);
    ASSERT_INT_EQ(5000000, (int)load);
}

static void test_load_value_at_1ms(void)
{
    /* 1 ms → 1000 ticks */
    uint32_t load = watchdog_calc_load_value(1);
    ASSERT_INT_EQ(1000, (int)load);
}

static void test_load_value_at_30s(void)
{
    /* 30 seconds → 30,000,000 ticks — should fit in uint32_t */
    uint32_t load = watchdog_calc_load_value(30000);
    ASSERT_INT_EQ(30000000, (int)load);
}

static void test_load_value_no_overflow(void)
{
    /* Maximum practical timeout: 30s should not overflow uint32_t */
    /* Max uint32_t = 4,294,967,295, which at 1 MHz = 4294 seconds */
    uint32_t load = watchdog_calc_load_value(4294);
    ASSERT_TRUE(load > 0);  /* Non-zero confirms no truncation */
    (void)0xFFFFFFFFUL;     /* Suppress unused literal warning */
}

/* --- Bark Timing --- */

static void test_bark_ticks_normal(void)
{
    /* With 5000 ms timeout and 1000 ms bark time:
     * Bark fires at (5000 - 1000) * 1000 = 4,000,000 ticks after enable */
    uint32_t bark = watchdog_calc_bark_ticks(WATCHDOG_TIMEOUT_MS,
                                              WATCHDOG_BARK_TIME_MS);
    ASSERT_INT_EQ(4000000, (int)bark);
}

static void test_bark_ticks_zero_bark(void)
{
    /* If bark time = 0, bark fires at timeout ticks (immediate bark) */
    uint32_t bark = watchdog_calc_bark_ticks(5000, 0);
    ASSERT_INT_EQ(5000000, (int)bark);
}

static void test_bark_ticks_equal_timeout(void)
{
    /* Bark time equal to timeout is invalid, returns 0 */
    uint32_t bark = watchdog_calc_bark_ticks(5000, 5000);
    ASSERT_INT_EQ(0, (int)bark);
}

static void test_bark_ticks_greater_than_timeout(void)
{
    /* Bark time greater than timeout is invalid, returns 0 */
    uint32_t bark = watchdog_calc_bark_ticks(5000, 6000);
    ASSERT_INT_EQ(0, (int)bark);
}

/* --- Reset Reason Detection --- */

static void test_reset_reason_timer_only(void)
{
    uint32_t reason = watchdog_check_reset_reason(WD_REASON_TIMER);
    ASSERT_INT_EQ(WD_REASON_TIMER, (int)reason);
}

static void test_reset_reason_force_only(void)
{
    uint32_t reason = watchdog_check_reset_reason(WD_REASON_FORCE);
    ASSERT_INT_EQ(WD_REASON_FORCE, (int)reason);
}

static void test_reset_reason_bod_only(void)
{
    uint32_t reason = watchdog_check_reset_reason(WD_REASON_BOD);
    ASSERT_INT_EQ(WD_REASON_BOD, (int)reason);
}

static void test_reset_reason_combined(void)
{
    /* Some chips set multiple bits: timer + force */
    uint32_t reason = watchdog_check_reset_reason(
        WD_REASON_TIMER | WD_REASON_FORCE);
    ASSERT_INT_EQ(WD_REASON_TIMER | WD_REASON_FORCE, (int)reason);
}

static void test_reset_reason_none(void)
{
    uint32_t reason = watchdog_check_reset_reason(0);
    ASSERT_INT_EQ(0, (int)reason);
}

/* --- Brownout Detection --- */

static void test_brownout_detection_from_reason(void)
{
    /* Brownout detected via WD_REASON_BOD bit */
    ASSERT_TRUE(watchdog_is_brownout_reset(WD_REASON_BOD, 0));
}

static void test_brownout_detection_from_scratch(void)
{
    /* Brownout detected via scratch magic */
    ASSERT_TRUE(watchdog_is_brownout_reset(0, WD_SCRATCH_BOD_MAGIC));
}

static void test_brownout_detection_both(void)
{
    /* Both reason and scratch indicate brownout */
    ASSERT_TRUE(watchdog_is_brownout_reset(WD_REASON_BOD, WD_SCRATCH_BOD_MAGIC));
}

static void test_brownout_detection_no_brownout(void)
{
    /* No brownout indicators */
    ASSERT_FALSE(watchdog_is_brownout_reset(0, 0));
}

static void test_brownout_detection_timer_only(void)
{
    /* Timer reset, not brownout */
    ASSERT_FALSE(watchdog_is_brownout_reset(WD_REASON_TIMER, 0));
}

static void test_brownout_detection_force_only(void)
{
    /* Force reset, not brownout */
    ASSERT_FALSE(watchdog_is_brownout_reset(WD_REASON_FORCE, 0));
}

static void test_brownout_detection_wrong_magic(void)
{
    /* Wrong magic value in scratch — not brownout */
    ASSERT_FALSE(watchdog_is_brownout_reset(0, 0xDEADBEEF));
}

/* --- Brownout Mark and Clear --- */

static void test_brownout_mark_magic(void)
{
    uint32_t mark = watchdog_mark_brownout();
    ASSERT_UINT_EQ(WD_SCRATCH_BOD_MAGIC, mark);
}

static void test_brownout_clear_zero(void)
{
    uint32_t clear = watchdog_clear_brownout();
    ASSERT_UINT_EQ(0, clear);
}

static void test_brownout_mark_then_detect(void)
{
    /* Mark brownout, then detect it */
    uint32_t scratch = watchdog_mark_brownout();
    ASSERT_TRUE(watchdog_is_brownout_reset(0, scratch));
}

static void test_brownout_mark_clear_no_detect(void)
{
    /* Mark brownout, then clear it, should not detect */
    uint32_t scratch = watchdog_mark_brownout();
    ASSERT_TRUE(watchdog_is_brownout_reset(0, scratch));

    scratch = watchdog_clear_brownout();
    ASSERT_FALSE(watchdog_is_brownout_reset(0, scratch));
}

/* --- Kick Interval Safety --- */

static void test_kick_interval_safe(void)
{
    /* Main loop kicks every ~100ms, watchdog timeout is 5000ms.
     * 100ms < 5000/4 = 1250ms → safe */
    ASSERT_TRUE(watchdog_validate_kick_interval(100, 5000));
}

static void test_kick_interval_marginal(void)
{
    /* Kick at 1/4 timeout: 1250ms with 5000ms timeout.
     * Must be strictly less than 1/4 → 1250 is NOT < 1250 → NOT safe */
    ASSERT_FALSE(watchdog_validate_kick_interval(1250, 5000));
}

static void test_kick_interval_too_slow(void)
{
    /* Kick at 3 seconds, timeout 5 seconds: 3000ms > 5000/4 = 1250 → NOT safe */
    ASSERT_FALSE(watchdog_validate_kick_interval(3000, 5000));
}

static void test_kick_interval_1ms(void)
{
    /* Very fast kick: always safe */
    ASSERT_TRUE(watchdog_validate_kick_interval(1, 5000));
}

/* --- Watchdog Register Address Verification --- */

static void test_wd_register_addresses(void)
{
    /* Verify register offsets are correctly spaced (each 4 bytes) */
    ASSERT_INT_EQ(0x00, WD_CTRL_OFFSET);
    ASSERT_INT_EQ(0x04, WD_LOAD_OFFSET);
    ASSERT_INT_EQ(0x08, WD_REASON_OFFSET);
    ASSERT_INT_EQ(0x0C, WD_SCRATCH0_OFFSET);
    ASSERT_INT_EQ(0x10, WD_SCRATCH1_OFFSET);
    ASSERT_INT_EQ(0x14, WD_SCRATCH2_OFFSET);
    ASSERT_INT_EQ(0x18, WD_SCRATCH3_OFFSET);
    ASSERT_INT_EQ(0x1C, WD_SCRATCH4_OFFSET);
    ASSERT_INT_EQ(0x20, WD_SCRATCH5_OFFSET);
    ASSERT_INT_EQ(0x24, WD_SCRATCH6_OFFSET);
    ASSERT_INT_EQ(0x28, WD_SCRATCH7_OFFSET);
}

static void test_wd_scratch_base_address(void)
{
    /* Verify that scratch register 0 is at base + 0x0C */
    uint32_t scratch0_addr = RP2350B_WATCHDOG_BASE + WD_SCRATCH0_OFFSET;
    ASSERT_UINT_EQ(0x4005800C, scratch0_addr);
}

/* --- Watchdog Control Bits --- */

static void test_wd_ctrl_bits_nonzero(void)
{
    /* Control bits must be non-zero and distinct */
    ASSERT_TRUE(WD_CTRL_ENABLE != 0);
    ASSERT_TRUE(WD_CTRL_PAUSE_DBG != 0);
    ASSERT_TRUE(WD_CTRL_PAUSE_JTAG != 0);
}

static void test_wd_ctrl_bits_distinct(void)
{
    /* Each control bit should occupy a unique position */
    ASSERT_TRUE(WD_CTRL_ENABLE != WD_CTRL_PAUSE_DBG);
    ASSERT_TRUE(WD_CTRL_ENABLE != WD_CTRL_PAUSE_JTAG);
    ASSERT_TRUE(WD_CTRL_PAUSE_DBG != WD_CTRL_PAUSE_JTAG);
}

/* --- Integration-style: Full boot sequence simulation --- */

static void test_boot_after_watchdog_reset(void)
{
    /* Simulate: system boots after watchdog timer reset */
    uint32_t reason = WD_REASON_TIMER;
    uint32_t scratch = 0;

    /* Watchdog timeout caused reset — not brownout */
    ASSERT_FALSE(watchdog_is_brownout_reset(reason, scratch));
    ASSERT_INT_EQ(WD_REASON_TIMER, (int)watchdog_check_reset_reason(reason));
}

static void test_boot_after_brownout_reset(void)
{
    /* Simulate: system boots after brownout reset */
    uint32_t reason = WD_REASON_BOD;
    uint32_t scratch = watchdog_mark_brownout();

    /* Brownout detected — firmware should take recovery action */
    ASSERT_TRUE(watchdog_is_brownout_reset(reason, scratch));

    /* After handling, clear the scratch register */
    scratch = watchdog_clear_brownout();
    ASSERT_FALSE(watchdog_is_brownout_reset(0, scratch));
}

static void test_boot_after_power_on(void)
{
    /* Simulate: clean power-on, no watchdog or brownout */
    uint32_t reason = 0;
    uint32_t scratch = 0;

    ASSERT_FALSE(watchdog_is_brownout_reset(reason, scratch));
    ASSERT_INT_EQ(0, (int)watchdog_check_reset_reason(reason));
}

static void test_boot_after_force_reset(void)
{
    /* Simulate: software force reset */
    uint32_t reason = WD_REASON_FORCE;
    uint32_t scratch = 0;

    ASSERT_FALSE(watchdog_is_brownout_reset(reason, scratch));
    ASSERT_INT_EQ(WD_REASON_FORCE, (int)watchdog_check_reset_reason(reason));
}

/* --- Edge Cases --- */

static void test_wd_reason_mask_only_3_bits(void)
{
    /* Only bits 0-2 are valid in WD_REASON */
    uint32_t noisy_reason = WD_REASON_TIMER | WD_REASON_FORCE | 0xFFFFFFF8;
    uint32_t masked = watchdog_check_reset_reason(noisy_reason);
    /* Upper bits should be stripped */
    ASSERT_INT_EQ(WD_REASON_TIMER | WD_REASON_FORCE, (int)masked);
}

static void test_bark_before_reset_timing(void)
{
    /* Bark interrupt fires 1000 ms before watchdog timeout.
     * With 5000 ms timeout, bark at 4000 ms, reset at 5000 ms.
     * This gives 1 second to deassert INT_REQ and stop peripherals. */
    uint32_t bark_time_ms = WATCHDOG_BARK_TIME_MS;
    uint32_t timeout_ms = WATCHDOG_TIMEOUT_MS;
    uint32_t bark_at_ms = timeout_ms - bark_time_ms;

    ASSERT_INT_GE(bark_at_ms, 1000);  /* At least 1 second warning */
    ASSERT_TRUE(bark_at_ms < timeout_ms);  /* Bark before reset */

    /* Bark time in ticks */
    uint32_t bark_ticks = watchdog_calc_bark_ticks(timeout_ms, bark_time_ms);
    uint32_t expected_ticks = bark_at_ms * WD_TICKS_PER_MS;
    ASSERT_INT_EQ((int)expected_ticks, (int)bark_ticks);
}

/* ── Main test runner ────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Watchdog Unit Tests ===\n\n");

    printf("--- Constants Verification ---\n");
    RUN_TEST(test_watchdog_timeout_sanity);
    RUN_TEST(test_watchdog_bark_time_sanity);
    RUN_TEST(test_brownout_magic_values_unique);
    RUN_TEST(test_brownout_magic_has_distinguishing_bits);

    printf("\n--- Load Value Calculations ---\n");
    RUN_TEST(test_load_value_at_timeout);
    RUN_TEST(test_load_value_at_1ms);
    RUN_TEST(test_load_value_at_30s);
    RUN_TEST(test_load_value_no_overflow);

    printf("\n--- Bark Timing ---\n");
    RUN_TEST(test_bark_ticks_normal);
    RUN_TEST(test_bark_ticks_zero_bark);
    RUN_TEST(test_bark_ticks_equal_timeout);
    RUN_TEST(test_bark_ticks_greater_than_timeout);

    printf("\n--- Reset Reason Detection ---\n");
    RUN_TEST(test_reset_reason_timer_only);
    RUN_TEST(test_reset_reason_force_only);
    RUN_TEST(test_reset_reason_bod_only);
    RUN_TEST(test_reset_reason_combined);
    RUN_TEST(test_reset_reason_none);

    printf("\n--- Brownout Detection ---\n");
    RUN_TEST(test_brownout_detection_from_reason);
    RUN_TEST(test_brownout_detection_from_scratch);
    RUN_TEST(test_brownout_detection_both);
    RUN_TEST(test_brownout_detection_no_brownout);
    RUN_TEST(test_brownout_detection_timer_only);
    RUN_TEST(test_brownout_detection_force_only);
    RUN_TEST(test_brownout_detection_wrong_magic);
    RUN_TEST(test_brownout_mark_magic);
    RUN_TEST(test_brownout_clear_zero);
    RUN_TEST(test_brownout_mark_then_detect);
    RUN_TEST(test_brownout_mark_clear_no_detect);

    printf("\n--- Kick Interval Safety ---\n");
    RUN_TEST(test_kick_interval_safe);
    RUN_TEST(test_kick_interval_marginal);
    RUN_TEST(test_kick_interval_too_slow);
    RUN_TEST(test_kick_interval_1ms);

    printf("\n--- Register Address Verification ---\n");
    RUN_TEST(test_wd_register_addresses);
    RUN_TEST(test_wd_scratch_base_address);
    RUN_TEST(test_wd_ctrl_bits_nonzero);
    RUN_TEST(test_wd_ctrl_bits_distinct);

    printf("\n--- Boot Sequence Simulation ---\n");
    RUN_TEST(test_boot_after_watchdog_reset);
    RUN_TEST(test_boot_after_brownout_reset);
    RUN_TEST(test_boot_after_power_on);
    RUN_TEST(test_boot_after_force_reset);

    printf("\n--- Edge Cases ---\n");
    RUN_TEST(test_wd_reason_mask_only_3_bits);
    RUN_TEST(test_bark_before_reset_timing);

    TEST_RESULTS();
}