/*
 * ghostwisp_boot.h — GhostWisp Core Boot Sequence API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Defines the core boot sequence for the GhostWisp RP2350B primary MCU.
 * GhostWisp is a standalone device (no RK3576 host bridge); USB-C is
 * the companion link to GhostBlade.
 *
 * The boot sequence is designed to be deterministic and fail-safe:
 *
 *   1. System clocks (150 MHz core, 48 MHz peripheral, 133 MHz XIP)
 *   2. FPU enable (Cortex-M33 CP10/CP11)
 *   3. GPIO pin muxing for all peripherals
 *   4. UART1 debug console (115200 8N1)
 *   5. USB CDC enumeration
 *   6. Watchdog configuration (5 s timeout, bark interrupt)
 *   7. Battery/fuel gauge I2C check
 *   8. Power-on self-test (POST) of critical peripherals
 *   9. Boot mode selection (normal vs recovery via strap pins)
 *  10. A/B partition selection and image verification
 *
 * After boot_complete() returns successfully, the system is in the
 * Active power state and ready for the event loop.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           devices/ghostwisp/docs/roadmap.md (Phase 3 — Firmware foundation)
 */

#ifndef GHOSTWISP_BOOT_H
#define GHOSTWISP_BOOT_H

#include <stdint.h>
#include <stdbool.h>

/* ── Boot configuration constants ────────────────────────────────────────── */

/** System clock frequency in Hz (150 MHz) */
#define GHOSTWISP_SYS_CLOCK_HZ       150000000UL

/** Peripheral clock frequency in Hz (48 MHz) */
#define GHOSTWISP_PERI_CLOCK_HZ      48000000UL

/** XIP (execute-in-place) clock frequency in Hz (133 MHz) */
#define GHOSTWISP_XIP_CLOCK_HZ       133000000UL

/** Watchdog timeout in milliseconds */
#define GHOSTWISP_WATCHDOG_TIMEOUT_MS   5000

/** Watchdog bark (early warning) time in milliseconds before reset */
#define GHOSTWISP_WATCHDOG_BARK_MS      1000

/** Boot strap magic values (read from PIN_BOOT_STRAP0/1) */
#define BOOT_STRAP_NORMAL             0x00U  /* Normal boot */
#define BOOT_STRAP_RECOVERY           0x01U  /* Forced recovery (USB update only) */
#define BOOT_STRAP_PARTITION_B        0x02U  /* Boot from B partition */
#define BOOT_STRAP_FACTORY_RESET      0x03U  /* Factory reset requested */

/** Maximum boot phases (for reporting) */
#define BOOT_PHASE_COUNT              10

/** Boot result codes */
typedef enum {
    BOOT_OK = 0,                    /**< Boot completed successfully */
    BOOT_ERR_CLOCK = -1,            /**< Clock initialization failed */
    BOOT_ERR_FPU = -2,              /**< FPU enable failed */
    BOOT_ERR_GPIO = -3,             /**< GPIO mux configuration failed */
    BOOT_ERR_UART = -4,             /**< UART console init failed */
    BOOT_ERR_USB = -5,              /**< USB CDC init failed */
    BOOT_ERR_WATCHDOG = -6,         /**< Watchdog configuration failed */
    BOOT_ERR_BATTERY = -7,          /**< Battery/fuel gauge check failed */
    BOOT_ERR_POST = -8,             /**< Power-on self-test failed */
    BOOT_ERR_PARTITION = -9,        /**< A/B partition selection failed */
    BOOT_ERR_SIGN_VERIFY = -10,     /**< Firmware signature verification failed */
    BOOT_ERR_TIMEOUT = -11,         /**< Boot phase timeout */
    BOOT_ERR_HARDWARE = -12,        /**< Unrecoverable hardware fault */
} boot_result_t;

