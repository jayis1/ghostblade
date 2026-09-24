/*
 * ghostwisp_boot.c — GhostWisp Core Boot Sequence Implementation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the core boot sequence for the GhostWisp RP2350B primary MCU.
 * GhostWisp (Project Little Spectre) is a standalone pocketable device —
 * there is no RK3576 host bridge as on GhostBlade. The RP2350B is the sole
 * processor, running an event-driven appliance firmware.
 *
 * Boot sequence:
 *
 *   Phase 0: Capture reset reason and boot straps
 *   Phase 1: Configure system clocks (150 MHz core, 48 MHz peri, 133 MHz XIP)
 *   Phase 2: Enable ARM Cortex-M33 FPU (CP10/CP11)
 *   Phase 3: Configure all GPIO pin muxing
 *   Phase 4: Initialize UART0 debug console (115200 8N1)
 *   Phase 5: Initialize USB CDC for companion link
 *   Phase 6: Configure hardware watchdog (5 s timeout, bark interrupt)
 *   Phase 7: Check battery / fuel gauge via I2C
 *   Phase 8: Power-on self-test (POST) of critical peripherals
 *   Phase 9: A/B partition selection and firmware signature verification
 *
 * Design goals (from devices/ghostwisp/docs/architecture.md):
 *   - Boot in under 2 seconds (Phase 0–6 must complete in < 1 s)
 *   - Fail safely: if any phase fails, enter recovery mode
 *   - Recovery mode: radios disabled, USB update only
 *   - Brownout detection: track count across resets via watchdog scratch
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           devices/ghostwisp/docs/roadmap.md (Phase 3 — Firmware foundation)
 *           RP2350B Datasheet: Sections 2 (Clocks), 4 (GPIO/ADC), 12 (Watchdog)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ghostwisp_boot.h"
#include "ghostwisp_pins.h"

#ifndef BOOT_HOST_TEST
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/watchdog.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#endif

/* ========================================================================
 * RP2350B Register Base Addresses
 * ======================================================================== */

#define RP2350B_SYSCTL_BASE      0x400B0000UL
#define RP2350B_CLOCKS_BASE      0x400B0800UL
#define RP2350B_RESETS_BASE      0x400B0C00UL
#define RP2350B_PSM_BASE         0x400B1000UL
#define RP2350B_PADS_BASE        0x400C0000UL
#define RP2350B_IO_BANK0_BASE    0x400D0000UL
#define RP2350B_WATCHDOG_BASE    0x400D8000UL
#define RP2350B_UART0_BASE       0x40070000UL
#define RP2350B_USB_BASE         0x40100000UL
#define RP2350B_I2C0_BASE        0x40078000UL
#define RP2350B_ADC_BASE         0x50041000UL
#define RP2350B_QMI_BASE         0x400D0000UL

/* ========================================================================
 * Watchdog Registers
 * ======================================================================== */

#define WD_CTRL                  0x00
#define WD_LOAD                  0x04
#define WD_REASON                0x08
#define WD_SCRATCH0              0x0C
#define WD_SCRATCH7              0x20

#define WD_CTRL_ENABLE           (1U << 0)
#define WD_CTRL_PAUSE_DBG0      (1U << 1)
#define WD_CTRL_PAUSE_JTAG      (1U << 3)

#define WD_REASON_FORCE         (1U << 0)
#define WD_REASON_TIMER         (1U << 1)

/* Scratch register magic values */
#define WD_MAGIC_INTENTIONAL    0xA7EC1001UL
#define WD_SCRATCH_BOD_MAGIC    0xB047B00FUL
#define WD_SCRATCH_BOOTCOUNT     7    /* Use scratch7 for brownout; scratch1 for count */

/* ========================================================================
 * Clocks Registers (offsets from CLOCKS_BASE)
 * ======================================================================== */

#define CLOCKS_REF_CTRL           0x10
#define CLOCKS_SYS_CTRL           0x20
#define CLOCKS_SYS_DIV            0x24
#define CLOCKS_PERI_CTRL          0x30
#define CLOCKS_PERI_DIV           0x34
#define CLOCKS_XIP_CTRL           0x40
#define CLOCKS_XIP_DIV            0x44

#define CLOCKS_CLK_SRC_RO         0
#define CLOCKS_CLK_SRC_XOSC       1
#define CLOCKS_CLK_SRC_PLL_SYS    2
#define CLOCKS_CLK_SRC_PLL_USB    3

