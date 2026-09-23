/*
 * ghostwisp_gpio.c — GhostWisp GPIO Driver Implementation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the GPIO driver for the GhostWisp RP2350B. Provides
 * runtime pin control for buttons, feedback outputs, and interrupt
 * handling. The boot sequence performs initial muxing; this driver
 * provides the application-facing API.
 *
 * Architecture:
 *   - SIO (Software I/O) for direct read/write via the SIO registers
 *   - IO_BANK0 for function select and interrupt configuration
 *   - PADS_BANK0 for pull resistor and drive strength configuration
 *   - Button reading uses debounce sampling
 *
 * Host test mode: When DRIVER_HOST_TEST is defined, all hardware
 * register access is replaced by in-memory arrays, enabling
 * unit testing on the host without any RP2350B hardware.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 4 (GPIO/ADC)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ghostwisp_gpio.h"
#include "ghostwisp_pins.h"

/* ======================================================================== */
/*  Host Test Mode                                                          */
/* ======================================================================== */

#ifdef DRIVER_HOST_TEST

/* ── Simulated GPIO state arrays ────────────────────────────────────────── */

static uint32_t  sim_func[GPIO_PIN_COUNT];      /* Function select per pin */
static uint8_t   sim_dir[GPIO_PIN_COUNT];        /* 0=input, 1=output */
static uint8_t   sim_pull[GPIO_PIN_COUNT];       /* gpio_pull_t per pin */
static uint8_t   sim_output_level[GPIO_PIN_COUNT]; /* Output level: 0 or 1 */
static uint8_t   sim_input_level[GPIO_PIN_COUNT];  /* Input level: 0 or 1 */
static uint32_t  sim_irq_enable[GPIO_PIN_COUNT]; /* Enabled IRQ events */
static uint32_t  sim_irq_status[GPIO_PIN_COUNT]; /* Pending IRQ events */
static bool      sim_initialized = false;
static gpio_irq_callback_t sim_irq_callback = NULL;

/* Test inspection functions (exposed via extern in tests) */
uint32_t gpio_test_func(uint8_t pin) { return (pin < GPIO_PIN_COUNT) ? sim_func[pin] : 0xFFFFFFFFU; }
uint8_t  gpio_test_dir(uint8_t pin) { return (pin < GPIO_PIN_COUNT) ? sim_dir[pin] : 0xFF; }
uint8_t  gpio_test_pull(uint8_t pin) { return (pin < GPIO_PIN_COUNT) ? sim_pull[pin] : 0xFF; }
bool     gpio_test_output_level(uint8_t pin) { return (pin < GPIO_PIN_COUNT) ? (sim_output_level[pin] != 0) : false; }
void     gpio_test_set_input_level(uint8_t pin, bool high) { if (pin < GPIO_PIN_COUNT) sim_input_level[pin] = high ? 1 : 0; }
uint32_t gpio_test_irq_enable(uint8_t pin) { return (pin < GPIO_PIN_COUNT) ? sim_irq_enable[pin] : 0; }
uint32_t gpio_test_irq_status(uint8_t pin) { return (pin < GPIO_PIN_COUNT) ? sim_irq_status[pin] : 0; }
void     gpio_test_trigger_irq(uint8_t pin, uint32_t events) {
    if (pin < GPIO_PIN_COUNT) sim_irq_status[pin] |= events;
}
gpio_irq_callback_t gpio_test_get_callback(void) { return sim_irq_callback; }
bool     gpio_test_is_init(void) { return sim_initialized; }
void     gpio_test_reset(void) {
    memset(sim_func, 0, sizeof(sim_func));
    memset(sim_dir, 0, sizeof(sim_dir));
    memset(sim_pull, 0, sizeof(sim_pull));
    memset(sim_output_level, 0, sizeof(sim_output_level));
    memset(sim_input_level, 0, sizeof(sim_input_level));
    memset(sim_irq_enable, 0, sizeof(sim_irq_enable));
    memset(sim_irq_status, 0, sizeof(sim_irq_status));
    sim_initialized = false;
    sim_irq_callback = NULL;
}

#else /* DRIVER_HOST_TEST — real hardware */

/* ======================================================================== */
/*  RP2350B Register Definitions                                            */
/* ======================================================================== */

