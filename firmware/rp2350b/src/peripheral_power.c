/*
 * peripheral_power.c — Peripheral Power Sequencing Module
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Implements GPIO-controlled power rail sequencing for the GhostBlade
 * peripherals. Each peripheral (LMS7002M SDR, ST25R3916 NFC, CC1101
 * sub-GHz, MT7922 Wi-Fi) has its own switched power rail controlled
 * by a GPIO from the RP2350B.
 *
 * The sequencing order matches the power-tree.md requirements to ensure
 * correct voltage ordering and prevent latch-up or inrush current issues.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "peripheral_power.h"

/* ── GPIO pin assignments ──────────────────────────────────────────────────── */

/*
 * RP2350B GPIO pin assignments for power rail control.
 * These match the GhostBlade schematic and board_pins.h definitions.
 *
 * All enable pins are active-high (GPIO high = rail on).
 * Each rail has a 10kΩ pull-down to ensure rails stay off during
 * MCU boot before GPIOs are configured.
 */
#define GPIO_SDR_1V8_EN    18   /* GPIO18: LMS7002M VCC_1V8_SDR enable */
#define GPIO_SDR_1V1_EN    19   /* GPIO19: LMS7002M VCC_1V1_SDR enable */
#define GPIO_SDR_3V3_EN    20   /* GPIO20: LMS7002M VCC_3V3_RF enable */
#define GPIO_NFC_EN        22   /* GPIO22: ST25R3916 VCC_NFC enable */
#define GPIO_SUBGHZ_EN     23   /* GPIO23: CC1101 VCC_SUBGHZ enable */
#define GPIO_SDIO_EN       24   /* GPIO24: MT7922 VCC_SDIO enable */

/* ── Rail configuration table ─────────────────────────────────────────────── */

/*
 * Static configuration for each power rail. The order in this array
 * matches the power-on sequence (index 0 is powered on first).
 */
static const struct power_rail_config rail_configs[POWER_RAIL_COUNT] = {
    [POWER_RAIL_SDR_1V8] = {
        .gpio_pin    = GPIO_SDR_1V8_EN,
        .active_high = true,
        .ramp_ms     = POWER_DELAY_SDR_1V8_MS,
        .current_ma  = POWER_LIMIT_SDR_1V8_MA,
        .name        = "SDR_1V8",
    },
    [POWER_RAIL_SDR_1V1] = {
        .gpio_pin    = GPIO_SDR_1V1_EN,
        .active_high = true,
        .ramp_ms     = POWER_DELAY_SDR_1V1_MS,
        .current_ma  = POWER_LIMIT_SDR_1V1_MA,
        .name        = "SDR_1V1",
    },
    [POWER_RAIL_SDR_3V3] = {
        .gpio_pin    = GPIO_SDR_3V3_EN,
        .active_high = true,
        .ramp_ms     = POWER_DELAY_SDR_3V3_MS,
        .current_ma  = POWER_LIMIT_SDR_3V3_MA,
        .name        = "SDR_3V3",
    },
    [POWER_RAIL_NFC] = {
        .gpio_pin    = GPIO_NFC_EN,
        .active_high = true,
        .ramp_ms     = POWER_DELAY_NFC_MS,
        .current_ma  = POWER_LIMIT_NFC_MA,
        .name        = "NFC",
    },
    [POWER_RAIL_SUBGHZ] = {
        .gpio_pin    = GPIO_SUBGHZ_EN,
        .active_high = true,
        .ramp_ms     = POWER_DELAY_SUBGHZ_MS,
        .current_ma  = POWER_LIMIT_SUBGHZ_MA,
        .name        = "SUBGHZ",
    },
    [POWER_RAIL_SDIO] = {
        .gpio_pin    = GPIO_SDIO_EN,
        .active_high = true,
        .ramp_ms     = POWER_DELAY_SDIO_MS,
        .current_ma  = POWER_LIMIT_SDIO_MA,
        .name        = "SDIO",
    },
};

/* ── Module state ─────────────────────────────────────────────────────────── */

static struct {
    enum power_rail_state states[POWER_RAIL_COUNT];
    power_event_cb        event_callback;
    void                 *event_context;
    bool                  initialized;
} pp_state;

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/**
 * set_rail_gpio — Set the GPIO state for a power rail
 *
 * @rail: Rail ID
 * @on:   True to enable, false to disable
 */
