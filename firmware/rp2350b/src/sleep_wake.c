/*
 * sleep_wake.c — Sleep/Wake State Machine for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Implements a simple state machine that transitions the RP2350B
 * between low-power states based on SPI bus activity. When the host
 * (RK3576) stops sending commands, the MCU progressively enters
 * deeper sleep states to conserve battery power.
 *
 * Architecture:
 *   SLEEP_IDLE → SLEEP_LIGHT → SLEEP_DEEP
 *       ↑              ↑             ↑
 *       └──────────────┴─────────────┘
 *                    │
 *              SPI activity detected
 *              (any state → SLEEP_IDLE)
 */

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "sleep_wake.h"
#include "spi_protocol.h"
#include "watchdog.h"

/* ── Module state ──────────────────────────────────────────────────────── */

static struct {
    enum sleep_state state;         /* Current sleep state */
    uint32_t last_activity_ms;      /* Timestamp of last SPI RX activity */
    uint32_t last_process_ms;       /* Timestamp of last process call */
    uint32_t idle_count;            /* Number of idle cycles in current state */
    volatile bool force_wake_pending;    /* Force wake flag (set from ISR context) */
} sw_state;

/* ── External references ──────────────────────────────────────────────── */

/* SPI0 RX activity indicator from spi_protocol.c or rp2350b_init.c */
extern volatile uint32_t spi_rx_head;
extern volatile uint32_t spi_rx_tail;

/* ── Implementation ──────────────────────────────────────────────────── */

void sleep_wake_init(void) {
    sw_state.state = SLEEP_IDLE;
    sw_state.last_activity_ms = to_ms_since_boot(get_absolute_time());
    sw_state.last_process_ms = sw_state.last_activity_ms;
    sw_state.idle_count = 0;
    sw_state.force_wake_pending = false;
}

/**
 * check_spi_activity — Check if SPI bus has received new data
 *
 * Returns: true if new data arrived since last check
 *
 * Note: The static last_head variable is reset when transitioning
 * to a sleep state, so wake events from pre-sleep activity are
 * properly detected.
 */
static bool check_spi_activity(void) {
    static uint32_t last_head = 0;
    uint32_t current_head = spi_rx_head;

    if (current_head != last_head) {
        last_head = current_head;
        return true;
    }
    return false;
}

/**
 * reset_spi_activity_tracker — Reset the SPI activity tracker
 *
 * Called when entering a sleep state so that any new SPI activity
 * upon waking triggers an immediate wake detection. Without this,
 * pre-sleep SPI head values would mask the wake signal.
 */
static void reset_spi_activity_tracker(void) {
    /* Re-read the current head position so that any new bytes
     * received after this point will trigger activity detection. */
    check_spi_activity();  /* Updates static last_head to current */
}

/**
 * enter_light_sleep — Transition to SLEEP_LIGHT
 *
 * Reduces CPU clock to 48 MHz and gates peripheral clocks.
 * SPI0 remains active for host communication.
 */
static void enter_light_sleep(void) {
    /* Reduce system clock to 48 MHz for power savings.
     * SPI0 continues to operate at this frequency since it's
     * a slave peripheral clocked by the host. */

    /* Reset activity tracker before sleeping so wake events are detected */
    reset_spi_activity_tracker();

    set_sys_clock_48mhz();

    /* Keep SPI0, watchdog, and ADC clocks running.
     * Gate clocks to unused peripherals (PIO1, I2C1 if idle). */
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    48 * MHZ, 48 * MHZ);
}

/**
 * exit_light_sleep — Restore full operation from SLEEP_LIGHT
 *
 * Restores clocks to full speed (150 MHz core, 48 MHz peripheral).
 */
static void exit_light_sleep(void) {
    /* Restore 150 MHz system clock */
    set_sys_clock_khz(150000, true);

    /* Re-initialize peripheral clock at 48 MHz */
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    48 * MHZ, 48 * MHZ);
}

/**
 * enter_deep_sleep — Transition to SLEEP_DEEP
 *
 * Halts Core 1 and reduces system clock further.
 * Only SPI0 and watchdog remain active.
 */
