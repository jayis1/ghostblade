/*
 * watchdog.c — Watchdog Timer for RP2350B Coprocessor
 *
 * Copyright (C) 2026 Apex One Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements a watchdog timer for the RP2350B coprocessor
 * on the Apex One board. The watchdog ensures the MCU recovers from
 * software hangs by resetting the processor if the main loop fails
 * to service the watchdog within the timeout period.
 *
 * The RP2350B has a hardware watchdog timer (TIMER1-based or
 * the PSM watchdog) that can be configured with a programmable
 * timeout. The main loop must call watchdog_feed() periodically
 * (at least once per timeout interval) to prevent a reset.
 *
 * Timeout: 500 ms (plenty of headroom for 150 MHz MCU)
 * Feed interval: Every main loop iteration (expected < 100 ms)
 *
 * Reference: RP2350B Datasheet, Section 4.7 (Watchdog)
 */

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * RP2350B Watchdog Register Map
 * ======================================================================== */

#define RP2350B_WATCHDOG_BASE    0x40058000UL

#define WD_CTRL                  0x00   /* Watchdog Control */
#define WD_LOAD                  0x04   /* Watchdog Load */
#define WD_REASON                0x08   /* Watchdog Reason */
#define WD_SCRATCH0              0x0C   /* Watchdog Scratch 0 */
#define WD_SCRATCH1              0x10   /* Watchdog Scratch 1 */
#define WD_SCRATCH2              0x14   /* Watchdog Scratch 2 */
#define WD_SCRATCH3              0x18   /* Watchdog Scratch 3 */
#define WD_SCRATCH4              0x1C   /* Watchdog Scratch 4 */
#define WD_SCRATCH5              0x20   /* Watchdog Scratch 5 */
#define WD_SCRATCH6              0x24   /* Watchdog Scratch 6 */
#define WD_SCRATCH7              0x28   /* Watchdog Scratch 7 */

/* WD_CTRL bits */
#define WD_CTRL_ENABLE           (1 << 0)    /* Watchdog enable */
#define WD_CTRL_PAUSE_DBG0      (1 << 1)    /* Pause when core 0 in debug */
#define WD_CTRL_PAUSE_DBG1      (1 << 2)    /* Pause when core 1 in debug */
#define WD_CTRL_PAUSE_JTAG      (1 << 3)    /* Pause when JTAG active */

/* WD_REASON bits */
#define WD_REASON_FORCE         (1 << 0)    /* Forced reset */
#define WD_REASON_TIMER         (1 << 1)    /* Timer expired (watchdog bite) */

/* ========================================================================
 * Helper Macros
 * ======================================================================== */

#define REG32(addr)           (*(volatile uint32_t *)(addr))

/* ========================================================================
 * Watchdog Configuration
 * ======================================================================== */

/*
 * Watchdog timeout calculation:
 *
 * The RP2350B watchdog counts down from the LOAD value at a rate
 * of 1 tick per μs (1 MHz clock derived from clk_ref).
 *
 * For a 500 ms timeout:
 *   LOAD = 500,000 (500 ms × 1,000,000 ticks/sec)
 *
 * The watchdog generates a chip reset when the counter reaches 0.
 * Writing to WD_LOAD reloads the counter (feeding the watchdog).
 */

#define WD_TIMEOUT_US          500000UL    /* 500 ms timeout */
#define WD_MAGIC_SCRATCH       0xAPEX1UL  /* Magic value in scratch register */

/* ========================================================================
 * Watchdog Public API
 * ======================================================================== */

/**
 * watchdog_init — Initialize and enable the hardware watchdog timer
 *
 * Configures the watchdog with a 500 ms timeout. The main loop
 * must call watchdog_feed() at least once per timeout interval.
 *
 * If the watchdog was previously triggered (check reason register),
 * this function reports the reset source.
 *
 * Returns: true if this boot is from a watchdog reset
 */