static void set_rail_gpio(enum power_rail_id rail, bool on)
{
    const struct power_rail_config *cfg = &rail_configs[rail];
    bool level;

    if (cfg->active_high) {
        level = on;
    } else {
        level = !on;
    }

    gpio_put(cfg->gpio_pin, level);
}

/**
 * notify_event — Invoke the event callback if registered
 *
 * @rail:  Rail that changed state
 * @state: New state
 */
static void notify_event(enum power_rail_id rail, enum power_rail_state state)
{
    if (pp_state.event_callback) {
        pp_state.event_callback(rail, state, pp_state.event_context);
    }
}

/* ── Public API implementation ─────────────────────────────────────────────── */

int peripheral_power_init(void)
{
    int i;

    /* Configure all power rail GPIOs as outputs, initially OFF */
    for (i = 0; i < POWER_RAIL_COUNT; i++) {
        const struct power_rail_config *cfg = &rail_configs[i];

        /* Set GPIO to OFF state before enabling output to avoid glitches */
        gpio_init(cfg->gpio_pin);
        set_rail_gpio((enum power_rail_id)i, false);
        gpio_set_dir(cfg->gpio_pin, GPIO_OUT);
        gpio_set_pull_mode(cfg->gpio_pin, GPIO_PULL_DOWN);

        /* Initialize state to OFF */
        pp_state.states[i] = POWER_RAIL_OFF;
    }

    pp_state.event_callback = NULL;
    pp_state.event_context = NULL;
    pp_state.initialized = true;

    return 0;
}

int peripheral_power_on(enum power_rail_id rail)
{
    if (rail < 0 || rail >= POWER_RAIL_COUNT) {
        return -1;
    }

    if (!pp_state.initialized) {
        return -1;
    }

    /* Already on? Idempotent: return success if already stable */
    if (pp_state.states[rail] == POWER_RAIL_STABLE) {
        return 0;
    }

    /* Rail is currently ramping — treat as busy/error */
    if (pp_state.states[rail] == POWER_RAIL_RAMPING) {
        return -1;
    }

    /* Drive the GPIO to enable the rail */
    set_rail_gpio(rail, true);
    pp_state.states[rail] = POWER_RAIL_RAMPING;
    notify_event(rail, POWER_RAIL_RAMPING);

    /* Wait for the rail to stabilize */
    sleep_ms(rail_configs[rail].ramp_ms);

    pp_state.states[rail] = POWER_RAIL_STABLE;
    notify_event(rail, POWER_RAIL_STABLE);

    return 0;
}

int peripheral_power_off(enum power_rail_id rail)
{
    if (rail < 0 || rail >= POWER_RAIL_COUNT) {
        return -1;
    }

    if (!pp_state.initialized) {
        return -1;
    }

    /* Already off? Idempotent: return success */
    if (pp_state.states[rail] == POWER_RAIL_OFF) {
        return 0;
    }

    /* Drive the GPIO to disable the rail */
    set_rail_gpio(rail, false);
    pp_state.states[rail] = POWER_RAIL_OFF;
    notify_event(rail, POWER_RAIL_OFF);

    return 0;
}