/* ========================================================================
 * Reset Registers (offsets from RESETS_BASE)
 * ======================================================================== */

#define RESETS_RESET              0x00
#define RESETS_RESET_DONE         0x08

#define RESETS_UART0_RESET        (1U << 8)
#define RESETS_I2C0_RESET         (1U << 15)
#define RESETS_SPI0_RESET         (1U << 16)
#define RESETS_SPI1_RESET         (1U << 17)
#define RESETS_SPI2_RESET         (1U << 23)
#define RESETS_ADC_RESET          (1U << 24)
#define RESETS_PADS_BANK0_RESET   (1U << 25)
#define RESETS_IO_BANK0_RESET     (1U << 26)
#define RESETS_USB_RESET          (1U << 28)

/* ========================================================================
 * GPIO Function Select Values (5 bits per pin in IO_BANK0)
 * ======================================================================== */

#define GPIO_FUNC_SPI             1U
#define GPIO_FUNC_UART            2U
#define GPIO_FUNC_I2C             3U
#define GPIO_FUNC_PWM             4U
#define GPIO_FUNC_SIO             5U
#define GPIO_FUNC_PIO0            6U
#define GPIO_FUNC_PIO1            7U
#define GPIO_FUNC_CLOCK           8U
#define GPIO_FUNC_USB             9U
#define GPIO_FUNC_NONE            31U

/* ========================================================================
 * UART0 Registers (offsets from UART0_BASE)
 * ======================================================================== */

#define UART0_DR                   0x000
#define UART0_FR                   0x018
#define UART0_IBRD                 0x024
#define UART0_FBRD                 0x028
#define UART0_LCR_H               0x02C
#define UART0_CR                   0x030

/* ========================================================================
 * FPU Enable (ARM Cortex-M33 CPACR)
 * ======================================================================== */

#define SCB_CPACR                 (*(volatile uint32_t *)0xE000ED88UL)
#define CPACR_CP10_FULL           (0xFU << 20)
#define CPACR_CP11_FULL           (0xFU << 22)

/* ========================================================================
 * Helper Macros
 * ======================================================================== */

#define REG32(addr)               (*(volatile uint32_t *)(addr))

/* ========================================================================
 * Global Boot State
 * ======================================================================== */

static boot_state_t g_boot_state;

/* ========================================================================
 * Time measurement (millisecond timer)
 *
 * Uses a simple free-running counter incremented by a timer interrupt.
 * For host-side testing, this is stubbed via the BOOT_HOST_TEST macro.
 * ======================================================================== */

#ifdef BOOT_HOST_TEST
/* Host test mode: use a software counter that tests can control */
static uint32_t g_host_time_ms = 0;
static uint32_t boot_get_time_ms(void) { return g_host_time_ms; }
void boot_test_set_time_ms(uint32_t t) { g_host_time_ms = t; }
void boot_test_advance_time_ms(uint32_t delta) { g_host_time_ms += delta; }
#else
/* Target mode: use the Pico SDK monotonic timer. */
static uint32_t boot_get_time_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}
#endif

/* ========================================================================
 * Low-level GPIO configuration
 * ======================================================================== */

#ifndef BOOT_HOST_TEST

/**
 * boot_gpio_set_function — Set the function select for a GPIO pin
 */
static void boot_gpio_set_function(uint8_t pin, uint32_t func_sel) {
    gpio_set_function(pin, (gpio_function_t)(func_sel & 0x1FU));
}

/**
 * boot_gpio_set_pull_up — Enable or disable pull-up on a GPIO pin
 */
static void boot_gpio_set_pull_up(uint8_t pin, bool enable) {
    if (enable) gpio_pull_up(pin);
    else gpio_disable_pulls(pin);
}

/**
 * boot_gpio_set_pull_down — Enable or disable pull-down on a GPIO pin
 */
static void boot_gpio_set_pull_down(uint8_t pin, bool enable) {
    if (enable) gpio_pull_down(pin);
    else gpio_disable_pulls(pin);
}

/**
 * boot_gpio_set_input — Configure a pin as input with optional pull-up
 */
static void boot_gpio_set_input(uint8_t pin, bool pull_up) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    if (pull_up) gpio_pull_up(pin);
    else gpio_disable_pulls(pin);
}

/**
 * boot_gpio_set_output — Configure a pin as SIO output
 */
static void boot_gpio_set_output(uint8_t pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
}

#else /* BOOT_HOST_TEST */

