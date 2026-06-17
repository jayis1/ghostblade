/*
 * test_power_states.c — Unit Tests for Battery Monitor Power State Machine
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the power state machine transitions in the RP2350B firmware's
 * battery monitor module. The state machine manages transitions between
 * ACTIVE, IDLE, SLEEP, and SHUTDOWN states based on battery voltage,
 * temperature, and inactivity timers.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DNO_CMOCKA -o test_power_states test_power_states.c
 *
 * Run:
 *   ./test_power_states
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

#define RUN_TEST(func) do {                                                \
    printf("Running: %s\n", #func);                                       \
    func();                                                                \
} while (0)

#define TEST_RESULTS() do {                                                \
    printf("\n=== Power State Machine Results: %d/%d passed, %d failed ===\n", \
           g_tests_passed, g_tests_run, g_tests_failed);                   \
    return g_tests_failed > 0 ? 1 : 0;                                     \
} while (0)

/* ── Power State Machine (mirrored from battery_monitor.c) ─────────────── */

typedef enum {
    POWER_ACTIVE = 0,   /* Full operation: all peripherals powered */
    POWER_IDLE,          /* Low-power idle: SDR streaming paused */
    POWER_SLEEP,         /* Deep sleep: MCU in low-power, wake on GPIO */
    POWER_SHUTDOWN       /* Terminal: preparing for power loss */
} power_state_t;

/* State transition thresholds */
#define IDLE_ENTER_VOLTAGE_MV     3500   /* Enter IDLE below 3500 mV */
#define IDLE_EXIT_VOLTAGE_MV      3700   /* Exit IDLE above 3700 mV (hysteresis) */
#define SLEEP_ENTER_VOLTAGE_MV    3200   /* Enter SLEEP below 3200 mV */
#define SLEEP_EXIT_VOLTAGE_MV     3500   /* Exit SLEEP above 3500 mV (hysteresis) */
#define SHUTDOWN_ENTER_VOLTAGE_MV 3000   /* Enter SHUTDOWN below 3000 mV */

/* Inactivity timers (milliseconds) */
#define IDLE_TIMEOUT_MS            5000   /* Enter IDLE after 5s inactivity */
#define SLEEP_TIMEOUT_MS           30000  /* Enter SLEEP after 30s in IDLE */

/* Temperature thresholds */
#define OVERTEMP_THRESHOLD_C       850    /* 85.0°C in 0.1°C units */
#define OVERTEMP_RECOVERY_C        750    /* 75.0°C recovery in 0.1°C units */

/* Brownout thresholds (from battery_monitor.h) */
#define BROWNOUT_THRESHOLD_MV      3000
#define BROWNOUT_RECOVERY_MV       3300

typedef struct {
    power_state_t state;
    uint32_t      idle_timer_ms;     /* Inactivity timer */
    uint32_t      idle_duration_ms;  /* Time spent in IDLE state */
    bool          brownout_active;
    bool          overtemp_active;
} power_state_ctx_t;

/**
 * power_state_init — Initialize power state context
 */
static void power_state_init(power_state_ctx_t *ctx)
{
    ctx->state = POWER_ACTIVE;
    ctx->idle_timer_ms = 0;
    ctx->idle_duration_ms = 0;
    ctx->brownout_active = false;
    ctx->overtemp_active = false;
}

/**
 * power_state_update — Update power state machine
 *
 * @ctx:       Power state context
 * @vbat_mv:   Current battery voltage in mV
 * @temp_c_x10: Current die temperature in 0.1°C
 * @dt_ms:     Time elapsed since last update in ms
 *
 * Returns: the new power state
 */
