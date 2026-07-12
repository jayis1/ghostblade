/*
 * peripheral_power.c — Peripheral Power Sequencing for GhostBlade
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements GPIO-controlled power rail sequencing for the GhostBlade board.
 * Manages the power-on/off sequence for SDR, NFC, sub-GHz, and Wi-Fi rails.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "peripheral_power.h"
#include "board_pins.h"

/* GPIO pins for power rail enable signals.
 * These are active-high unless noted. The RP2350B GPIOs connect
 * to load-switch enable inputs on the board. */

/* Power rail GPIO assignments (board-specific) */
#define PWR_GPIO_SDR_1V8       6    /* LMS7002M 1.8V core enable */
#define PWR_GPIO_SDR_1V1       7    /* LMS7002M 1.1V PLL enable */
#define PWR_GPIO_SDR_3V3       9    /* LMS7002M 3.3V I/O enable */
#define PWR_GPIO_NFC           11   /* ST25R3916 5V enable (via level shifter) */
#define PWR_GPIO_SUBGHZ        22   /* CC1101 3.3V enable */
#define PWR_GPIO_SDIO           25   /* MT7922 SDIO 3.3V enable */

/* RP2350B GPIO base address for direct register access */
#define RP2350B_GPIO_BASE       0x400D0000UL
#define REG32(addr)             (*(volatile uint32_t *)(addr))

/* ========================================================================
 * Module State
 * ======================================================================== */

static struct {
    enum power_rail_state states[POWER_RAIL_COUNT];
    power_event_cb event_callback;
    void *event_context;
} power_mod;

/* Power rail configuration table (matches header enum order) */
static const struct power_rail_config rail_configs[POWER_RAIL_COUNT] = {
    /* POWER_RAIL_SDR_1V8 */
    { PWR_GPIO_SDR_1V8, true, POWER_DELAY_SDR_1V8_MS,
      POWER_LIMIT_SDR_1V8_MA, "SDR 1V8" },
    /* POWER_RAIL_SDR_1V1 */
    { PWR_GPIO_SDR_1V1, true, POWER_DELAY_SDR_1V1_MS,
      POWER_LIMIT_SDR_1V1_MA, "SDR 1V1" },
    /* POWER_RAIL_SDR_3V3 */
    { PWR_GPIO_SDR_3V3, true, POWER_DELAY_SDR_3V3_MS,
      POWER_LIMIT_SDR_3V3_MA, "SDR 3V3" },
    /* POWER_RAIL_NFC */
    { PWR_GPIO_NFC, true, POWER_DELAY_NFC_MS,
      POWER_LIMIT_NFC_MA, "NFC 5V" },
    /* POWER_RAIL_SUBGHZ */
    { PWR_GPIO_SUBGHZ, true, POWER_DELAY_SUBGHZ_MS,
      POWER_LIMIT_SUBGHZ_MA, "Sub-GHz 3V3" },
    /* POWER_RAIL_SDIO */
    { PWR_GPIO_SDIO, true, POWER_DELAY_SDIO_MS,
      POWER_LIMIT_SDIO_MA, "Wi-Fi SDIO 3V3" },
};

/* ========================================================================
 * Simple Delay (busy-loop)
 * ======================================================================== */

static void delay_ms(uint32_t ms) {
    /* Approximate delay: at 150 MHz, ~150000 NOPs per ms.
     * This is a rough busy-wait; for production code, use a
     * hardware timer or sleep_ms() from the Pico SDK. */
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 15000; j++) {
            __asm__ volatile("nop");
        }
    }
}

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * set_rail_gpio — Set the GPIO for a power rail to the specified state
 *
 * @rail:   Power rail ID
 * @on:     true = enable the rail, false = disable
 */
static void set_rail_gpio(enum power_rail_id rail, bool on) {
    const struct power_rail_config *cfg = &rail_configs[rail];
    bool level;

    if (cfg->active_high)
        level = on;
    else
        level = !on;

    /* Use rp2350b_gpio_set equivalent for direct register access */
    volatile uint32_t *out_set = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x0104);
    volatile uint32_t *out_clr = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x0108);

    if (level)
        *out_set = (1UL << cfg->gpio_pin);
    else
        *out_clr = (1UL << cfg->gpio_pin);

    __asm__ volatile("dmb" ::: "memory");
}

/**
 * notify_event — Call the event callback if registered
 */