#define RP2350B_IO_BANK0_BASE    0x400D0000UL
#define RP2350B_PADS_BASE        0x400C0000UL
#define SIO_BASE                 0xD0000000UL

/* SIO registers */
#define SIO_GPIO_IN              (*(volatile uint32_t *)(SIO_BASE + 0x004))
#define SIO_GPIO_OUT             (*(volatile uint32_t *)(SIO_BASE + 0x010))
#define SIO_GPIO_OUT_SET         (*(volatile uint32_t *)(SIO_BASE + 0x014))
#define SIO_GPIO_OUT_CLR         (*(volatile uint32_t *)(SIO_BASE + 0x018))
#define SIO_GPIO_OUT_XOR         (*(volatile uint32_t *)(SIO_BASE + 0x01C))
#define SIO_GPIO_OE              (*(volatile uint32_t *)(SIO_BASE + 0x020))
#define SIO_GPIO_OE_SET          (*(volatile uint32_t *)(SIO_BASE + 0x024))
#define SIO_GPIO_OE_CLR          (*(volatile uint32_t *)(SIO_BASE + 0x028))

/* IO_BANK0: each pin has CTRL at 0x04 + pin*8, and INTR at 0x00 + pin*8 */
#define IO_BANK0_CTRL(pin)       (*(volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x04 + (uint32_t)(pin) * 8))
#define IO_BANK0_INTR(pin)       (*(volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x00 + (uint32_t)(pin) * 8))

/* PADS_BANK0: each pin has a control register at 0x04 + pin*4 */
#define PADS_BANK0_CTRL(pin)     (*(volatile uint32_t *)(RP2350B_PADS_BASE + 0x04 + (uint32_t)(pin) * 4))

/* PADS bits */
#define PADS_OD                 (1U << 0)   /* Output disable */
#define PADS_PUE                (1U << 2)   /* Pull-up enable */
#define PADS_PDE                (1U << 3)   /* Pull-down enable */

/* PROC0_INTE — interrupt enable for processor 0 (at offset 0x100 in IO_BANK0) */
#define IO_BANK0_PROC0_INTE     (*(volatile uint32_t *)(RP2350B_IO_BANK0_BASE + 0x100))

static bool g_gpio_initialized = false;
static gpio_irq_callback_t g_irq_callback = NULL;

/* IRQ config storage (for runtime query) */
static uint32_t g_irq_enable[GPIO_PIN_COUNT];
static uint8_t  g_gpio_func[GPIO_PIN_COUNT];
static uint8_t  g_gpio_dir[GPIO_PIN_COUNT];
static uint8_t  g_gpio_pull[GPIO_PIN_COUNT];

#endif /* DRIVER_HOST_TEST */

/* ======================================================================== */
/*  Button pin mapping                                                      */
/* ======================================================================== */

static const uint8_t button_pins[BTN_COUNT] = {
    [BTN_UP]     = PIN_BTN_UP,
    [BTN_DOWN]   = PIN_BTN_DOWN,
    [BTN_LEFT]   = PIN_BTN_LEFT,
    [BTN_RIGHT]  = PIN_BTN_RIGHT,
    [BTN_CENTER] = PIN_BTN_CENTER,
    [BTN_A]      = PIN_BTN_A,
    [BTN_B]      = PIN_BTN_B,
};

/* ======================================================================== */
/*  Implementation                                                          */
/* ======================================================================== */

gpio_result_t gpio_init(void) {
#ifdef DRIVER_HOST_TEST
    gpio_test_reset();
    sim_initialized = true;
    return GPIO_OK;
#else
    memset(g_irq_enable, 0, sizeof(g_irq_enable));
    memset(g_gpio_func, 0, sizeof(g_gpio_func));
    memset(g_gpio_dir, 0, sizeof(g_gpio_dir));
    memset(g_gpio_pull, 0, sizeof(g_gpio_pull));
    g_gpio_initialized = true;
    g_irq_callback = NULL;

    /* Enable SIO IRQ in the NVIC (IRQ number 16 on RP2350B for IO_BANK0) */
    /* The actual NVIC enable is done in the boot sequence; here we just
     * ensure the callback is cleared. */
    return GPIO_OK;
#endif
}