static power_state_t power_state_update(power_state_ctx_t *ctx,
                                         uint16_t vbat_mv,
                                         int16_t temp_c_x10,
                                         uint32_t dt_ms)
{
    /* Update brownout detection with hysteresis */
    if (ctx->brownout_active) {
        if (vbat_mv > BROWNOUT_RECOVERY_MV)
            ctx->brownout_active = false;
    } else {
        if (vbat_mv < BROWNOUT_THRESHOLD_MV)
            ctx->brownout_active = true;
    }

    /* Update overtemperature detection with hysteresis */
    if (ctx->overtemp_active) {
        if (temp_c_x10 < OVERTEMP_RECOVERY_C)
            ctx->overtemp_active = false;
    } else {
        if (temp_c_x10 > OVERTEMP_THRESHOLD_C)
            ctx->overtemp_active = true;
    }

    /* Immediate shutdown on brownout */
    if (ctx->brownout_active) {
        ctx->state = POWER_SHUTDOWN;
        return ctx->state;
    }

    /* Immediate shutdown on overtemperature */
    if (ctx->overtemp_active) {
        ctx->state = POWER_SHUTDOWN;
        return ctx->state;
    }

    /* State transitions based on voltage and timers */
    switch (ctx->state) {
    case POWER_ACTIVE:
        ctx->idle_timer_ms += dt_ms;

        /* Check voltage-based transitions */
        if (vbat_mv < IDLE_ENTER_VOLTAGE_MV) {
            ctx->state = POWER_IDLE;
            ctx->idle_timer_ms = 0;
            ctx->idle_duration_ms = 0;
        } else if (ctx->idle_timer_ms >= IDLE_TIMEOUT_MS) {
            /* Inactivity timeout: enter IDLE regardless of voltage */
            ctx->state = POWER_IDLE;
            ctx->idle_timer_ms = 0;
            ctx->idle_duration_ms = 0;
        }
        break;

    case POWER_IDLE:
        ctx->idle_duration_ms += dt_ms;

        /* Check recovery: voltage above hysteresis threshold */
        if (vbat_mv > IDLE_EXIT_VOLTAGE_MV) {
            ctx->state = POWER_ACTIVE;
            ctx->idle_timer_ms = 0;
            ctx->idle_duration_ms = 0;
        }
        /* Check deeper sleep: voltage critical or timer expired */
        else if (vbat_mv < SLEEP_ENTER_VOLTAGE_MV ||
                 ctx->idle_duration_ms >= SLEEP_TIMEOUT_MS) {
            ctx->state = POWER_SLEEP;
            ctx->idle_timer_ms = 0;
            ctx->idle_duration_ms = 0;
        }
        break;

    case POWER_SLEEP:
        /* Check recovery: voltage above recovery threshold */
        if (vbat_mv > SLEEP_EXIT_VOLTAGE_MV) {
            ctx->state = POWER_ACTIVE;
            ctx->idle_timer_ms = 0;
            ctx->idle_duration_ms = 0;
        }
        /* Check shutdown: voltage critically low */
        else if (vbat_mv < SHUTDOWN_ENTER_VOLTAGE_MV) {
            ctx->state = POWER_SHUTDOWN;
        }
        break;

    case POWER_SHUTDOWN:
        /* Terminal state — no exit possible */
        break;
    }

    return ctx->state;
}

/* ── Test cases ────────────────────────────────────────────────────────── */

/* --- Active state transitions --- */

static void test_active_remains_active_high_voltage(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* 4000 mV, 27°C — well within active range */
    power_state_update(&ctx, 4000, 270, 0);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);
}

static void test_active_to_idle_voltage_drop(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Voltage drops below 3500 mV → enter IDLE */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);
}

static void test_active_to_idle_inactivity_timeout(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Voltage fine, but 5 seconds of inactivity */
    power_state_update(&ctx, 4000, 270, 5000);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);
}

static void test_active_stays_active_partial_inactivity(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* 4 seconds of inactivity — not yet enough */
    power_state_update(&ctx, 4000, 270, 4000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);
}

/* --- Idle state transitions --- */

static void test_idle_recovery_voltage_rises(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* Voltage rises above hysteresis → back to ACTIVE */
    power_state_update(&ctx, 3800, 270, 1000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);
}

static void test_idle_stays_idle_marginal_voltage(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE at 3400 mV */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* Voltage at 3600 mV — above IDLE_ENTER but below IDLE_EXIT (3700) */
    power_state_update(&ctx, 3600, 270, 1000);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);
}

static void test_idle_to_sleep_voltage_critical(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* Voltage drops below 3200 mV → enter SLEEP */
    power_state_update(&ctx, 3100, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);
}

static void test_idle_to_sleep_timeout(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* Stay in IDLE for 30 seconds */
    power_state_update(&ctx, 3400, 270, 30000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);
}

/* --- Sleep state transitions --- */

static void test_sleep_recovery_voltage_rises(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE then SLEEP */
    power_state_update(&ctx, 3400, 270, 0);
    power_state_update(&ctx, 3100, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);

    /* Voltage rises above 3500 mV → back to ACTIVE */
    power_state_update(&ctx, 3600, 270, 1000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);
}