int peripheral_power_on_sequence(void)
{
    int result;

    if (!pp_state.initialized) {
        return -1;
    }

    /* Power-on sequence matches the power-tree.md requirements.
     *
     * Each call to peripheral_power_on() enables the GPIO and waits
     * for the rail ramp time. The total sequence takes approximately:
     *   5 + 5 + 5 + 50 + 50 + 20 = 135ms
     *
     * Assumption: VCC_3V3 (from PMIC) is already stable before this
     * function is called. The RK3576 drives VCC_3V3 enable, and
     * the RP2350B should only be called after VCC_3V3_RP is stable.
     */

    /* Step 1: SDR 1V8 (LMS7002M core) — depends on VCC_3V3 */
    result = peripheral_power_on(POWER_RAIL_SDR_1V8);
    if (result != 0) {
        goto fail;
    }

    /* Step 2: SDR 1V1 (LMS7002M PLL) — depends on SDR 1V8 */
    result = peripheral_power_on(POWER_RAIL_SDR_1V1);
    if (result != 0) {
        peripheral_power_off(POWER_RAIL_SDR_1V8);
        goto fail;
    }

    /* Step 3: SDR 3V3 (LMS7002M PA/LNA) — depends on SDR 1V1 */
    result = peripheral_power_on(POWER_RAIL_SDR_3V3);
    if (result != 0) {
        peripheral_power_off(POWER_RAIL_SDR_1V1);
        peripheral_power_off(POWER_RAIL_SDR_1V8);
        goto fail;
    }

    /* Step 4: NFC (ST25R3916) — depends on VCC_3V3 */
    result = peripheral_power_on(POWER_RAIL_NFC);
    if (result != 0) {
        peripheral_power_off(POWER_RAIL_SDR_3V3);
        peripheral_power_off(POWER_RAIL_SDR_1V1);
        peripheral_power_off(POWER_RAIL_SDR_1V8);
        goto fail;
    }

    /* Step 5: Sub-GHz (CC1101) — depends on VCC_3V3 */
    result = peripheral_power_on(POWER_RAIL_SUBGHZ);
    if (result != 0) {
        peripheral_power_off(POWER_RAIL_NFC);
        peripheral_power_off(POWER_RAIL_SDR_3V3);
        peripheral_power_off(POWER_RAIL_SDR_1V1);
        peripheral_power_off(POWER_RAIL_SDR_1V8);
        goto fail;
    }

    /* Step 6: SDIO (MT7922 Wi-Fi) — depends on VCC_3V3 */
    result = peripheral_power_on(POWER_RAIL_SDIO);
    if (result != 0) {
        peripheral_power_off(POWER_RAIL_SUBGHZ);
        peripheral_power_off(POWER_RAIL_NFC);
        peripheral_power_off(POWER_RAIL_SDR_3V3);
        peripheral_power_off(POWER_RAIL_SDR_1V1);
        peripheral_power_off(POWER_RAIL_SDR_1V8);
        goto fail;
    }

    return 0;

fail:
    /* A rail failed to power on. All previously enabled rails have been
     * shut down. Report the error. */
    return result;
}

int peripheral_power_off_sequence(void)
{
    if (!pp_state.initialized) {
        return -1;
    }

    /* Power-off sequence is the reverse of the power-on sequence.
     * We add a small delay between each rail to allow the load
     * to discharge, preventing voltage feeding back through
     * parasitic paths. */

    /* Step 1: SDIO off (Wi-Fi module) */
    peripheral_power_off(POWER_RAIL_SDIO);
    sleep_ms(5);

    /* Step 2: Sub-GHz off (CC1101) */
    peripheral_power_off(POWER_RAIL_SUBGHZ);
    sleep_ms(5);

    /* Step 3: NFC off (ST25R3916) */
    peripheral_power_off(POWER_RAIL_NFC);
    sleep_ms(5);

    /* Step 4: SDR 3V3 off (LMS7002M PA/LNA) */
    peripheral_power_off(POWER_RAIL_SDR_3V3);
    sleep_ms(5);

    /* Step 5: SDR 1V1 off (LMS7002M PLL) */
    peripheral_power_off(POWER_RAIL_SDR_1V1);
    sleep_ms(5);

    /* Step 6: SDR 1V8 off (LMS7002M core) */
    peripheral_power_off(POWER_RAIL_SDR_1V8);

    return 0;
}

enum power_rail_state peripheral_power_get_state(enum power_rail_id rail)
{
    if (rail < 0 || rail >= POWER_RAIL_COUNT) {
        return POWER_RAIL_FAULT;
    }

    return pp_state.states[rail];
}

void peripheral_power_set_event_callback(power_event_cb cb, void *context)
{
    pp_state.event_callback = cb;
    pp_state.event_context = context;
}

bool peripheral_power_all_on(void)
{
    int i;

    for (i = 0; i < POWER_RAIL_COUNT; i++) {
        if (pp_state.states[i] != POWER_RAIL_STABLE) {
            return false;
        }
    }

    return true;
}

bool peripheral_power_all_off(void)
{
    int i;

    for (i = 0; i < POWER_RAIL_COUNT; i++) {
        if (pp_state.states[i] != POWER_RAIL_OFF) {
            return false;
        }
    }

    return true;
}

const struct power_rail_config *peripheral_power_get_config(enum power_rail_id rail)
{
    if (rail < 0 || rail >= POWER_RAIL_COUNT) {
        return NULL;
    }

    return &rail_configs[rail];
}