gpio_result_t gpio_set_function(uint8_t pin, gpio_func_t func) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

#ifdef DRIVER_HOST_TEST
    sim_func[pin] = (uint32_t)func;
#else
    IO_BANK0_CTRL(pin) = (uint32_t)func & 0x1FU;
    __asm__ volatile ("dmb" ::: "memory");
    g_gpio_func[pin] = (uint8_t)func;
#endif

    return GPIO_OK;
}

gpio_func_t gpio_get_function(uint8_t pin) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_FUNC_NONE;

#ifdef DRIVER_HOST_TEST
    return (gpio_func_t)sim_func[pin];
#else
    return (gpio_func_t)(IO_BANK0_CTRL(pin) & 0x1FU);
#endif
}

gpio_result_t gpio_set_dir(uint8_t pin, gpio_dir_t dir) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

    /* Ensure pin is in SIO mode for direction to matter */
#ifdef DRIVER_HOST_TEST
    sim_dir[pin] = (uint8_t)dir;
    sim_func[pin] = GPIO_FUNC_SIO;
#else
    if (dir == GPIO_DIR_OUTPUT) {
        SIO_GPIO_OE_SET = (1U << pin);
        PADS_BANK0_CTRL(pin) &= ~PADS_OD;
    } else {
        SIO_GPIO_OE_CLR = (1U << pin);
        PADS_BANK0_CTRL(pin) |= PADS_OD;
    }
    __asm__ volatile ("dmb" ::: "memory");
    g_gpio_dir[pin] = (uint8_t)dir;
#endif

    return GPIO_OK;
}

int gpio_get_dir(uint8_t pin) {
    if (pin >= GPIO_PIN_COUNT)
        return -1;

#ifdef DRIVER_HOST_TEST
    return sim_dir[pin] ? GPIO_DIR_OUTPUT : GPIO_DIR_INPUT;
#else
    return (SIO_GPIO_OE & (1U << pin)) ? GPIO_DIR_OUTPUT : GPIO_DIR_INPUT;
#endif
}

gpio_result_t gpio_set_pull(uint8_t pin, gpio_pull_t pull) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

#ifdef DRIVER_HOST_TEST
    sim_pull[pin] = (uint8_t)pull;
#else
    uint32_t pad = PADS_BANK0_CTRL(pin);
    pad &= ~(PADS_PUE | PADS_PDE);
    switch (pull) {
        case GPIO_PULL_UP:
            pad |= PADS_PUE;
            break;
        case GPIO_PULL_DOWN:
            pad |= PADS_PDE;
            break;
        case GPIO_PULL_BUSKEEP:
            /* RP2350B doesn't have explicit bus-keep; use both PU and PD */
            pad |= (PADS_PUE | PADS_PDE);
            break;
        case GPIO_PULL_NONE:
        default:
            break;
    }
    PADS_BANK0_CTRL(pin) = pad;
    __asm__ volatile ("dmb" ::: "memory");
    g_gpio_pull[pin] = (uint8_t)pull;
#endif

    return GPIO_OK;
}

gpio_pull_t gpio_get_pull(uint8_t pin) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_PULL_NONE;

#ifdef DRIVER_HOST_TEST
    return (gpio_pull_t)sim_pull[pin];
#else
    uint32_t pad = PADS_BANK0_CTRL(pin);
    bool pue = (pad & PADS_PUE) != 0;
    bool pde = (pad & PADS_PDE) != 0;
    if (pue && pde) return GPIO_PULL_BUSKEEP;
    if (pue) return GPIO_PULL_UP;
    if (pde) return GPIO_PULL_DOWN;
    return GPIO_PULL_NONE;
#endif
}

gpio_result_t gpio_put(uint8_t pin, bool value) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

#ifdef DRIVER_HOST_TEST
    sim_output_level[pin] = value ? 1 : 0;
#else
    if (value)
        SIO_GPIO_OUT_SET = (1U << pin);
    else
        SIO_GPIO_OUT_CLR = (1U << pin);
    __asm__ volatile ("dmb" ::: "memory");
#endif

    return GPIO_OK;
}