static void notify_event(enum power_rail_id rail, enum power_rail_state state) {
    if (power_mod.event_callback) {
        power_mod.event_callback(rail, state, power_mod.event_context);
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

int peripheral_power_init(void) {
    /* Initialize all rails to OFF state and configure GPIOs as outputs */
    memset(&power_mod, 0, sizeof(power_mod));

    for (int i = 0; i < POWER_RAIL_COUNT; i++) {
        power_mod.states[i] = POWER_RAIL_OFF;
        set_rail_gpio((enum power_rail_id)i, false);

        /* Configure GPIO as output using pad control */
        volatile uint32_t *pad_ctrl = (volatile uint32_t *)(0x400C0000UL + 0x04 + rail_configs[i].gpio_pin * 4);
        *pad_ctrl &= ~(1UL << 0);   /* Enable output driver */
        *pad_ctrl &= ~(1UL << 3);   /* Disable pull-down */
        *pad_ctrl |= (1UL << 2);    /* Enable pull-up (for load switches) */

        /* Set GPIO function to SIO (software-controlled) */
        volatile uint32_t *ctrl = (volatile uint32_t *)(0x400D0000UL + 0x04 + rail_configs[i].gpio_pin * 8);
        *ctrl = 5;  /* GPIO_FUNC_SIO */
    }

    return 0;
}

int peripheral_power_on(enum power_rail_id rail) {
    if (rail >= POWER_RAIL_COUNT)
        return -1;

    if (power_mod.states[rail] == POWER_RAIL_STABLE)
        return -1;  /* Already on */

    /* Enable the rail */
    set_rail_gpio(rail, true);
    power_mod.states[rail] = POWER_RAIL_RAMPING;
    notify_event(rail, POWER_RAIL_RAMPING);

    /* Wait for voltage to stabilize */
    delay_ms(rail_configs[rail].ramp_ms);

    power_mod.states[rail] = POWER_RAIL_STABLE;
    notify_event(rail, POWER_RAIL_STABLE);

    return 0;
}

int peripheral_power_off(enum power_rail_id rail) {
    if (rail >= POWER_RAIL_COUNT)
        return -1;

    if (power_mod.states[rail] == POWER_RAIL_OFF)
        return -1;  /* Already off */

    set_rail_gpio(rail, false);
    power_mod.states[rail] = POWER_RAIL_OFF;
    notify_event(rail, POWER_RAIL_OFF);

    return 0;
}

int peripheral_power_on_sequence(void) {
    int ret;

    /* Power-on sequence matching power-tree.md:
     *   1. SDR 1V8  → wait 5ms
     *   2. SDR 1V1  → wait 5ms
     *   3. SDR 3V3  → wait 5ms
     *   4. NFC      → wait 50ms
     *   5. Sub-GHz  → wait 50ms
     *   6. SDIO     → wait 20ms */

    ret = peripheral_power_on(POWER_RAIL_SDR_1V8);
    if (ret < 0) return ret;

    ret = peripheral_power_on(POWER_RAIL_SDR_1V1);
    if (ret < 0) return ret;

    ret = peripheral_power_on(POWER_RAIL_SDR_3V3);
    if (ret < 0) return ret;

    ret = peripheral_power_on(POWER_RAIL_NFC);
    if (ret < 0) return ret;

    ret = peripheral_power_on(POWER_RAIL_SUBGHZ);
    if (ret < 0) return ret;

    ret = peripheral_power_on(POWER_RAIL_SDIO);
    if (ret < 0) return ret;

    return 0;
}

int peripheral_power_off_sequence(void) {
    /* Power-off in reverse order with minimum delays */
    peripheral_power_off(POWER_RAIL_SDIO);
    delay_ms(5);

    peripheral_power_off(POWER_RAIL_SUBGHZ);
    delay_ms(5);

    peripheral_power_off(POWER_RAIL_NFC);
    delay_ms(5);

    peripheral_power_off(POWER_RAIL_SDR_3V3);
    delay_ms(5);

    peripheral_power_off(POWER_RAIL_SDR_1V1);
    delay_ms(5);

    peripheral_power_off(POWER_RAIL_SDR_1V8);

    return 0;
}

enum power_rail_state peripheral_power_get_state(enum power_rail_id rail) {
    if (rail >= POWER_RAIL_COUNT)
        return POWER_RAIL_FAULT;
    return power_mod.states[rail];
}

void peripheral_power_set_event_callback(power_event_cb cb, void *context) {
    power_mod.event_callback = cb;
    power_mod.event_context = context;
}

bool peripheral_power_all_on(void) {
    for (int i = 0; i < POWER_RAIL_COUNT; i++) {
        if (power_mod.states[i] != POWER_RAIL_STABLE)
            return false;
    }
    return true;
}

bool peripheral_power_all_off(void) {
    for (int i = 0; i < POWER_RAIL_COUNT; i++) {
        if (power_mod.states[i] != POWER_RAIL_OFF)
            return false;
    }
    return true;
}

const struct power_rail_config *peripheral_power_get_config(enum power_rail_id rail) {
    if (rail >= POWER_RAIL_COUNT)
        return NULL;
    return &rail_configs[rail];
}