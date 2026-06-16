/*
 * watchdog.h — Hardware Watchdog Management API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Manages the RP2350B hardware watchdog timer for system reliability.
 * The watchdog is configured with a 5-second timeout. The main loop
 * kicks the watchdog each iteration. If the main loop stalls, the
 * watchdog bark interrupt fires first (1 tick before reset), giving
 * the system a chance to deassert INT_REQ and stop peripherals.
 *
 * Brownout recovery: If the system resets due to brownout, the
 * watchdog scratch register retains a flag that can be checked on
 * the next boot via watchdog_check_brownout().
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

/* ── Watchdog configuration ─────────────────────────────────────────────── */

/** Watchdog timeout in milliseconds */
#define WATCHDOG_TIMEOUT_MS     5000

/** Watchdog bark time in milliseconds (fires this long before reset) */
#define WATCHDOG_BARK_TIME_MS   1000

/** Magic value stored in scratch register on brownout detection */
#define WATCHDOG_BROWNOUT_MAGIC 0xB047B00TL

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * watchdog_init — Initialize and enable the hardware watchdog timer
 *
 * Configures the RP2350B watchdog with a 5-second timeout.
 * If the system is booting after a watchdog reset (rather than
 * power-on), this function detects it and returns true.
 *
 * Returns: true if this boot is due to a watchdog reset,
 *          false for normal power-on reset
 */
bool watchdog_init(void);

/**
 * watchdog_kick — Reset the watchdog timer (kick the dog)
 *
 * Must be called from the main loop at least every WATCHDOG_TIMEOUT_MS
 * milliseconds to prevent a system reset. Called automatically by
 * the main loop in main.c.
 */
void watchdog_kick(void);

/**
 * watchdog_enable_bark — Enable the watchdog bark interrupt
 *
 * The bark interrupt fires WATCHDOG_BARK_TIME_MS before the watchdog
 * expires. The handler (configured in main.c) deasserts INT_REQ,
 * stops peripherals, and attempts one last watchdog kick.
 */
void watchdog_enable_bark(void);

/**
 * watchdog_mark_brownout — Mark brownout condition in scratch register
 *
 * Writes a magic value to the watchdog scratch register so that
 * after the inevitable reset, watchdog_check_brownout() can detect
 * that the reset was caused by a brownout condition.
 */
void watchdog_mark_brownout(void);

/**
 * watchdog_check_brownout — Check if the last reset was due to brownout
 *
 * Reads the watchdog scratch register for the brownout magic value.
 * If found, clears the register and returns true. This should be
 * called early in main() to log the brownout recovery.
 *
 * Returns: true if brownout was detected, false otherwise
 */
bool watchdog_check_brownout(void);

#endif /* WATCHDOG_H */