bool gpio_get(uint8_t pin) {
    if (pin >= GPIO_PIN_COUNT)
        return false;

#ifdef DRIVER_HOST_TEST
    /* If pin is configured as output, return output level;
     * if input, return simulated input level */
    if (sim_dir[pin] == 1)
        return sim_output_level[pin] != 0;
    else
        return sim_input_level[pin] != 0;
#else
    return (SIO_GPIO_IN & (1U << pin)) != 0;
#endif
}

gpio_result_t gpio_toggle(uint8_t pin) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

#ifdef DRIVER_HOST_TEST
    sim_output_level[pin] ^= 1;
#else
    SIO_GPIO_OUT_XOR = (1U << pin);
    __asm__ volatile ("dmb" ::: "memory");
#endif

    return GPIO_OK;
}

gpio_result_t gpio_set_irq_enabled(uint8_t pin, uint32_t events, bool enable) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

    /* events must use only the defined bits */
    uint32_t valid_events = GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH |
                            GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
    if (events & ~valid_events)
        return GPIO_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    if (enable)
        sim_irq_enable[pin] |= events;
    else
        sim_irq_enable[pin] &= ~events;
#else
    /* Configure the INT edge/level in IO_BANK0 INTR register.
     * The INTR register has 4 bits per pin: level-low, level-high,
     * edge-fall, edge-rise (bits 0–3 of each pin's INTR). */
    uint32_t cfg = IO_BANK0_INTR(pin);
    if (enable) {
        cfg |= events;
    } else {
        cfg &= ~events;
    }
    IO_BANK0_INTR(pin) = cfg;
    g_irq_enable[pin] = enable ? (g_irq_enable[pin] | events) : (g_irq_enable[pin] & ~events);
    __asm__ volatile ("dmb" ::: "memory");
#endif

    return GPIO_OK;
}

void gpio_set_irq_callback(gpio_irq_callback_t callback) {
#ifdef DRIVER_HOST_TEST
    sim_irq_callback = callback;
#else
    g_irq_callback = callback;
#endif
}

gpio_result_t gpio_acknowledge_irq(uint8_t pin) {
    if (pin >= GPIO_PIN_COUNT)
        return GPIO_ERR_INVALID_PIN;

#ifdef DRIVER_HOST_TEST
    sim_irq_status[pin] = 0;
#else
    /* Write 1 to the edge-detect bits to clear them in IO_BANK0 INTR */
    IO_BANK0_INTR(pin) = GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
    __asm__ volatile ("dmb" ::: "memory");
#endif

    return GPIO_OK;
}

void gpio_handle_irq(void) {
    for (uint8_t pin = 0; pin < GPIO_PIN_COUNT; pin++) {
#ifdef DRIVER_HOST_TEST
        uint32_t status = sim_irq_status[pin];
        uint32_t enabled = sim_irq_enable[pin];
#else
        uint32_t status = IO_BANK0_INTR(pin);
        uint32_t enabled = g_irq_enable[pin];
#endif
        uint32_t pending = status & enabled;
        if (pending) {
            /* Invoke callback if registered */
#ifdef DRIVER_HOST_TEST
            if (sim_irq_callback)
                sim_irq_callback(pin, pending);
#else
            if (g_irq_callback)
                g_irq_callback(pin, pending);
#endif
            /* Acknowledge edge interrupts (level stays until pin changes) */
            gpio_acknowledge_irq(pin);
        }
    }
}

bool gpio_button_pressed(gpio_button_t button) {
    if (button >= BTN_COUNT)
        return false;

    uint8_t pin = button_pins[button];

    /* Active-low: pressed = pin reads low (false) */
    for (int i = 0; i < GPIO_DEBOUNCE_SAMPLES; i++) {
        if (gpio_get(pin))
            return false;  /* Not pressed (pin is high) */
    }
    return true;  /* All samples agree: pressed (pin is low) */
}

bool gpio_button_raw(gpio_button_t button) {
    if (button >= BTN_COUNT)
        return false;

    uint8_t pin = button_pins[button];
    return !gpio_get(pin);  /* Active-low: pressed when pin is low */
}

uint8_t gpio_button_pin(gpio_button_t button) {
    if (button >= BTN_COUNT)
        return 0xFF;
    return button_pins[button];
}

bool gpio_is_initialized(void) {
#ifdef DRIVER_HOST_TEST
    return sim_initialized;
#else
    return g_gpio_initialized;
#endif
}