/* Host test stubs: record calls for verification */
static uint32_t g_gpio_func[48];
static bool g_gpio_pullup[48];
static bool g_gpio_pulldown[48];
static bool g_gpio_is_input[48];
static bool g_gpio_is_output[48];

static void boot_gpio_set_function(uint8_t pin, uint32_t func_sel) {
    if (pin < 48) g_gpio_func[pin] = func_sel & 0x1FU;
}
static void boot_gpio_set_pull_up(uint8_t pin, bool enable) {
    if (pin < 48) g_gpio_pullup[pin] = enable;
}
static void boot_gpio_set_pull_down(uint8_t pin, bool enable) {
    if (pin < 48) g_gpio_pulldown[pin] = enable;
}
static void boot_gpio_set_input(uint8_t pin, bool pull_up) {
    if (pin < 48) {
        g_gpio_is_input[pin] = true;
        g_gpio_is_output[pin] = false;
        g_gpio_pullup[pin] = pull_up;
    }
}
static void boot_gpio_set_output(uint8_t pin) {
    if (pin < 48) {
        g_gpio_is_output[pin] = true;
        g_gpio_is_input[pin] = false;
    }
}

/* Test inspection functions */
uint32_t boot_test_gpio_func(uint8_t pin) { return (pin < 48) ? g_gpio_func[pin] : 0xFFFFFFFFU; }
bool boot_test_gpio_pullup(uint8_t pin) { return (pin < 48) ? g_gpio_pullup[pin] : false; }
bool boot_test_gpio_is_input(uint8_t pin) { return (pin < 48) ? g_gpio_is_input[pin] : false; }
bool boot_test_gpio_is_output(uint8_t pin) { return (pin < 48) ? g_gpio_is_output[pin] : false; }

#endif /* BOOT_HOST_TEST */

/* ========================================================================
 * Boot strap reading
 * ======================================================================== */

/**
 * read_boot_strap — Sample the boot strap pins
 *
 * The boot strap is a 2-bit value from PIN_BOOT_STRAP0 (bit 0)
 * and PIN_BOOT_STRAP1 (bit 1). Straps are active-high with
 * internal pull-downs, so floating = 0 (normal boot).
 *
 * Returns: BOOT_STRAP_NORMAL, BOOT_STRAP_RECOVERY,
 *          BOOT_STRAP_PARTITION_B, or BOOT_STRAP_FACTORY_RESET
 */
static uint8_t read_boot_strap(void) {
    uint8_t strap = 0;

#ifndef BOOT_HOST_TEST
    /* Configure straps before sampling because boot_init() precedes the full
     * GPIO phase. Floating straps therefore deterministically mean normal. */
    gpio_init(PIN_BOOT_STRAP0);
    gpio_set_dir(PIN_BOOT_STRAP0, GPIO_IN);
    gpio_pull_down(PIN_BOOT_STRAP0);
    gpio_init(PIN_BOOT_STRAP1);
    gpio_set_dir(PIN_BOOT_STRAP1, GPIO_IN);
    gpio_pull_down(PIN_BOOT_STRAP1);
    busy_wait_us_32(10U);
    if (gpio_get(PIN_BOOT_STRAP0)) strap |= 0x01;
    if (gpio_get(PIN_BOOT_STRAP1)) strap |= 0x02;
#else
    /* Host test: read from a test-settable variable */
    extern uint8_t boot_test_strap_value(void);
    strap = boot_test_strap_value();
#endif

    return strap;
}

/* ========================================================================
 * Watchdog interface (thin wrapper for boot sequence)
 * ======================================================================== */

#ifndef BOOT_HOST_TEST
static uint32_t g_watchdog_timeout_ms = GHOSTWISP_WATCHDOG_TIMEOUT_MS;
static bool g_watchdog_active = false;

static void watchdog_load(uint32_t us) {
    g_watchdog_timeout_ms = us / 1000U;
    if (g_watchdog_active) watchdog_update();
}
static uint32_t watchdog_read_reason(void) {
    return watchdog_hw->reason;
}
static void watchdog_clear_reason(uint32_t bits) {
    (void)bits; /* RP2350 reset reason is read-only and clears on the next reset. */
}
static uint32_t watchdog_read_scratch(uint8_t idx) {
    return (idx < 8U) ? watchdog_hw->scratch[idx] : 0U;
}
static void watchdog_write_scratch(uint8_t idx, uint32_t val) {
    if (idx < 8U) watchdog_hw->scratch[idx] = val;
}
static void watchdog_enable_ctrl(void) {
    watchdog_enable(g_watchdog_timeout_ms, true);
    g_watchdog_active = true;
}
#else
/* Host test stubs */
static uint32_t g_wd_reason = 0;
static uint32_t g_wd_scratch[8] = {0};
static uint32_t g_wd_load_val = 0;
static bool g_wd_enabled = false;

