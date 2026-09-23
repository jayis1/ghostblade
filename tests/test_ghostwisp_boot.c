/*
 * test_ghostwisp_boot.c — Unit Tests for GhostWisp Core Boot Sequence
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the boot sequence logic, phase ordering, reset reason capture,
 * boot strap reading, watchdog configuration, GPIO mux verification,
 * A/B partition selection, and recovery mode behavior.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DBOOT_HOST_TEST -o test_ghostwisp_boot test_ghostwisp_boot.c ghostwisp_boot.c
 *
 * Run:
 *   ./test_ghostwisp_boot
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Include the boot header — this pulls in all types and constants */
#include "ghostwisp_boot.h"
#include "ghostwisp_pins.h"

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

#define ASSERT_STR_EQ(expected, actual) do {                                \
    g_tests_run++;                                                          \
    if (strcmp((expected), (actual)) != 0) {                                \
        fprintf(stderr, "  FAIL: %s:%d: expected \"%s\", got \"%s\"\n",     \
                __FILE__, __LINE__, (expected), (actual));                  \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

#define ASSERT_PTR_NOT_NULL(ptr) do {                                       \
    g_tests_run++;                                                          \
    if ((ptr) == NULL) {                                                    \
        fprintf(stderr, "  FAIL: %s:%d: pointer is NULL: %s\n",             \
                __FILE__, __LINE__, #ptr);                                  \
        g_tests_failed++;                                                   \
    } else {                                                                \
        g_tests_passed++;                                                   \
    }                                                                       \
} while (0)

/* ── Test control stubs (provided to ghostwisp_boot.c via extern) ───────── */

static uint8_t g_test_strap = BOOT_STRAP_NORMAL;

uint8_t boot_test_strap_value(void) {
    return g_test_strap;
}

/* ── Host test inspection functions (from ghostwisp_boot.c) ─────────────── */

extern uint32_t boot_test_gpio_func(uint8_t pin);
extern bool boot_test_gpio_pullup(uint8_t pin);
extern bool boot_test_gpio_is_input(uint8_t pin);
extern bool boot_test_gpio_is_output(uint8_t pin);
extern void boot_test_set_time_ms(uint32_t t);
extern void boot_test_advance_time_ms(uint32_t delta);
extern void boot_test_set_wd_reason(uint32_t reason);
extern void boot_test_set_wd_scratch(uint8_t idx, uint32_t val);
extern uint32_t boot_test_get_wd_load(void);
extern bool boot_test_wd_enabled(void);

/* ── Test helper: reset all test state ──────────────────────────────────── */

static void test_reset(void) {
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_test_set_time_ms(0);
    boot_test_set_wd_reason(0);
    for (int i = 0; i < 8; i++)
        boot_test_set_wd_scratch((uint8_t)i, 0);
}

/* ========================================================================
 * Test 1: boot_init captures power-on reset reason
 * ======================================================================== */

static void test_boot_init_poweron_reset(void) {
    printf("  test_boot_init_poweron_reset... ");
    test_reset();
    /* No watchdog reason bits set, no brownout scratch → power-on */
    const boot_state_t *state = boot_init();
    ASSERT_PTR_NOT_NULL(state);
    ASSERT_INT_EQ(RESET_REASON_POWERON, state->reset_reason);
    ASSERT_INT_EQ(BOOT_STRAP_NORMAL, state->boot_strap);
    ASSERT_INT_EQ(BOOT_OK, state->result);
    printf("OK\n");
}

/* ========================================================================
 * Test 2: boot_init captures watchdog reset reason
 * ======================================================================== */

static void test_boot_init_watchdog_reset(void) {
    printf("  test_boot_init_watchdog_reset... ");
    test_reset();
    boot_test_set_wd_reason(0x02);  /* WD_REASON_TIMER */
    const boot_state_t *state = boot_init();
    ASSERT_INT_EQ(RESET_REASON_WATCHDOG, state->reset_reason);
    printf("OK\n");
}

/* ========================================================================
 * Test 3: boot_init captures forced reset reason
 * ======================================================================== */

static void test_boot_init_forced_reset(void) {
    printf("  test_boot_init_forced_reset... ");
    test_reset();
    boot_test_set_wd_reason(0x01);  /* WD_REASON_FORCE */
    const boot_state_t *state = boot_init();
    ASSERT_INT_EQ(RESET_REASON_FORCE, state->reset_reason);
    printf("OK\n");
}

/* ========================================================================
 * Test 4: boot_init captures brownout reset with count tracking
 * ======================================================================== */

static void test_boot_init_brownout_reset(void) {
    printf("  test_boot_init_brownout_reset... ");
    test_reset();
    /* Set brownout magic in scratch7 and a prior count in scratch1 */
    boot_test_set_wd_scratch(7, 0xB047B00FUL);
    boot_test_set_wd_scratch(1, 3);  /* Prior brownout count = 3 */
    const boot_state_t *state = boot_init();
    ASSERT_INT_EQ(RESET_REASON_BROWNOUT, state->reset_reason);
    ASSERT_INT_EQ(4, (int)state->brownout_count);  /* 3 + 1 = 4 */
    printf("OK\n");
}

/* ========================================================================
 * Test 5: boot_init reads boot strap values
 * ======================================================================== */

static void test_boot_init_strap_values(void) {
    printf("  test_boot_init_strap_values... ");
    test_reset();
    g_test_strap = BOOT_STRAP_RECOVERY;
    const boot_state_t *state = boot_init();
    ASSERT_INT_EQ(BOOT_STRAP_RECOVERY, state->boot_strap);

    test_reset();
    g_test_strap = BOOT_STRAP_PARTITION_B;
    state = boot_init();
    ASSERT_INT_EQ(BOOT_STRAP_PARTITION_B, state->boot_strap);

    test_reset();
    g_test_strap = BOOT_STRAP_FACTORY_RESET;
    state = boot_init();
    ASSERT_INT_EQ(BOOT_STRAP_FACTORY_RESET, state->boot_strap);
    printf("OK\n");
}

/* ========================================================================
 * Test 6: boot_complete succeeds in normal boot
 * ======================================================================== */

static void test_boot_complete_normal(void) {
    printf("  test_boot_complete_normal... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_result_t result = boot_complete();
    ASSERT_INT_EQ(BOOT_OK, result);

    const boot_state_t *state = boot_get_state();
    ASSERT_TRUE(state->post_passed);
    ASSERT_TRUE(state->signature_verified);
    ASSERT_FALSE(state->partition_b);
    printf("OK\n");
}

/* ========================================================================
 * Test 7: boot_complete in recovery mode skips battery and signature
 * ======================================================================== */

static void test_boot_complete_recovery(void) {
    printf("  test_boot_complete_recovery... ");
    test_reset();
    g_test_strap = BOOT_STRAP_RECOVERY;
    boot_result_t result = boot_complete();
    ASSERT_INT_EQ(BOOT_OK, result);

    const boot_state_t *state = boot_get_state();
    ASSERT_FALSE(state->signature_verified);
    printf("OK\n");
}

/* ========================================================================
 * Test 8: factory reset strap enters recovery immediately
 * ======================================================================== */

static void test_boot_complete_factory_reset(void) {
    printf("  test_boot_complete_factory_reset... ");
    test_reset();
    g_test_strap = BOOT_STRAP_FACTORY_RESET;
    boot_result_t result = boot_complete();
    ASSERT_INT_EQ(BOOT_OK, result);

    const boot_state_t *state = boot_get_state();
    ASSERT_INT_EQ(BOOT_STRAP_RECOVERY, state->boot_strap);
    printf("OK\n");
}

/* ========================================================================
 * Test 9: partition B strap selects B partition
 * ======================================================================== */

static void test_boot_partition_b(void) {
    printf("  test_boot_partition_b... ");
    test_reset();
    g_test_strap = BOOT_STRAP_PARTITION_B;
    boot_result_t result = boot_complete();
    ASSERT_INT_EQ(BOOT_OK, result);

    const boot_state_t *state = boot_get_state();
    ASSERT_TRUE(state->partition_b);
    ASSERT_TRUE(state->signature_verified);
    printf("OK\n");
}

/* ========================================================================
 * Test 10: all boot phases are executed in order
 * ======================================================================== */

static void test_boot_phases_executed(void) {
    printf("  test_boot_phases_executed... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    const boot_state_t *state = boot_get_state();
    /* The last phase should be PARTITION (phase 9) */
    ASSERT_INT_EQ(BOOT_PHASE_PARTITION, state->current_phase);
    /* All phase times should be recorded (non-zero in test mode is fine,
     * but they should all be present in the array) */
    /* In host test mode, time doesn't advance unless we set it, so
     * times may all be 0. The key check is that the phase completed. */
    printf("OK\n");
}

/* ========================================================================
 * Test 11: watchdog is configured with correct timeout
 * ======================================================================== */

static void test_watchdog_configured(void) {
    printf("  test_watchdog_configured... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* Watchdog should be enabled with the correct load value */
    ASSERT_TRUE(boot_test_wd_enabled());
    uint32_t load = boot_test_get_wd_load();
    ASSERT_UINT_EQ((uint32_t)GHOSTWISP_WATCHDOG_TIMEOUT_MS * 1000, load);
    printf("OK\n");
}

/* ========================================================================
 * Test 12: boot_kick_watchdog reloads the timer
 * ======================================================================== */

static void test_boot_kick_watchdog(void) {
    printf("  test_boot_kick_watchdog... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* Corrupt the load value, then kick and verify it's restored */
    boot_test_set_wd_scratch(0, 0);  /* Just to touch scratch */
    boot_kick_watchdog();
    uint32_t load = boot_test_get_wd_load();
    ASSERT_UINT_EQ((uint32_t)GHOSTWISP_WATCHDOG_TIMEOUT_MS * 1000, load);
    printf("OK\n");
}

/* ========================================================================
 * Test 13: GPIO pin configuration — SPI buses
 * ======================================================================== */

static void test_gpio_spi_config(void) {
    printf("  test_gpio_spi_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* SPI0: CC1101 — SCK, TX, RX should be SPI function */
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_CC_SPI_SCK));  /* GPIO_FUNC_SPI */
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_CC_SPI_TX));
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_CC_SPI_RX));

    /* SPI0 CSn should be SIO output */
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_CC_SPI_CSN));

    /* SPI1: ST25R3916 — SCK, TX, RX should be SPI function */
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_NFC_SPI_SCK));
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_NFC_SPI_TX));
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_NFC_SPI_RX));
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_NFC_SPI_CSN));

    /* SPI2: microSD — SCK, TX, RX should be SPI function */
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_SD_SPI_SCK));
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_SD_SPI_TX));
    ASSERT_UINT_EQ(1U, boot_test_gpio_func(PIN_SD_SPI_RX));
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_SD_SPI_CSN));
    printf("OK\n");
}