bool watchdog_init(void) {
    volatile uint32_t *wd_ctrl = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_CTRL);
    volatile uint32_t *wd_load = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_LOAD);
    volatile uint32_t *wd_reason = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_REASON);
    volatile uint32_t *wd_scratch0 = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_SCRATCH0);

    bool wd_reset = false;

    /* Check if we're recovering from a watchdog reset */
    if (*wd_reason & WD_REASON_TIMER) {
        wd_reset = true;

        /* Clear the reason register */
        *wd_reason = 0;

        /* Scratch register 0 contains our magic if we set it before reset */
        if (*wd_scratch0 == WD_MAGIC_SCRATCH) {
            /* This was an intentional watchdog-triggered reboot
             * (e.g., after firmware update). Clear the magic. */
            *wd_scratch0 = 0;
        }
    }

    /* Clear forced reset reason if present */
    if (*wd_reason & WD_REASON_FORCE) {
        *wd_reason = 0;
    }

    /* Load initial timeout value */
    *wd_load = WD_TIMEOUT_US;

    /* Enable watchdog, pause during JTAG debug */
    *wd_ctrl = WD_CTRL_ENABLE | WD_CTRL_PAUSE_JTAG | WD_CTRL_PAUSE_DBG0;

    return wd_reset;
}

/**
 * watchdog_feed — Reset the watchdog counter (kick the dog)
 *
 * Must be called at least once per timeout interval (500 ms).
 * Typically called at the start of each main loop iteration.
 *
 * Writing to WD_LOAD reloads the counter to the timeout value.
 */
void watchdog_feed(void) {
    volatile uint32_t *wd_load = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_LOAD);
    *wd_load = WD_TIMEOUT_US;
}

/**
 * watchdog_disable — Disable the watchdog timer
 *
 * WARNING: This should only be called during debug or firmware update.
 * The production firmware should NEVER disable the watchdog.
 *
 * Returns: 0 on success, -1 if watchdog cannot be disabled
 *
 * Note: On some implementations, the watchdog cannot be disabled
 * once enabled (one-shot enable). In that case, this function
 * sets the timeout to maximum and returns -1.
 */
int watchdog_disable(void) {
    volatile uint32_t *wd_ctrl = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_CTRL);

    /* Attempt to disable the watchdog */
    *wd_ctrl = 0;

    /* Verify it was disabled */
    if (*wd_ctrl & WD_CTRL_ENABLE) {
        /* Cannot disable — set maximum timeout instead */
        volatile uint32_t *wd_load = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_LOAD);
        *wd_load = 0xFFFFFFFEUL;  /* Maximum timeout (~4.3 seconds) */
        return -1;
    }

    return 0;
}

/**
 * watchdog_reboot — Force a system reboot via the watchdog
 *
 * Triggers an immediate watchdog reset by loading a minimal timeout
 * and waiting for the watchdog to fire. Optionally writes a magic
 * value to a scratch register to signal the reboot was intentional.
 *
 * @intentional: If true, write magic to scratch0 for post-reboot detection
 */
void watchdog_reboot(bool intentional) {
    volatile uint32_t *wd_load = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_LOAD);
    volatile uint32_t *wd_scratch0 = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_SCRATCH0);
    volatile uint32_t *wd_ctrl = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_CTRL);

    if (intentional) {
        *wd_scratch0 = WD_MAGIC_SCRATCH;
    }

    /* Ensure watchdog is enabled */
    *wd_ctrl |= WD_CTRL_ENABLE;

    /* Load minimum timeout — will reset in ~1 μs */
    *wd_load = 1;

    /* Wait for reset (should never return) */
    while (1)
        __asm__("nop");
}

/**
 * watchdog_get_reason — Check if the last reset was from the watchdog
 *
 * Returns: Reason flags (0 = no watchdog reset, WD_REASON_TIMER = timeout,
 *          WD_REASON_FORCE = forced)
 */
uint32_t watchdog_get_reason(void) {
    volatile uint32_t *wd_reason = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_REASON);
    return *wd_reason;
}

/**
 * watchdog_get_scratch — Read a watchdog scratch register
 *
 * @index: Scratch register index (0-7)
 * Returns: Scratch register value
 */
uint32_t watchdog_get_scratch(uint8_t index) {
    if (index > 7)
        return 0;

    volatile uint32_t *wd_scratch = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_SCRATCH0);
    return wd_scratch[index];
}

/**
 * watchdog_set_scratch — Write a watchdog scratch register
 *
 * @index: Scratch register index (0-7)
 * @val:   Value to write
 *
 * Scratch registers survive watchdog resets and can be used to
 * pass information across reboots (e.g., firmware update status,
 * error codes, or boot count).
 */
void watchdog_set_scratch(uint8_t index, uint32_t val) {
    if (index > 7)
        return;

    volatile uint32_t *wd_scratch = (volatile uint32_t *)(RP2350B_WATCHDOG_BASE + WD_SCRATCH0);
    wd_scratch[index] = val;
}