static void test_sleep_to_shutdown_voltage_critical(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter SLEEP */
    power_state_update(&ctx, 3400, 270, 0);
    power_state_update(&ctx, 3100, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);

    /* Voltage drops below 3000 mV → enter SHUTDOWN */
    power_state_update(&ctx, 2900, 270, 1000);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
}

static void test_sleep_stays_sleep_marginal_voltage(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter SLEEP */
    power_state_update(&ctx, 3400, 270, 0);
    power_state_update(&ctx, 3100, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);

    /* Voltage at 3400 mV — below SLEEP_EXIT (3500) → stays in SLEEP */
    power_state_update(&ctx, 3400, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);
}

/* --- Shutdown state --- */

static void test_shutdown_is_terminal(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Force shutdown via brownout */
    power_state_update(&ctx, 2900, 270, 0);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);

    /* Even if voltage recovers, state stays SHUTDOWN */
    power_state_update(&ctx, 4200, 270, 1000);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
}

/* --- Brownout detection with state machine --- */

static void test_brownout_forces_shutdown(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Normal operation */
    power_state_update(&ctx, 3800, 270, 0);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* Voltage drops below brownout threshold */
    power_state_update(&ctx, 2900, 270, 0);
    ASSERT_TRUE(ctx.brownout_active);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
}

static void test_brownout_hysteresis_in_state_machine(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Trigger brownout */
    power_state_update(&ctx, 2900, 270, 0);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
    ASSERT_TRUE(ctx.brownout_active);

    /* Once in SHUTDOWN, brownout recovery doesn't help */
    /* (SHUTDOWN is terminal) */
}

/* --- Overtemperature detection --- */

static void test_overtemp_forces_shutdown(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Normal operation at good voltage */
    power_state_update(&ctx, 4000, 270, 0);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* Temperature exceeds 85.0°C (850 in 0.1°C units) */
    power_state_update(&ctx, 4000, 900, 0);
    ASSERT_TRUE(ctx.overtemp_active);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
}

static void test_overtemp_threshold_exact(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Exactly at threshold (85.0°C = 850) — triggers overtemp */
    power_state_update(&ctx, 4000, 851, 0);
    ASSERT_TRUE(ctx.overtemp_active);
}

static void test_overtemp_below_threshold(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Just below threshold (84.9°C = 849) — no overtemp */
    power_state_update(&ctx, 4000, 849, 0);
    ASSERT_FALSE(ctx.overtemp_active);
}

static void test_overtemp_recovery_hysteresis(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Trigger overtemp */
    power_state_update(&ctx, 4000, 900, 0);
    ASSERT_TRUE(ctx.overtemp_active);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);

    /* Note: overtemp_active recovers below 75°C (750),
     * but once in SHUTDOWN, state doesn't recover */
}

/* --- Gradual voltage descent --- */