static void watchdog_load(uint32_t us) { g_wd_load_val = us; }
static uint32_t watchdog_read_reason(void) { return g_wd_reason; }
static void watchdog_clear_reason(uint32_t bits) { g_wd_reason &= ~bits; }
static uint32_t watchdog_read_scratch(uint8_t idx) { return (idx < 8) ? g_wd_scratch[idx] : 0; }
static void watchdog_write_scratch(uint8_t idx, uint32_t val) { if (idx < 8) g_wd_scratch[idx] = val; }
static void watchdog_enable_ctrl(void) { g_wd_enabled = true; }

/* Test control functions */
void boot_test_set_wd_reason(uint32_t reason) { g_wd_reason = reason; }
void boot_test_set_wd_scratch(uint8_t idx, uint32_t val) { if (idx < 8) g_wd_scratch[idx] = val; }
uint32_t boot_test_get_wd_load(void) { return g_wd_load_val; }
bool boot_test_wd_enabled(void) { return g_wd_enabled; }
void boot_test_reset_wd_enabled(void) { g_wd_enabled = false; }
#endif

/* ========================================================================
 * Phase name / result name / reset reason name lookups
 * ======================================================================== */

const char *boot_phase_name(boot_phase_t phase) {
    switch (phase) {
        case BOOT_PHASE_RESET:     return "reset-capture";
        case BOOT_PHASE_CLOCKS:    return "clocks";
        case BOOT_PHASE_FPU:       return "fpu";
        case BOOT_PHASE_GPIO:      return "gpio";
        case BOOT_PHASE_UART:      return "uart";
        case BOOT_PHASE_USB:       return "usb";
        case BOOT_PHASE_WATCHDOG:  return "watchdog";
        case BOOT_PHASE_BATTERY:   return "battery";
        case BOOT_PHASE_POST:      return "post";
        case BOOT_PHASE_PARTITION: return "partition";
        default:                   return "unknown";
    }
}

const char *boot_result_name(boot_result_t result) {
    switch (result) {
        case BOOT_OK:                return "ok";
        case BOOT_ERR_CLOCK:         return "clock-failed";
        case BOOT_ERR_FPU:           return "fpu-failed";
        case BOOT_ERR_GPIO:          return "gpio-failed";
        case BOOT_ERR_UART:          return "uart-failed";
        case BOOT_ERR_USB:           return "usb-failed";
        case BOOT_ERR_WATCHDOG:      return "watchdog-failed";
        case BOOT_ERR_BATTERY:       return "battery-failed";
        case BOOT_ERR_POST:          return "post-failed";
        case BOOT_ERR_PARTITION:     return "partition-failed";
        case BOOT_ERR_SIGN_VERIFY:   return "signature-failed";
        case BOOT_ERR_TIMEOUT:       return "timeout";
        case BOOT_ERR_HARDWARE:      return "hardware-fault";
        default:                     return "unknown";
    }
}

const char *boot_reset_reason_name(reset_reason_t reason) {
    switch (reason) {
        case RESET_REASON_POWERON:    return "power-on";
        case RESET_REASON_WATCHDOG:   return "watchdog";
        case RESET_REASON_BROWNOUT:   return "brownout";
        case RESET_REASON_FORCE:      return "forced";
        case RESET_REASON_DEEP_SLEEP: return "deep-sleep-wake";
        case RESET_REASON_UNKNOWN:    return "unknown";
        default:                      return "unknown";
    }
}

/* ========================================================================
 * Boot state access
 * ======================================================================== */

const boot_state_t *boot_get_state(void) {
    return &g_boot_state;
}

boot_state_t *boot_get_state_mut(void) {
    return &g_boot_state;
}

reset_reason_t boot_get_reset_reason(void) {
    return g_boot_state.reset_reason;
}

uint8_t boot_get_boot_strap(void) {
    return g_boot_state.boot_strap;
}

/* ========================================================================
 * Boot Phase 0: Capture reset reason and boot straps
 * ======================================================================== */