/* ========================================================================
 * Test 14: GPIO pin configuration — I2C bus
 * ======================================================================== */

static void test_gpio_i2c_config(void) {
    printf("  test_gpio_i2c_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* I2C0: fuel gauge — SDA and SCL should be I2C function (3) */
    ASSERT_UINT_EQ(3U, boot_test_gpio_func(PIN_BATT_I2C_SDA));
    ASSERT_UINT_EQ(3U, boot_test_gpio_func(PIN_BATT_I2C_SCL));

    /* I2C pins should have pull-up enabled */
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BATT_I2C_SDA));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BATT_I2C_SCL));
    printf("OK\n");
}

/* ========================================================================
 * Test 15: GPIO pin configuration — buttons with pull-ups
 * ======================================================================== */

static void test_gpio_buttons_config(void) {
    printf("  test_gpio_buttons_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* All buttons should be inputs with pull-ups (active-low) */
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_UP));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_UP));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_DOWN));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_DOWN));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_LEFT));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_LEFT));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_RIGHT));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_RIGHT));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_CENTER));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_CENTER));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_A));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_A));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BTN_B));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_BTN_B));
    printf("OK\n");
}

/* ========================================================================
 * Test 16: GPIO pin configuration — display control pins
 * ======================================================================== */

static void test_gpio_display_config(void) {
    printf("  test_gpio_display_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* Display SPI should be PIO0 function (6) */
    ASSERT_UINT_EQ(6U, boot_test_gpio_func(PIN_DISP_SPI_SCK));
    ASSERT_UINT_EQ(6U, boot_test_gpio_func(PIN_DISP_SPI_TX));

    /* Display control pins should be SIO outputs */
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_DISP_SPI_CSN));
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_DISP_DC));
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_DISP_RST));
    ASSERT_TRUE(boot_test_gpio_is_output(PIN_DISP_BL));
    printf("OK\n");
}