/** Boot phase identifiers (for progress reporting and diagnostics) */
typedef enum {
    BOOT_PHASE_RESET = 0,           /**< Initial reset / reason capture */
    BOOT_PHASE_CLOCKS = 1,          /**< System clock configuration */
    BOOT_PHASE_FPU = 2,             /**< FPU enable */
    BOOT_PHASE_GPIO = 3,            /**< GPIO pin muxing */
    BOOT_PHASE_UART = 4,            /**< Debug UART console */
    BOOT_PHASE_USB = 5,             /**< USB CDC enumeration */
    BOOT_PHASE_WATCHDOG = 6,        /**< Watchdog configuration */
    BOOT_PHASE_BATTERY = 7,         /**< Battery/fuel gauge check */
    BOOT_PHASE_POST = 8,            /**< Power-on self-test */
    BOOT_PHASE_PARTITION = 9,       /**< A/B partition + signature verify */
} boot_phase_t;

/** Reset reason flags (captured at boot start) */
typedef enum {
    RESET_REASON_POWERON   = 0x00,  /**< Power-on reset */
    RESET_REASON_WATCHDOG  = 0x01,  /**< Watchdog timeout reset */
    RESET_REASON_BROWNOUT  = 0x02,  /**< Brownout/undervoltage reset */
    RESET_REASON_FORCE     = 0x04,  /**< Forced reset (software) */
    RESET_REASON_DEEP_SLEEP = 0x08, /**< Wake from deep sleep */
    RESET_REASON_UNKNOWN   = 0xFF,  /**< Unknown reset cause */
} reset_reason_t;

/** Boot state — tracks the progress and result of the boot sequence */
typedef struct {
    boot_phase_t current_phase;     /**< Current or last-completed phase */
    boot_result_t result;           /**< Overall result (BOOT_OK if running) */
    reset_reason_t reset_reason;    /**< Why we booted (from watchdog reason) */
    uint8_t boot_strap;             /**< Boot strap value (normal/recovery/etc) */
    bool partition_b;               /**< True if booting from B partition */
    bool signature_verified;        /**< True if firmware signature passed */
    bool post_passed;               /**< True if POST completed */
    uint32_t boot_time_ms;          /**< Total boot time in milliseconds */
    uint32_t phase_times_ms[BOOT_PHASE_COUNT]; /**< Per-phase elapsed time */
    uint32_t brownout_count;        /**< Cumulative brownout count (from scratch) */
} boot_state_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * boot_init — Initialize the boot state and capture reset reason
 *
 * Must be called first, before any other boot function. Reads the
 * watchdog reason register to determine why the system reset, and
 * samples the boot strap pins to determine the boot mode.
 *
 * Returns a pointer to the global boot state (never NULL).
 */
const boot_state_t *boot_init(void);

/**
 * boot_get_state — Get the current boot state (const access)
 *
 * Returns a pointer to the global boot state. Safe to call at any time.
 */
const boot_state_t *boot_get_state(void);

/**
 * boot_get_state_mut — Get a mutable pointer to boot state
 *
 * For test access and internal modules that need to update state.
 */
boot_state_t *boot_get_state_mut(void);

/**
 * boot_get_reset_reason — Get the reason for the last reset
 *
 * Returns one of the RESET_REASON_* values.
 */
reset_reason_t boot_get_reset_reason(void);

/**
 * boot_get_boot_strap — Get the boot strap value
 *
 * Returns BOOT_STRAP_NORMAL, BOOT_STRAP_RECOVERY, BOOT_STRAP_PARTITION_B,
 * or BOOT_STRAP_FACTORY_RESET.
 */
uint8_t boot_get_boot_strap(void);

/**
 * boot_phase_clocks — Configure system clocks (Phase 1)
 *
 * Sets up 150 MHz core, 48 MHz peripheral, 133 MHz XIP clocks.
 *
 * Returns BOOT_OK on success, BOOT_ERR_CLOCK on failure.
 */
boot_result_t boot_phase_clocks(void);

/**
 * boot_phase_fpu — Enable ARM Cortex-M33 FPU (Phase 2)
 *
 * Enables CP10 and CP11 coprocessor access for floating-point.
 *
 * Returns BOOT_OK on success, BOOT_ERR_FPU on failure.
 */
boot_result_t boot_phase_fpu(void);

/**
 * boot_phase_gpio — Configure GPIO pin muxing (Phase 3)
 *
 * Sets up all peripheral pin functions (SPI, I2C, UART, ADC, USB,
 * display, buttons, IR, feedback, power management, boot straps).
 *
 * Returns BOOT_OK on success, BOOT_ERR_GPIO on failure.
 */