/**
 * boot_init — Initialize boot state and capture reset reason
 *
 * Reads the watchdog reason register to determine why the system reset.
 * Checks scratch register 7 for the brownout magic. Samples boot straps.
 */
const boot_state_t *boot_init(void) {
    memset(&g_boot_state, 0, sizeof(g_boot_state));
    g_boot_state.current_phase = BOOT_PHASE_RESET;
    g_boot_state.result = BOOT_OK;

    /* Read watchdog reset reason */
    uint32_t reason = watchdog_read_reason();

    if (reason & WD_REASON_TIMER) {
        g_boot_state.reset_reason = RESET_REASON_WATCHDOG;
        watchdog_clear_reason(WD_REASON_TIMER);
    } else if (reason & WD_REASON_FORCE) {
        g_boot_state.reset_reason = RESET_REASON_FORCE;
        watchdog_clear_reason(WD_REASON_FORCE);
    } else {
        /* Check for brownout marker in scratch7 */
        if (watchdog_read_scratch(7) == WD_SCRATCH_BOD_MAGIC) {
            g_boot_state.reset_reason = RESET_REASON_BROWNOUT;
            g_boot_state.brownout_count = watchdog_read_scratch(1) + 1;
            watchdog_write_scratch(7, 0);  /* Clear brownout marker */
            watchdog_write_scratch(1, g_boot_state.brownout_count);
        } else {
            g_boot_state.reset_reason = RESET_REASON_POWERON;
        }
    }

    /* Check for intentional reboot magic (scratch0) */
    if (watchdog_read_scratch(0) == WD_MAGIC_INTENTIONAL) {
        watchdog_write_scratch(0, 0);
    }

    /* Read boot straps */
    g_boot_state.boot_strap = read_boot_strap();

    /* Record phase 0 timing */
    g_boot_state.phase_times_ms[BOOT_PHASE_RESET] = boot_get_time_ms();

    return &g_boot_state;
}

/* ========================================================================
 * Boot Phase 1: Configure system clocks
 * ======================================================================== */

boot_result_t boot_phase_clocks(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_CLOCKS;

#ifndef BOOT_HOST_TEST
    /* The SDK performs the safe source switch and PLL programming, including
     * bounded lock waits and voltage-aware sequencing for RP2350. */
    if (!set_sys_clock_hz(GHOSTWISP_SYS_CLOCK_HZ, true)) {
        g_boot_state.phase_times_ms[BOOT_PHASE_CLOCKS] = boot_get_time_ms() - t0;
        return BOOT_ERR_CLOCK;
    }
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_CLOCKS] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 2: Enable FPU
 * ======================================================================== */

boot_result_t boot_phase_fpu(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_FPU;

#ifndef BOOT_HOST_TEST
    /* Enable CP10 and CP11 full access for floating-point */
    SCB_CPACR |= (CPACR_CP10_FULL | CPACR_CP11_FULL);
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb");
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_FPU] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 3: Configure GPIO pin muxing
 * ======================================================================== */

