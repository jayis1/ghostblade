/*
 * test_sleep_wake.c — Unit Tests for Sleep/Wake State Machine
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the RP2350B sleep/wake state machine logic:
 *   1. State transitions (IDLE → LIGHT → DEEP)
 *   2. Wake-on-SPI activity detection
 *   3. Force wake from any state
 *   4. Timeout threshold boundary conditions
 *   5. Idle time tracking
 *
 * Since the sleep_wake module depends on RP2350B hardware (clocks,
 * multicore), we test the state machine logic by reimplementing the
 * core decision logic in pure C.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_sleep_wake test_sleep_wake.c
 *
 * Run:
 *   ./test_sleep_wake
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Sleep state definitions (mirrors sleep_wake.h)
 * ======================================================================== */

enum sleep_state {
    SLEEP_IDLE = 0,
    SLEEP_LIGHT,
    SLEEP_DEEP,
    SLEEP_STATE_COUNT
};

#define SLEEP_IDLE_TIMEOUT_MS    5000
#define SLEEP_LIGHT_TIMEOUT_MS   30000
#define SLEEP_MIN_IDLE_MS        100

/* ========================================================================
 * Minimal Test Framework
 * ======================================================================== */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(actual, expected, msg) do {                            \
    tests_run++;                                                         \
    if ((actual) == (expected)) {                                         \
        tests_passed++;                                                   \
    } else {                                                              \
        tests_failed++;                                                   \
        fprintf(stderr, "  FAIL [%s:%d]: %s — expected %d, got %d\n",    \
                __FILE__, __LINE__, msg, (int)(expected), (int)(actual)); \
    }                                                                     \
} while (0)

#define ASSERT_TRUE(condition, msg) do {                                  \
    tests_run++;                                                         \
    if (condition) {                                                       \
        tests_passed++;                                                   \
    } else {                                                              \
        tests_failed++;                                                   \
        fprintf(stderr, "  FAIL [%s:%d]: %s — expected true\n",           \
                __FILE__, __LINE__, msg);                                 \
    }                                                                     \
} while (0)

#define ASSERT_NEQ(actual, not_expected, msg) do {                       \
    tests_run++;                                                         \
    if ((actual) != (not_expected)) {                                     \
        tests_passed++;                                                   \
    } else {                                                              \
        tests_failed++;                                                   \
        fprintf(stderr, "  FAIL [%s:%d]: %s — should not equal %d\n",    \
                __FILE__, __LINE__, msg, (int)(not_expected));            \
    }                                                                     \
} while (0)

#define RUN_TEST(func) do { \
    printf("Running: %s\n", #func); \
    func(); \
} while(0)

/* ========================================================================
 * Reimplemented state machine logic (pure, no hardware dependencies)
 * ======================================================================== */

/**
 * sleep_wake_next_state — Compute next state based on current state and idle time
 *
 * This is the pure state transition function extracted from sleep_wake.c.
 * It determines the next state based on:
 *   - Current state
 *   - Time since last SPI activity (idle_ms)
 *   - Whether SPI activity was detected this cycle
 *
 * Returns: Next sleep state
 */