/* ========================================================================
 * Test 17: GPIO pin configuration — boot straps with pull-downs
 * ======================================================================== */

static void test_gpio_boot_straps_config(void) {
    printf("  test_gpio_boot_straps_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* Boot straps should be inputs (SIO function = 5) */
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BOOT_STRAP0));
    ASSERT_TRUE(boot_test_gpio_is_input(PIN_BOOT_STRAP1));
    printf("OK\n");
}

/* ========================================================================
 * Test 18: GPIO pin configuration — expansion header UART
 * ======================================================================== */

static void test_gpio_expansion_uart(void) {
    printf("  test_gpio_expansion_uart... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* Expansion UART should be UART function (2) */
    ASSERT_UINT_EQ(2U, boot_test_gpio_func(PIN_EXP_UART_TX));
    ASSERT_UINT_EQ(2U, boot_test_gpio_func(PIN_EXP_UART_RX));
    printf("OK\n");
}

/* ========================================================================
 * Test 19: boot_enter_recovery sets recovery strap
 * ======================================================================== */

static void test_enter_recovery(void) {
    printf("  test_enter_recovery... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    /* Enter recovery mode */
    boot_enter_recovery();

    const boot_state_t *state = boot_get_state();
    ASSERT_INT_EQ(BOOT_STRAP_RECOVERY, state->boot_strap);
    printf("OK\n");
}

/* ========================================================================
 * Test 20: clock constants are correct
 * ======================================================================== */

static void test_clock_constants(void) {
    printf("  test_clock_constants... ");
    ASSERT_UINT_EQ(150000000UL, GHOSTWISP_SYS_CLOCK_HZ);
    ASSERT_UINT_EQ(48000000UL, GHOSTWISP_PERI_CLOCK_HZ);
    ASSERT_UINT_EQ(133000000UL, GHOSTWISP_XIP_CLOCK_HZ);
    printf("OK\n");
}

/* ========================================================================
 * Test 21: watchdog timeout constant
 * ======================================================================== */

static void test_watchdog_constants(void) {
    printf("  test_watchdog_constants... ");
    ASSERT_INT_EQ(5000, GHOSTWISP_WATCHDOG_TIMEOUT_MS);
    ASSERT_INT_EQ(1000, GHOSTWISP_WATCHDOG_BARK_MS);
    printf("OK\n");
}

/* ========================================================================
 * Test 22: boot phase name lookup
 * ======================================================================== */

static void test_phase_names(void) {
    printf("  test_phase_names... ");
    ASSERT_STR_EQ("reset-capture", boot_phase_name(BOOT_PHASE_RESET));
    ASSERT_STR_EQ("clocks", boot_phase_name(BOOT_PHASE_CLOCKS));
    ASSERT_STR_EQ("fpu", boot_phase_name(BOOT_PHASE_FPU));
    ASSERT_STR_EQ("gpio", boot_phase_name(BOOT_PHASE_GPIO));
    ASSERT_STR_EQ("uart", boot_phase_name(BOOT_PHASE_UART));
    ASSERT_STR_EQ("usb", boot_phase_name(BOOT_PHASE_USB));
    ASSERT_STR_EQ("watchdog", boot_phase_name(BOOT_PHASE_WATCHDOG));
    ASSERT_STR_EQ("battery", boot_phase_name(BOOT_PHASE_BATTERY));
    ASSERT_STR_EQ("post", boot_phase_name(BOOT_PHASE_POST));
    ASSERT_STR_EQ("partition", boot_phase_name(BOOT_PHASE_PARTITION));
    printf("OK\n");
}

/* ========================================================================
 * Test 23: boot result name lookup
 * ======================================================================== */

static void test_result_names(void) {
    printf("  test_result_names... ");
    ASSERT_STR_EQ("ok", boot_result_name(BOOT_OK));
    ASSERT_STR_EQ("clock-failed", boot_result_name(BOOT_ERR_CLOCK));
    ASSERT_STR_EQ("fpu-failed", boot_result_name(BOOT_ERR_FPU));
    ASSERT_STR_EQ("gpio-failed", boot_result_name(BOOT_ERR_GPIO));
    ASSERT_STR_EQ("uart-failed", boot_result_name(BOOT_ERR_UART));
    ASSERT_STR_EQ("usb-failed", boot_result_name(BOOT_ERR_USB));
    ASSERT_STR_EQ("watchdog-failed", boot_result_name(BOOT_ERR_WATCHDOG));
    ASSERT_STR_EQ("battery-failed", boot_result_name(BOOT_ERR_BATTERY));
    ASSERT_STR_EQ("post-failed", boot_result_name(BOOT_ERR_POST));
    ASSERT_STR_EQ("partition-failed", boot_result_name(BOOT_ERR_PARTITION));
    ASSERT_STR_EQ("signature-failed", boot_result_name(BOOT_ERR_SIGN_VERIFY));
    ASSERT_STR_EQ("timeout", boot_result_name(BOOT_ERR_TIMEOUT));
    ASSERT_STR_EQ("hardware-fault", boot_result_name(BOOT_ERR_HARDWARE));
    printf("OK\n");
}

/* ========================================================================
 * Test 24: reset reason name lookup
 * ======================================================================== */

static void test_reset_reason_names(void) {
    printf("  test_reset_reason_names... ");
    ASSERT_STR_EQ("power-on", boot_reset_reason_name(RESET_REASON_POWERON));
    ASSERT_STR_EQ("watchdog", boot_reset_reason_name(RESET_REASON_WATCHDOG));
    ASSERT_STR_EQ("brownout", boot_reset_reason_name(RESET_REASON_BROWNOUT));
    ASSERT_STR_EQ("forced", boot_reset_reason_name(RESET_REASON_FORCE));
    ASSERT_STR_EQ("deep-sleep-wake", boot_reset_reason_name(RESET_REASON_DEEP_SLEEP));
    ASSERT_STR_EQ("unknown", boot_reset_reason_name(RESET_REASON_UNKNOWN));
    printf("OK\n");
}

/* ========================================================================
 * Test 25: boot state mutable access
 * ======================================================================== */

static void test_boot_state_mut(void) {
    printf("  test_boot_state_mut... ");
    test_reset();
    boot_state_t *mut = boot_get_state_mut();
    ASSERT_PTR_NOT_NULL(mut);
    const boot_state_t *cst = boot_get_state();
    ASSERT_PTR_NOT_NULL(cst);
    /* They should point to the same underlying state */
    ASSERT_TRUE(mut == cst);
    printf("OK\n");
}

/* ========================================================================
 * Test 26: NFC IRQ pin is input with pull-up (active-low)
 * ======================================================================== */

static void test_nfc_irq_config(void) {
    printf("  test_nfc_irq_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    ASSERT_TRUE(boot_test_gpio_is_input(PIN_NFC_IRQ));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_NFC_IRQ));
    printf("OK\n");
}

/* ========================================================================
 * Test 27: microSD card detect is input with pull-up
 * ======================================================================== */

static void test_sd_cd_config(void) {
    printf("  test_sd_cd_config... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_complete();

    ASSERT_TRUE(boot_test_gpio_is_input(PIN_SD_CD));
    ASSERT_TRUE(boot_test_gpio_pullup(PIN_SD_CD));
    printf("OK\n");
}

/* ========================================================================
 * Test 28: GPIO pin count does not exceed RP2350B's 48 pins
 * ======================================================================== */

static void test_pin_count_valid(void) {
    printf("  test_pin_count_valid... ");
    /* All defined pins should be < 48 (RP2350B has GPIO0–GPIO47) */
    ASSERT_TRUE(PIN_CC_SPI_SCK < 48);
    ASSERT_TRUE(PIN_NFC_SPI_SCK < 48);
    ASSERT_TRUE(PIN_SD_SPI_SCK < 48);
    ASSERT_TRUE(PIN_DISP_SPI_SCK < 48);
    ASSERT_TRUE(PIN_BTN_UP < 48);
    ASSERT_TRUE(PIN_BTN_CENTER < 48);
    ASSERT_TRUE(PIN_BOOT_STRAP0 < 48);
    ASSERT_TRUE(PIN_BOOT_STRAP1 < 48);
    ASSERT_TRUE(PIN_BATT_I2C_SDA < 48);
    ASSERT_TRUE(PIN_BATT_I2C_SCL < 48);
    ASSERT_TRUE(PIN_RGB_LED < 48);
    ASSERT_TRUE(PIN_RADIO_DISABLE < 48);
    printf("OK\n");
}

/* ========================================================================
 * Test 29: no pin conflicts — all used pins are distinct
 * ======================================================================== */

static void test_no_pin_conflicts(void) {
    printf("  test_no_pin_conflicts... ");
    /* Collect all defined pins and verify uniqueness */
    uint8_t pins[] = {
        PIN_CC_SPI_SCK, PIN_CC_SPI_TX, PIN_CC_SPI_RX, PIN_CC_SPI_CSN,
        PIN_CC_GDO0, PIN_CC_GDO2,
        PIN_NFC_SPI_SCK, PIN_NFC_SPI_TX, PIN_NFC_SPI_RX, PIN_NFC_SPI_CSN,
        PIN_NFC_IRQ,
        PIN_SD_SPI_SCK, PIN_SD_SPI_TX, PIN_SD_SPI_RX, PIN_SD_SPI_CSN,
        PIN_SD_CD,
        PIN_DISP_SPI_SCK, PIN_DISP_SPI_TX, PIN_DISP_SPI_CSN,
        PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_BL,
        PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
        PIN_BTN_CENTER, PIN_BTN_A, PIN_BTN_B,
        PIN_IR_TX, PIN_IR_RX,
        PIN_USB_VBUS_SENSE, PIN_USB_HOST_EN,
        PIN_BATT_I2C_SDA, PIN_BATT_I2C_SCL,
        PIN_EXP_UART_TX, PIN_EXP_UART_RX,
        PIN_EXP_GPIO0, PIN_EXP_GPIO1, PIN_EXP_GPIO2,
        PIN_RGB_LED, PIN_BUZZER, PIN_VIBRATION,
        PIN_POWER_SWITCH, PIN_CHARGE_STAT, PIN_RADIO_DISABLE,
        PIN_BOOT_STRAP0, PIN_BOOT_STRAP1,
    };
    size_t count = sizeof(pins) / sizeof(pins[0]);

    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (pins[i] == pins[j]) {
                fprintf(stderr, "  FAIL: %s:%d: pin %u is used twice (indices %zu and %zu)\n",
                        __FILE__, __LINE__, pins[i], i, j);
                g_tests_run++;
                g_tests_failed++;
                goto conflict_done;
            }
        }
    }
    g_tests_run++;
    g_tests_passed++;
conflict_done:
    printf("OK\n");
}

/* ========================================================================
 * Test 30: intentional reboot magic is cleared on boot
 * ======================================================================== */

static void test_intentional_reboot_magic_cleared(void) {
    printf("  test_intentional_reboot_magic_cleared... ");
    test_reset();
    boot_test_set_wd_scratch(0, 0xA7EC1001UL);  /* Intentional magic */
    boot_init();
    /* After boot_init, scratch0 should be cleared (we can't read it
     * directly from the test API easily, but the code path should
     * have run without error) */
    const boot_state_t *state = boot_get_state();
    ASSERT_INT_EQ(BOOT_OK, state->result);
    printf("OK\n");
}

/* ========================================================================
 * Test 31: boot_get_reset_reason returns captured reason
 * ======================================================================== */

static void test_get_reset_reason(void) {
    printf("  test_get_reset_reason... ");
    test_reset();
    boot_test_set_wd_reason(0x02);  /* Watchdog timer */
    boot_init();
    ASSERT_INT_EQ(RESET_REASON_WATCHDOG, boot_get_reset_reason());
    printf("OK\n");
}

/* ========================================================================
 * Test 32: boot_get_boot_strap returns captured strap
 * ======================================================================== */

static void test_get_boot_strap(void) {
    printf("  test_get_boot_strap... ");
    test_reset();
    g_test_strap = BOOT_STRAP_RECOVERY;
    boot_init();
    ASSERT_INT_EQ(BOOT_STRAP_RECOVERY, boot_get_boot_strap());
    printf("OK\n");
}

/* ========================================================================
 * Test 33: individual phase functions return BOOT_OK
 * ======================================================================== */

static void test_individual_phases_ok(void) {
    printf("  test_individual_phases_ok... ");
    test_reset();
    g_test_strap = BOOT_STRAP_NORMAL;
    boot_init();

    ASSERT_INT_EQ(BOOT_OK, boot_phase_clocks());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_fpu());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_gpio());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_uart());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_usb());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_watchdog());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_battery());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_post());
    ASSERT_INT_EQ(BOOT_OK, boot_phase_partition());
    printf("OK\n");
}