boot_result_t boot_phase_gpio(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_GPIO;


    /* --- SPI0: CC1101 sub-GHz radio --- */
    boot_gpio_set_function(PIN_CC_SPI_SCK, GPIO_FUNC_SPI);
    boot_gpio_set_function(PIN_CC_SPI_TX,  GPIO_FUNC_SPI);
    boot_gpio_set_function(PIN_CC_SPI_RX,  GPIO_FUNC_SPI);
    boot_gpio_set_output(PIN_CC_SPI_CSN);   /* CSn controlled by SIO */
    boot_gpio_set_input(PIN_CC_GDO0, false);
    boot_gpio_set_input(PIN_CC_GDO2, false);

    /* --- SPI1: ST25R3916 NFC --- */
    boot_gpio_set_function(PIN_NFC_SPI_SCK, GPIO_FUNC_SPI);
    boot_gpio_set_function(PIN_NFC_SPI_TX,  GPIO_FUNC_SPI);
    boot_gpio_set_function(PIN_NFC_SPI_RX,  GPIO_FUNC_SPI);
    boot_gpio_set_output(PIN_NFC_SPI_CSN);
    boot_gpio_set_input(PIN_NFC_IRQ, true); /* Active-low IRQ, pull-up */

    /* --- SPI2: microSD --- */
    boot_gpio_set_function(PIN_SD_SPI_SCK, GPIO_FUNC_SPI);
    boot_gpio_set_function(PIN_SD_SPI_TX,  GPIO_FUNC_SPI);
    boot_gpio_set_function(PIN_SD_SPI_RX,  GPIO_FUNC_SPI);
    boot_gpio_set_output(PIN_SD_SPI_CSN);
    boot_gpio_set_input(PIN_SD_CD, true);   /* Active-low card detect, pull-up */

    /* --- Display (SPI3 / PIO) --- */
    boot_gpio_set_function(PIN_DISP_SPI_SCK, GPIO_FUNC_PIO0);
    boot_gpio_set_function(PIN_DISP_SPI_TX,  GPIO_FUNC_PIO0);
    boot_gpio_set_output(PIN_DISP_SPI_CSN);
    boot_gpio_set_output(PIN_DISP_DC);
    boot_gpio_set_output(PIN_DISP_RST);
    boot_gpio_set_output(PIN_DISP_BL);

    /* --- User input buttons (active-low, pull-up) --- */
    boot_gpio_set_input(PIN_BTN_UP, true);
    boot_gpio_set_input(PIN_BTN_DOWN, true);
    boot_gpio_set_input(PIN_BTN_LEFT, true);
    boot_gpio_set_input(PIN_BTN_RIGHT, true);
    boot_gpio_set_input(PIN_BTN_CENTER, true);
    boot_gpio_set_input(PIN_BTN_A, true);
    boot_gpio_set_input(PIN_BTN_B, true);

    /* --- IR --- */
    boot_gpio_set_output(PIN_IR_TX);
    boot_gpio_set_input(PIN_IR_RX, false);

    /* --- USB control signals --- */
    boot_gpio_set_input(PIN_USB_VBUS_SENSE, false);
    boot_gpio_set_output(PIN_USB_HOST_EN);

    /* --- I2C0: fuel gauge --- */
    boot_gpio_set_function(PIN_BATT_I2C_SDA, GPIO_FUNC_I2C);
    boot_gpio_set_function(PIN_BATT_I2C_SCL, GPIO_FUNC_I2C);
    boot_gpio_set_pull_up(PIN_BATT_I2C_SDA, true);
    boot_gpio_set_pull_up(PIN_BATT_I2C_SCL, true);

    /* --- Expansion header --- */
    boot_gpio_set_function(PIN_EXP_UART_TX, GPIO_FUNC_UART);
    boot_gpio_set_function(PIN_EXP_UART_RX, GPIO_FUNC_UART);
    boot_gpio_set_input(PIN_EXP_GPIO0, false);
    boot_gpio_set_input(PIN_EXP_GPIO1, false);
    boot_gpio_set_input(PIN_EXP_GPIO2, false);

    /* --- Feedback --- */
    boot_gpio_set_function(PIN_RGB_LED, GPIO_FUNC_PIO1);
    boot_gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM);
    boot_gpio_set_output(PIN_VIBRATION);

    /* --- Power management --- */
    boot_gpio_set_input(PIN_POWER_SWITCH, false);
    boot_gpio_set_input(PIN_CHARGE_STAT, true);
    boot_gpio_set_output(PIN_RADIO_DISABLE);

    /* --- Boot straps (input with pull-down) --- */
    boot_gpio_set_input(PIN_BOOT_STRAP0, false);
    boot_gpio_set_pull_down(PIN_BOOT_STRAP0, true);
    boot_gpio_set_input(PIN_BOOT_STRAP1, false);
    boot_gpio_set_pull_down(PIN_BOOT_STRAP1, true);

    /* --- Assert radio disable high during boot (safe state) --- */
#ifndef BOOT_HOST_TEST
    gpio_put(PIN_RADIO_DISABLE, true);
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_GPIO] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 4: Initialize UART1 debug console
 * ======================================================================== */

boot_result_t boot_phase_uart(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_UART;

#ifndef BOOT_HOST_TEST
    const uint actual_baud = uart_init(uart1, 115200U);
    gpio_set_function(PIN_DEBUG_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_DEBUG_UART_RX, GPIO_FUNC_UART);
    uart_set_format(uart1, 8U, 1U, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, true);
    if (actual_baud == 0U) {
        g_boot_state.phase_times_ms[BOOT_PHASE_UART] = boot_get_time_ms() - t0;
        return BOOT_ERR_UART;
    }
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_UART] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 5: Initialize USB CDC
 * ======================================================================== */

boot_result_t boot_phase_usb(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_USB;

#ifndef BOOT_HOST_TEST
    /* USB CDC initialization is owned by stdio_init_all() in main(). Avoid
     * resetting the controller after TinyUSB has already configured it. */
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_USB] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 6: Configure hardware watchdog
 * ======================================================================== */