static enum sleep_state sleep_wake_next_state(enum sleep_state current,
                                                uint32_t idle_ms,
                                                bool has_activity)
{
    /* Any activity immediately returns to IDLE */
    if (has_activity) {
        return SLEEP_IDLE;
    }

    switch (current) {
    case SLEEP_IDLE:
        if (idle_ms >= SLEEP_IDLE_TIMEOUT_MS) {
            return SLEEP_LIGHT;
        }
        return SLEEP_IDLE;

    case SLEEP_LIGHT:
        if (idle_ms >= SLEEP_LIGHT_TIMEOUT_MS) {
            return SLEEP_DEEP;
        }
        return SLEEP_LIGHT;

    case SLEEP_DEEP:
        /* Stay in deep sleep until activity detected */
        return SLEEP_DEEP;

    default:
        return SLEEP_IDLE;
    }
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test 1: Initial state should be IDLE */
static void test_initial_state(void) {
    printf("  Testing initial state is SLEEP_IDLE...\n");

    enum sleep_state state = SLEEP_IDLE;
    ASSERT_EQ(state, SLEEP_IDLE, "Initial state is IDLE");
}

/* Test 2: IDLE stays IDLE with recent activity */
static void test_idle_with_activity(void) {
    printf("  Testing IDLE stays IDLE with recent activity...\n");

    /* Fresh activity — should stay in IDLE regardless of idle time */
    enum sleep_state next;

    next = sleep_wake_next_state(SLEEP_IDLE, 0, true);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE + activity → IDLE (0ms idle)");

    next = sleep_wake_next_state(SLEEP_IDLE, 100, true);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE + activity → IDLE (100ms idle)");

    next = sleep_wake_next_state(SLEEP_IDLE, 60000, true);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE + activity → IDLE (60s idle)");
}

/* Test 3: IDLE → LIGHT transition at timeout boundary */
static void test_idle_to_light_transition(void) {
    printf("  Testing IDLE → LIGHT transition at timeout boundary...\n");

    enum sleep_state next;

    /* Below threshold — should stay IDLE */
    next = sleep_wake_next_state(SLEEP_IDLE, 4999, false);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE + 4999ms idle → IDLE (just under threshold)");

    /* At threshold — should transition to LIGHT */
    next = sleep_wake_next_state(SLEEP_IDLE, 5000, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "IDLE + 5000ms idle → LIGHT (at threshold)");

    /* Well above threshold — should transition to LIGHT */
    next = sleep_wake_next_state(SLEEP_IDLE, 10000, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "IDLE + 10000ms idle → LIGHT");
}

/* Test 4: LIGHT → DEEP transition at timeout boundary */
static void test_light_to_deep_transition(void) {
    printf("  Testing LIGHT → DEEP transition at timeout boundary...\n");

    enum sleep_state next;

    /* Below threshold — should stay in LIGHT */
    next = sleep_wake_next_state(SLEEP_LIGHT, 29999, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "LIGHT + 29999ms idle → LIGHT (just under threshold)");

    /* At threshold — should transition to DEEP */
    next = sleep_wake_next_state(SLEEP_LIGHT, 30000, false);
    ASSERT_EQ(next, SLEEP_DEEP, "LIGHT + 30000ms idle → DEEP (at threshold)");

    /* Well above threshold — should stay in DEEP */
    next = sleep_wake_next_state(SLEEP_LIGHT, 120000, false);
    ASSERT_EQ(next, SLEEP_DEEP, "LIGHT + 120000ms idle → DEEP");
}

/* Test 5: DEEP stays DEEP without activity */
static void test_deep_persistent(void) {
    printf("  Testing DEEP stays DEEP without activity...\n");

    enum sleep_state next;

    /* DEEP should persist regardless of idle time */
    next = sleep_wake_next_state(SLEEP_DEEP, 0, false);
    ASSERT_EQ(next, SLEEP_DEEP, "DEEP + no activity → DEEP (0ms)");

    next = sleep_wake_next_state(SLEEP_DEEP, 60000, false);
    ASSERT_EQ(next, SLEEP_DEEP, "DEEP + no activity → DEEP (60s)");

    next = sleep_wake_next_state(SLEEP_DEEP, 3600000, false);
    ASSERT_EQ(next, SLEEP_DEEP, "DEEP + no activity → DEEP (1 hour)");
}

/* Test 6: Activity wakes from any state */
static void test_activity_wakes_all_states(void) {
    printf("  Testing activity detection wakes from any state...\n");

    enum sleep_state next;

    /* Activity in LIGHT state → IDLE */
    next = sleep_wake_next_state(SLEEP_LIGHT, 10000, true);
    ASSERT_EQ(next, SLEEP_IDLE, "LIGHT + activity → IDLE");

    /* Activity in DEEP state → IDLE */
    next = sleep_wake_next_state(SLEEP_DEEP, 60000, true);
    ASSERT_EQ(next, SLEEP_IDLE, "DEEP + activity → IDLE");

    /* Activity in IDLE state → IDLE (no transition needed) */
    next = sleep_wake_next_state(SLEEP_IDLE, 0, true);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE + activity → IDLE");
}

/* Test 7: Full state sequence IDLE → LIGHT → DEEP → IDLE */
static void test_full_state_sequence(void) {
    printf("  Testing full state sequence IDLE → LIGHT → DEEP → IDLE...\n");

    enum sleep_state state = SLEEP_IDLE;

    /* Start in IDLE, no activity for 5000ms → LIGHT */
    state = sleep_wake_next_state(state, 5000, false);
    ASSERT_EQ(state, SLEEP_LIGHT, "Step 1: IDLE → LIGHT");

    /* Stay in LIGHT, no activity for 29999ms → still LIGHT */
    state = sleep_wake_next_state(state, 29999, false);
    ASSERT_EQ(state, SLEEP_LIGHT, "Step 2: LIGHT stays LIGHT");

    /* No activity for 30000ms → DEEP */
    state = sleep_wake_next_state(state, 30000, false);
    ASSERT_EQ(state, SLEEP_DEEP, "Step 3: LIGHT → DEEP");

    /* Activity detected → IDLE */
    state = sleep_wake_next_state(state, 60000, true);
    ASSERT_EQ(state, SLEEP_IDLE, "Step 4: DEEP → IDLE (activity)");
}

/* Test 8: State values are distinct */
static void test_state_values_distinct(void) {
    printf("  Testing state values are distinct...\n");

    ASSERT_NEQ(SLEEP_IDLE, SLEEP_LIGHT, "IDLE != LIGHT");
    ASSERT_NEQ(SLEEP_IDLE, SLEEP_DEEP, "IDLE != DEEP");
    ASSERT_NEQ(SLEEP_LIGHT, SLEEP_DEEP, "LIGHT != DEEP");
    ASSERT_EQ(SLEEP_IDLE, 0, "IDLE = 0");
    ASSERT_EQ(SLEEP_LIGHT, 1, "LIGHT = 1");
    ASSERT_EQ(SLEEP_DEEP, 2, "DEEP = 2");
    ASSERT_EQ(SLEEP_STATE_COUNT, 3, "STATE_COUNT = 3");
}

/* Test 9: Timeout threshold values */
static void test_timeout_values(void) {
    printf("  Testing timeout threshold values...\n");

    ASSERT_EQ(SLEEP_IDLE_TIMEOUT_MS, 5000, "IDLE timeout = 5000ms");
    ASSERT_EQ(SLEEP_LIGHT_TIMEOUT_MS, 30000, "LIGHT timeout = 30000ms");
    ASSERT_EQ(SLEEP_MIN_IDLE_MS, 100, "MIN_IDLE = 100ms");
}

/* Test 10: Rapid activity/idle alternation */
static void test_rapid_activity_alternation(void) {
    printf("  Testing rapid activity/idle alternation...\n");

    enum sleep_state state = SLEEP_IDLE;

    /* Alternating activity/no-activity with small idle times
     * should keep the state in IDLE */
    for (int i = 0; i < 100; i++) {
        state = sleep_wake_next_state(state, 50, true);
        ASSERT_EQ(state, SLEEP_IDLE, "Rapid alternation stays IDLE");
        state = sleep_wake_next_state(state, 50, false);
        /* With 50ms idle, we're well below the 5000ms threshold */
        ASSERT_EQ(state, SLEEP_IDLE, "Short idle stays IDLE");
    }
}

/* Test 11: Boundary at exactly 0 ms idle */
static void test_zero_idle_time(void) {
    printf("  Testing zero idle time boundary...\n");

    enum sleep_state next;

    /* 0 ms idle, no activity — just entered idle, should stay */
    next = sleep_wake_next_state(SLEEP_IDLE, 0, false);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE + 0ms idle → IDLE");

    next = sleep_wake_next_state(SLEEP_LIGHT, 0, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "LIGHT + 0ms idle → LIGHT");

    next = sleep_wake_next_state(SLEEP_DEEP, 0, false);
    ASSERT_EQ(next, SLEEP_DEEP, "DEEP + 0ms idle → DEEP");
}

/* Test 12: UINT32_MAX idle time edge case */
static void test_max_idle_time(void) {
    printf("  Testing UINT32_MAX idle time edge case...\n");

    enum sleep_state next;

    /* Maximum idle time should cause transitions */
    next = sleep_wake_next_state(SLEEP_IDLE, UINT32_MAX, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "IDLE + UINT32_MAX idle → LIGHT");

    next = sleep_wake_next_state(SLEEP_LIGHT, UINT32_MAX, false);
    ASSERT_EQ(next, SLEEP_DEEP, "LIGHT + UINT32_MAX idle → DEEP");

    /* Even UINT32_MAX idle with activity should return to IDLE */
    next = sleep_wake_next_state(SLEEP_DEEP, UINT32_MAX, true);
    ASSERT_EQ(next, SLEEP_IDLE, "DEEP + activity → IDLE");
}

/* Test 13: Multiple transitions back and forth */
static void test_multiple_transitions(void) {
    printf("  Testing multiple transitions back and forth...\n");

    enum sleep_state state = SLEEP_IDLE;

    /* Cycle 1: IDLE → LIGHT → IDLE */
    state = sleep_wake_next_state(state, SLEEP_IDLE_TIMEOUT_MS, false);
    ASSERT_EQ(state, SLEEP_LIGHT, "Cycle 1: IDLE → LIGHT");
    state = sleep_wake_next_state(state, 100, true);
    ASSERT_EQ(state, SLEEP_IDLE, "Cycle 1: LIGHT → IDLE (activity)");

    /* Cycle 2: IDLE → LIGHT → DEEP → IDLE */
    state = sleep_wake_next_state(state, SLEEP_IDLE_TIMEOUT_MS, false);
    ASSERT_EQ(state, SLEEP_LIGHT, "Cycle 2: IDLE → LIGHT");
    state = sleep_wake_next_state(state, SLEEP_LIGHT_TIMEOUT_MS, false);
    ASSERT_EQ(state, SLEEP_DEEP, "Cycle 2: LIGHT → DEEP");
    state = sleep_wake_next_state(state, 5000, true);
    ASSERT_EQ(state, SLEEP_IDLE, "Cycle 2: DEEP → IDLE (activity)");

    /* Cycle 3: Quick activity in IDLE */
    state = sleep_wake_next_state(state, 1000, false);
    ASSERT_EQ(state, SLEEP_IDLE, "Cycle 3: IDLE stays (1000ms < 5000ms)");
}

/* Test 14: Verify transition thresholds with off-by-one */
static void test_transition_threshold_off_by_one(void) {
    printf("  Testing transition threshold off-by-one conditions...\n");

    enum sleep_state next;

    /* IDLE_TIMEOUT - 1: should stay IDLE */
    next = sleep_wake_next_state(SLEEP_IDLE, SLEEP_IDLE_TIMEOUT_MS - 1, false);
    ASSERT_EQ(next, SLEEP_IDLE, "IDLE at threshold - 1 → IDLE");

    /* IDLE_TIMEOUT: should transition */
    next = sleep_wake_next_state(SLEEP_IDLE, SLEEP_IDLE_TIMEOUT_MS, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "IDLE at threshold → LIGHT");

    /* IDLE_TIMEOUT + 1: should definitely transition */
    next = sleep_wake_next_state(SLEEP_IDLE, SLEEP_IDLE_TIMEOUT_MS + 1, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "IDLE at threshold + 1 → LIGHT");

    /* LIGHT_TIMEOUT - 1: should stay LIGHT */
    next = sleep_wake_next_state(SLEEP_LIGHT, SLEEP_LIGHT_TIMEOUT_MS - 1, false);
    ASSERT_EQ(next, SLEEP_LIGHT, "LIGHT at threshold - 1 → LIGHT");

    /* LIGHT_TIMEOUT: should transition */
    next = sleep_wake_next_state(SLEEP_LIGHT, SLEEP_LIGHT_TIMEOUT_MS, false);
    ASSERT_EQ(next, SLEEP_DEEP, "LIGHT at threshold → DEEP");

    /* LIGHT_TIMEOUT + 1: should definitely transition */
    next = sleep_wake_next_state(SLEEP_LIGHT, SLEEP_LIGHT_TIMEOUT_MS + 1, false);
    ASSERT_EQ(next, SLEEP_DEEP, "LIGHT at threshold + 1 → DEEP");
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void) {
    printf("=== GhostBlade Sleep/Wake State Machine Unit Tests ===\n\n");

    RUN_TEST(test_initial_state);
    RUN_TEST(test_idle_with_activity);
    RUN_TEST(test_idle_to_light_transition);
    RUN_TEST(test_light_to_deep_transition);
    RUN_TEST(test_deep_persistent);
    RUN_TEST(test_activity_wakes_all_states);
    RUN_TEST(test_full_state_sequence);
    RUN_TEST(test_state_values_distinct);
    RUN_TEST(test_timeout_values);
    RUN_TEST(test_rapid_activity_alternation);
    RUN_TEST(test_zero_idle_time);
    RUN_TEST(test_max_idle_time);
    RUN_TEST(test_multiple_transitions);
    RUN_TEST(test_transition_threshold_off_by_one);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}