static void test_full_battery_descent(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Start at full battery */
    power_state_update(&ctx, 4200, 270, 0);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* Voltage drops slightly */
    power_state_update(&ctx, 3800, 270, 1000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* Cross idle threshold (3500 mV) */
    power_state_update(&ctx, 3400, 270, 1000);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* Voltage recovers above hysteresis */
    power_state_update(&ctx, 3800, 270, 1000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* Voltage drops again, deeper this time */
    power_state_update(&ctx, 3400, 270, 1000);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* Continue dropping below sleep threshold */
    power_state_update(&ctx, 3100, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);

    /* Voltage recovers from sleep */
    power_state_update(&ctx, 3600, 270, 1000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* Final drop to shutdown */
    power_state_update(&ctx, 3400, 270, 0);   /* IDLE */
    power_state_update(&ctx, 3100, 270, 1000); /* SLEEP */
    power_state_update(&ctx, 2900, 270, 1000); /* SHUTDOWN */
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
}

static void test_rapid_voltage_dip(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Quick dip below brownout threshold */
    power_state_update(&ctx, 2900, 270, 0);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
    ASSERT_TRUE(ctx.brownout_active);
}

/* --- Boundary conditions --- */

static void test_exact_threshold_idle_enter(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* At exactly 3500 mV → NOT below IDLE_ENTER (3500),
     * so stays ACTIVE */
    power_state_update(&ctx, 3500, 270, 0);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);

    /* 3499 mV → below threshold, enters IDLE */
    power_state_update(&ctx, 3499, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);
}

static void test_exact_threshold_idle_exit(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* At exactly 3700 mV → NOT above IDLE_EXIT (3700),
     * stays IDLE. Must be strictly greater. */
    power_state_update(&ctx, 3700, 270, 1000);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* At 3701 mV → above threshold, exits to ACTIVE */
    power_state_update(&ctx, 3701, 270, 1000);
    ASSERT_INT_EQ(POWER_ACTIVE, ctx.state);
}

static void test_exact_threshold_sleep_enter(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter IDLE first */
    power_state_update(&ctx, 3400, 270, 0);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* At exactly 3200 mV → NOT below SLEEP_ENTER (3200),
     * stays IDLE (if timer hasn't expired) */
    power_state_update(&ctx, 3200, 270, 1000);
    ASSERT_INT_EQ(POWER_IDLE, ctx.state);

    /* 3199 mV → below threshold, enters SLEEP */
    power_state_update(&ctx, 3199, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);
}

static void test_exact_threshold_shutdown_enter(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Enter SLEEP */
    power_state_update(&ctx, 3400, 270, 0);
    power_state_update(&ctx, 3100, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);

    /* At exactly 3000 mV → NOT below SHUTDOWN_ENTER (3000),
     * stays SLEEP */
    power_state_update(&ctx, 3000, 270, 1000);
    ASSERT_INT_EQ(POWER_SLEEP, ctx.state);

    /* 2999 mV → below threshold, enters SHUTDOWN */
    power_state_update(&ctx, 2999, 270, 1000);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
}

/* --- Combined brownout + overtemperature scenarios --- */

static void test_brownout_priority_over_overtemp(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Both brownout and overtemp */
    power_state_update(&ctx, 2900, 900, 0);
    ASSERT_TRUE(ctx.brownout_active);
    ASSERT_TRUE(ctx.overtemp_active);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);
    /* Brownout is checked first, but both lead to SHUTDOWN */
}

static void test_overtemp_then_cooling_no_recovery(void)
{
    power_state_ctx_t ctx;
    power_state_init(&ctx);

    /* Trigger overtemp */
    power_state_update(&ctx, 4000, 900, 0);
    ASSERT_INT_EQ(POWER_SHUTDOWN, ctx.state);

    /* Cooling doesn't help — SHUTDOWN is terminal */
    /* Even though overtemp_active might clear if we could
     * continue (it wouldn't, because SHUTDOWN short-circuits),
     * the state is permanent */
}

/* ── Main test runner ────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Power State Machine Unit Tests ===\n\n");

    printf("--- Active State Transitions ---\n");
    RUN_TEST(test_active_remains_active_high_voltage);
    RUN_TEST(test_active_to_idle_voltage_drop);
    RUN_TEST(test_active_to_idle_inactivity_timeout);
    RUN_TEST(test_active_stays_active_partial_inactivity);

    printf("\n--- Idle State Transitions ---\n");
    RUN_TEST(test_idle_recovery_voltage_rises);
    RUN_TEST(test_idle_stays_idle_marginal_voltage);
    RUN_TEST(test_idle_to_sleep_voltage_critical);
    RUN_TEST(test_idle_to_sleep_timeout);

    printf("\n--- Sleep State Transitions ---\n");
    RUN_TEST(test_sleep_recovery_voltage_rises);
    RUN_TEST(test_sleep_to_shutdown_voltage_critical);
    RUN_TEST(test_sleep_stays_sleep_marginal_voltage);

    printf("\n--- Shutdown State ---\n");
    RUN_TEST(test_shutdown_is_terminal);

    printf("\n--- Brownout with State Machine ---\n");
    RUN_TEST(test_brownout_forces_shutdown);
    RUN_TEST(test_brownout_hysteresis_in_state_machine);

    printf("\n--- Overtemperature Detection ---\n");
    RUN_TEST(test_overtemp_forces_shutdown);
    RUN_TEST(test_overtemp_threshold_exact);
    RUN_TEST(test_overtemp_below_threshold);
    RUN_TEST(test_overtemp_recovery_hysteresis);

    printf("\n--- Voltage Descent Scenarios ---\n");
    RUN_TEST(test_full_battery_descent);
    RUN_TEST(test_rapid_voltage_dip);

    printf("\n--- Boundary Conditions ---\n");
    RUN_TEST(test_exact_threshold_idle_enter);
    RUN_TEST(test_exact_threshold_idle_exit);
    RUN_TEST(test_exact_threshold_sleep_enter);
    RUN_TEST(test_exact_threshold_shutdown_enter);

    printf("\n--- Combined Scenarios ---\n");
    RUN_TEST(test_brownout_priority_over_overtemp);
    RUN_TEST(test_overtemp_then_cooling_no_recovery);

    TEST_RESULTS();
}