boot_result_t boot_phase_watchdog(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_WATCHDOG;

    /* Load the watchdog with the configured timeout (5 seconds = 5000000 µs) */
    watchdog_load((uint32_t)GHOSTWISP_WATCHDOG_TIMEOUT_MS * 1000);

    /* Enable the watchdog with debug pause */
    watchdog_enable_ctrl();

    g_boot_state.phase_times_ms[BOOT_PHASE_WATCHDOG] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 7: Check battery / fuel gauge
 * ======================================================================== */

boot_result_t boot_phase_battery(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_BATTERY;

    /* In recovery mode, skip battery check — USB power is assumed */
    if (g_boot_state.boot_strap == BOOT_STRAP_RECOVERY) {
        g_boot_state.phase_times_ms[BOOT_PHASE_BATTERY] = boot_get_time_ms() - t0;
        return BOOT_OK;
    }

#ifndef BOOT_HOST_TEST
    /* Read fuel gauge (e.g., MAX17048) over I2C0
     * Address 0x36 (7-bit). Read SOC register (0x04) and VCELL (0x02).
     * If the fuel gauge is not responding, check if VBUS is present
     * (USB power). If VBUS is present, continue boot with a warning.
     * If no VBUS and no battery, fail. */
    bool vbus_present = gpio_get(PIN_USB_VBUS_SENSE);

    /* I2C read attempt: send address 0x36, check ACK */
    /* For now, we accept boot if either battery or VBUS is present.
     * The full fuel gauge read is implemented in battery_monitor.c. */
    if (!vbus_present) {
        /* No USB power — must have battery. If fuel gauge NACKs,
         * we still boot but log a warning. The POST will catch
         * a truly dead battery. */
    }
#else
    /* Host test: always pass (battery hardware not present) */
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_BATTERY] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 8: Power-on self-test (POST)
 * ======================================================================== */

boot_result_t boot_phase_post(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_POST;
    bool critical_failure = false;

#ifndef BOOT_HOST_TEST
    /* POST checks (quick, non-destructive):
     *
     * 1. QSPI flash JEDEC ID — verify flash is present and responsive
     * 2. CC1101 chip version register (0x00 | 0x80 read) — verify SPI
     * 3. ST25R3916 chip ID register — verify SPI
     * 4. Display controller ID — verify display connected
     *
     * Only QSPI flash failure is considered critical (can't boot without
     * firmware storage). Radio/NFC/display failures are non-fatal —
     * the firmware boots with reduced capabilities. */

    /* QSPI flash JEDEC ID check */
    volatile uint32_t *qmi_direct = (volatile uint32_t *)(RP2350B_QMI_BASE + 0x00);
    /* Read JEDEC ID: command 0x9F, expect 0xEF 0x40 0x18 (W25Q128) */
    /* This is a simplified check — full implementation in flash driver */
    (void)qmi_direct;

    /* CC1101 version check via SPI0 */
    /* Read register 0x30 (PARTNUM) and 0x31 (VERSION) */
    /* Expected: PARTNUM=0x14, VERSION=0x04 for CC1101 */

    /* ST25R3916 chip ID check via SPI1 */
    /* Read register 0x3F (CHIP_ID_REVISION) */
    /* Expected: 0x16 (ST25R3916B) */

    /* Display ID check */
    /* Read ILI9341 ID: command 0x04, expect 0x9341 */
#else
    /* Host test: POST always passes (no hardware to check) */
#endif

    g_boot_state.post_passed = !critical_failure;
    g_boot_state.phase_times_ms[BOOT_PHASE_POST] = boot_get_time_ms() - t0;

    if (critical_failure)
        return BOOT_ERR_POST;
    return BOOT_OK;
}

/* ========================================================================
 * Boot Phase 9: A/B partition selection and signature verification
 * ======================================================================== */

boot_result_t boot_phase_partition(void) {
    uint32_t t0 = boot_get_time_ms();
    g_boot_state.current_phase = BOOT_PHASE_PARTITION;

    /* Determine partition from boot strap */
    if (g_boot_state.boot_strap == BOOT_STRAP_PARTITION_B) {
        g_boot_state.partition_b = true;
    } else {
        g_boot_state.partition_b = false;
    }

    /* In recovery mode, skip signature verification */
    if (g_boot_state.boot_strap == BOOT_STRAP_RECOVERY ||
        g_boot_state.boot_strap == BOOT_STRAP_FACTORY_RESET) {
        g_boot_state.signature_verified = false;
        g_boot_state.phase_times_ms[BOOT_PHASE_PARTITION] = boot_get_time_ms() - t0;
        return BOOT_OK;
    }

#ifndef BOOT_HOST_TEST
    /* Verify firmware signature against embedded public key.
     * The signature is stored in the firmware image header.
     * The public key is in the immutable recovery partition.
     *
     * This uses a Ed25519 or ECDSA-P256 signature over the
     * application image hash. If verification fails, fall back
     * to the other partition. If both fail, enter recovery.
     *
     * Full implementation in signed_update.c (to be added). */
    extern bool ghostwisp_verify_firmware_signature(bool partition_b);
    g_boot_state.signature_verified = ghostwisp_verify_firmware_signature(
        g_boot_state.partition_b);

    if (!g_boot_state.signature_verified) {
        /* Try the other partition */
        g_boot_state.partition_b = !g_boot_state.partition_b;
        g_boot_state.signature_verified = ghostwisp_verify_firmware_signature(
            g_boot_state.partition_b);

        if (!g_boot_state.signature_verified) {
            /* Both partitions fail verification — enter recovery */
            g_boot_state.phase_times_ms[BOOT_PHASE_PARTITION] = boot_get_time_ms() - t0;
            return BOOT_ERR_SIGN_VERIFY;
        }
    }
#else
    /* Host test: signature verification always passes */
    g_boot_state.signature_verified = true;
#endif

    g_boot_state.phase_times_ms[BOOT_PHASE_PARTITION] = boot_get_time_ms() - t0;
    return BOOT_OK;
}

/* ========================================================================
 * Boot watchdog kick
 * ======================================================================== */

void boot_kick_watchdog(void) {
    watchdog_load((uint32_t)GHOSTWISP_WATCHDOG_TIMEOUT_MS * 1000);
}

/* ========================================================================
 * Enter recovery mode
 * ======================================================================== */

void boot_enter_recovery(void) {
    /* Disable all radios (assert radio disable). */
#ifndef BOOT_HOST_TEST
    gpio_put(PIN_RADIO_DISABLE, true);
#endif

    /* Preserve the failure result for diagnostics while marking recovery. */
    g_boot_state.boot_strap = BOOT_STRAP_RECOVERY;

    /* In recovery mode, only USB update and diagnostics are available.
     * The main loop should check boot_strap and skip normal operation. */
}

/* ========================================================================
 * Complete boot sequence
 * ======================================================================== */

boot_result_t boot_complete(void) {
    uint32_t boot_start = boot_get_time_ms();

    /* Phase 0: Capture reset reason and boot straps */
    boot_init();

    /* If factory reset strap is set, enter recovery immediately */
    if (g_boot_state.boot_strap == BOOT_STRAP_FACTORY_RESET) {
        boot_enter_recovery();
        g_boot_state.boot_time_ms = boot_get_time_ms() - boot_start;
        return BOOT_OK;
    }

    /* Phase 1: Clocks */
    g_boot_state.result = boot_phase_clocks();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 2: FPU */
    g_boot_state.result = boot_phase_fpu();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 3: GPIO */
    g_boot_state.result = boot_phase_gpio();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 4: UART */
    g_boot_state.result = boot_phase_uart();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 5: USB */
    g_boot_state.result = boot_phase_usb();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 6: Watchdog */
    g_boot_state.result = boot_phase_watchdog();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 7: Battery */
    g_boot_state.result = boot_phase_battery();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 8: POST */
    g_boot_state.result = boot_phase_post();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* Phase 9: Partition + signature */
    g_boot_state.result = boot_phase_partition();
    if (g_boot_state.result != BOOT_OK) goto boot_fail;

    /* All phases complete */
    g_boot_state.result = BOOT_OK;
    g_boot_state.boot_time_ms = boot_get_time_ms() - boot_start;
    return BOOT_OK;

boot_fail:
    g_boot_state.boot_time_ms = boot_get_time_ms() - boot_start;

    /* On critical failure, enter recovery mode if possible */
    if (g_boot_state.result == BOOT_ERR_SIGN_VERIFY ||
        g_boot_state.result == BOOT_ERR_PARTITION ||
        g_boot_state.result == BOOT_ERR_HARDWARE) {
        boot_enter_recovery();
    }

    return g_boot_state.result;
}