static void enter_deep_sleep(void) {
    /* Already in light sleep (48 MHz); Core 1 may already be
     * paused by the SDR DMA idle detection. Signal Core 1 to
     * finish its current buffer and stop. */

    /* Reset activity tracker before deep sleep */
    reset_spi_activity_tracker();

    multicore_fifo_push_blocking(0xDEAD0000U);

    /* Further reduce clock — keep at 48 MHz but disable more
     * peripheral clocks. SPI0 slave still works at 48 MHz since
     * the host provides the clock. */
}

/**
 * exit_deep_sleep — Restore full operation from SLEEP_DEEP
 *
 * Restores clocks and relaunches Core 1.
 * Also kicks the watchdog since deep sleep may have consumed
 * significant time from the watchdog timeout window.
 */
static void exit_deep_sleep(void) {
    /* Restore clocks (same as exit_light_sleep) */
    exit_light_sleep();

    /* Kick the watchdog immediately after restoring clocks.
     * The watchdog timer may have counted down significantly
     * during the deep sleep period. */
    watchdog_kick();

    /* Relaunch Core 1 — it will reinitialize SDR DMA.
     * The SDR DMA module handles its own re-init on Core 1
     * startup, so we just need to launch the entry point again. */
    extern void core1_entry(void);
    multicore_launch_core1(core1_entry);
}

enum sleep_state sleep_wake_process(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t idle_ms = now - sw_state.last_activity_ms;
    bool has_activity = check_spi_activity();

    /* Check for forced wake (from ISR or host INT_REQ).
     * Use compiler barrier after reading volatile flag to prevent
     * the compiler from caching the read across loop iterations. */
    if (sw_state.force_wake_pending) {
        __asm__ volatile ("" ::: "memory");  /* Compiler barrier */
        sw_state.force_wake_pending = false;

        /* Transition to IDLE from any state */
        if (sw_state.state == SLEEP_DEEP) {
            exit_deep_sleep();
        } else if (sw_state.state == SLEEP_LIGHT) {
            exit_light_sleep();
        }
        sw_state.state = SLEEP_IDLE;
        sw_state.idle_count = 0;
        sw_state.last_activity_ms = now;
        return sw_state.state;
    }

    /* Update activity timestamp if new data arrived */
    if (has_activity) {
        sw_state.last_activity_ms = now;
        sw_state.idle_count = 0;

        /* If we're in a sleep state, wake up */
        if (sw_state.state == SLEEP_DEEP) {
            exit_deep_sleep();
            sw_state.state = SLEEP_IDLE;
        } else if (sw_state.state == SLEEP_LIGHT) {
            exit_light_sleep();
            sw_state.state = SLEEP_IDLE;
        }
        return sw_state.state;
    }

    /* No activity — consider transitioning to lower power states */
    sw_state.idle_count++;

    switch (sw_state.state) {
    case SLEEP_IDLE:
        if (idle_ms >= SLEEP_IDLE_TIMEOUT_MS) {
            enter_light_sleep();
            sw_state.state = SLEEP_LIGHT;
            sw_state.idle_count = 0;
        }
        break;

    case SLEEP_LIGHT:
        if (idle_ms >= SLEEP_LIGHT_TIMEOUT_MS) {
            enter_deep_sleep();
            sw_state.state = SLEEP_DEEP;
            sw_state.idle_count = 0;
        }
        break;

    case SLEEP_DEEP:
        /* Stay in deep sleep until activity detected.
         * Kick the watchdog periodically to prevent a reset
         * during deep sleep — the main loop may not be
         * calling watchdog_kick() fast enough at 48 MHz. */
        if (sw_state.idle_count % 50 == 0)
            watchdog_kick();
        break;

    default:
        sw_state.state = SLEEP_IDLE;
        break;
    }

    sw_state.last_process_ms = now;
    return sw_state.state;
}

enum sleep_state sleep_wake_get_state(void) {
    return sw_state.state;
}

void sleep_wake_force_wake(void) {
    sw_state.force_wake_pending = true;
}

uint32_t sleep_wake_get_idle_ms(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    return now - sw_state.last_activity_ms;
}