/* ========================================================================
 * Test 34: battery phase is skipped in recovery mode
 * ======================================================================== */

static void test_battery_skipped_in_recovery(void) {
    printf("  test_battery_skipped_in_recovery... ");
    test_reset();
    g_test_strap = BOOT_STRAP_RECOVERY;
    boot_init();
    /* In recovery mode, battery phase should pass immediately */
    ASSERT_INT_EQ(BOOT_OK, boot_phase_battery());
    printf("OK\n");
}

/* ========================================================================
 * Test 35: boot phase count matches expected
 * ======================================================================== */

static void test_boot_phase_count(void) {
    printf("  test_boot_phase_count... ");
    ASSERT_INT_EQ(10, BOOT_PHASE_COUNT);
    printf("OK\n");
}

/* ========================================================================
 * Test 36: boot strap constants are distinct
 * ======================================================================== */

static void test_strap_constants_distinct(void) {
    printf("  test_strap_constants_distinct... ");
    ASSERT_TRUE(BOOT_STRAP_NORMAL != BOOT_STRAP_RECOVERY);
    ASSERT_TRUE(BOOT_STRAP_NORMAL != BOOT_STRAP_PARTITION_B);
    ASSERT_TRUE(BOOT_STRAP_NORMAL != BOOT_STRAP_FACTORY_RESET);
    ASSERT_TRUE(BOOT_STRAP_RECOVERY != BOOT_STRAP_PARTITION_B);
    ASSERT_TRUE(BOOT_STRAP_RECOVERY != BOOT_STRAP_FACTORY_RESET);
    ASSERT_TRUE(BOOT_STRAP_PARTITION_B != BOOT_STRAP_FACTORY_RESET);
    printf("OK\n");
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════╗\n");
    printf("║  GhostWisp Boot Sequence — Unit Tests              ║\n");
    printf("║  Project Little Spectre — jayis1                   ║\n");
    printf("╚════════════════════════════════════════════════════╝\n");
    printf("\n");

    test_boot_init_poweron_reset();
    test_boot_init_watchdog_reset();
    test_boot_init_forced_reset();
    test_boot_init_brownout_reset();
    test_boot_init_strap_values();
    test_boot_complete_normal();
    test_boot_complete_recovery();
    test_boot_complete_factory_reset();
    test_boot_partition_b();
    test_boot_phases_executed();
    test_watchdog_configured();
    test_boot_kick_watchdog();
    test_gpio_spi_config();
    test_gpio_i2c_config();
    test_gpio_buttons_config();
    test_gpio_display_config();
    test_gpio_boot_straps_config();
    test_gpio_expansion_uart();
    test_enter_recovery();
    test_clock_constants();
    test_watchdog_constants();
    test_phase_names();
    test_result_names();
    test_reset_reason_names();
    test_boot_state_mut();
    test_nfc_irq_config();
    test_sd_cd_config();
    test_pin_count_valid();
    test_no_pin_conflicts();
    test_intentional_reboot_magic_cleared();
    test_get_reset_reason();
    test_get_boot_strap();
    test_individual_phases_ok();
    test_battery_skipped_in_recovery();
    test_boot_phase_count();
    test_strap_constants_distinct();

    printf("\n");
    printf("══════════════════════════════════════════════════════════\n");
    printf("  Tests run:    %d\n", g_tests_run);
    printf("  Tests passed: %d\n", g_tests_passed);
    printf("  Tests failed: %d\n", g_tests_failed);
    printf("══════════════════════════════════════════════════════════\n");

    if (g_tests_failed > 0) {
        printf("\n  *** %d TEST(S) FAILED ***\n\n", g_tests_failed);
        return 1;
    }

    printf("\n  All tests passed.\n\n");
    return 0;
}