boot_result_t boot_phase_gpio(void);

/**
 * boot_phase_uart — Initialize debug UART console (Phase 4)
 *
 * Configures UART1 at 115200 8N1 on the debug/expansion pins.
 *
 * Returns BOOT_OK on success, BOOT_ERR_UART on failure.
 */
boot_result_t boot_phase_uart(void);

/**
 * boot_phase_usb — Initialize USB CDC (Phase 5)
 *
 * Configures USB CDC for companion link and debug output.
 *
 * Returns BOOT_OK on success, BOOT_ERR_USB on failure.
 */
boot_result_t boot_phase_usb(void);

/**
 * boot_phase_watchdog — Configure hardware watchdog (Phase 6)
 *
 * Sets up a 5-second watchdog with bark interrupt support.
 *
 * Returns BOOT_OK on success, BOOT_ERR_WATCHDOG on failure.
 */
boot_result_t boot_phase_watchdog(void);

/**
 * boot_phase_battery — Check battery / fuel gauge (Phase 7)
 *
 * Reads the fuel gauge over I2C to verify battery is present and
 * above the minimum boot threshold. In recovery mode, this phase
 * is skipped (USB power is assumed).
 *
 * Returns BOOT_OK on success, BOOT_ERR_BATTERY on failure.
 */
boot_result_t boot_phase_battery(void);

/**
 * boot_phase_post — Run power-on self-test (Phase 8)
 *
 * Performs a quick self-test of critical peripherals: checks SPI
 * bus responsiveness (CC1101, ST25R3916), display controller ID,
 * and QSPI flash JEDEC ID. Non-critical failures are logged but
 * do not block boot.
 *
 * Returns BOOT_OK on success, BOOT_ERR_POST if a critical peripheral
 * is non-responsive.
 */
boot_result_t boot_phase_post(void);

/**
 * boot_phase_partition — Select A/B partition and verify (Phase 9)
 *
 * Reads the boot strap to determine which partition to boot from.
 * In normal boot, verifies the firmware signature against the
 * embedded public key. In recovery mode, skips signature check
 * and enters recovery state.
 *
 * Returns BOOT_OK on success, BOOT_ERR_PARTITION or BOOT_ERR_SIGN_VERIFY
 * on failure.
 */
boot_result_t boot_phase_partition(void);

/**
 * boot_complete — Run the complete boot sequence
 *
 * Executes all boot phases in order. If any phase fails, the boot
 * sequence stops and the result is recorded in the boot state.
 * On critical failure, triggers a watchdog reboot after logging
 * the error.
 *
 * Returns BOOT_OK if all phases completed, or the error code of
 * the first failing phase.
 */
boot_result_t boot_complete(void);

/**
 * boot_kick_watchdog — Reset the watchdog timer
 *
 * Must be called periodically from the main event loop to prevent
 * a watchdog reset. Wraps the low-level watchdog feed.
 */
void boot_kick_watchdog(void);

/**
 * boot_enter_recovery — Enter recovery mode
 *
 * Disables all radios and active peripherals. Only USB update
 * and diagnostics are available. Called when boot phase fails
 * or when the recovery boot strap is detected.
 */
void boot_enter_recovery(void);

/**
 * boot_phase_name — Get a human-readable name for a boot phase
 *
 * @phase: Boot phase identifier
 * Returns: Static string with the phase name (never NULL)
 */
const char *boot_phase_name(boot_phase_t phase);

/**
 * boot_result_name — Get a human-readable name for a boot result
 *
 * @result: Boot result code
 * Returns: Static string with the result name (never NULL)
 */
const char *boot_result_name(boot_result_t result);

/**
 * boot_reset_reason_name — Get a human-readable name for a reset reason
 *
 * @reason: Reset reason code
 * Returns: Static string with the reason name (never NULL)
 */
const char *boot_reset_reason_name(reset_reason_t reason);

/**
 * boot_test_reset_wd_enabled — Reset the watchdog enabled state for tests
 *
 * Clears the stub g_wd_enabled flag so each test starts clean.
 * Only available when BOOT_HOST_TEST is defined.
 */
void boot_test_reset_wd_enabled(void);

#endif /* GHOSTWISP_BOOT_H */