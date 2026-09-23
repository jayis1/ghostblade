/*
 * ghostwisp_gpio.h — GhostWisp GPIO Driver API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a high-level GPIO interface for the GhostWisp RP2350B.
 * Manages pin function selection, pull resistor configuration,
 * digital read/write, toggle, and interrupt (IRQ) configuration
 * for all 48 GPIO pins.
 *
 * The boot sequence (ghostwisp_boot.c) performs initial pin muxing;
 * this driver provides the runtime API for applications to read
 * buttons, drive feedback outputs, and react to pin interrupts.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 4 (GPIO/ADC)
 */

#ifndef GHOSTWISP_GPIO_H
#define GHOSTWISP_GPIO_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

/** Total GPIO pins on RP2350B */
#define GPIO_PIN_COUNT          48

/** Maximum debounce samples for button reading */
#define GPIO_DEBOUNCE_SAMPLES   5

/* ── GPIO function select values (5 bits in IO_BANK0 CTRL) ──────────────── */

typedef enum {
    GPIO_FUNC_SPI   = 1,    /**< SPI peripheral function */
    GPIO_FUNC_UART  = 2,    /**< UART peripheral function */
    GPIO_FUNC_I2C   = 3,    /**< I2C peripheral function */
    GPIO_FUNC_PWM   = 4,    /**< PWM peripheral function */
    GPIO_FUNC_SIO   = 5,    /**< Software I/O (direct GPIO control) */
    GPIO_FUNC_PIO0  = 6,    /**< PIO0 (programmable I/O state machine 0) */
    GPIO_FUNC_PIO1  = 7,    /**< PIO1 (programmable I/O state machine 1) */
    GPIO_FUNC_CLOCK = 8,    /**< Clock output */
    GPIO_FUNC_USB   = 9,    /**< USB peripheral function */
    GPIO_FUNC_NONE  = 31,   /**< Isolated (no function) */
} gpio_func_t;

/* ── GPIO direction ─────────────────────────────────────────────────────── */

typedef enum {
    GPIO_DIR_INPUT  = 0,    /**< Input (output driver disabled) */
    GPIO_DIR_OUTPUT = 1,    /**< Output (output driver enabled) */
} gpio_dir_t;

/* ── Pull resistor configuration ────────────────────────────────────────── */

typedef enum {
    GPIO_PULL_NONE   = 0,   /**< No pull resistor */
    GPIO_PULL_UP     = 1,   /**< Internal pull-up (≈56 kΩ) */
    GPIO_PULL_DOWN   = 2,   /**< Internal pull-down (≈56 kΩ) */
    GPIO_PULL_BUSKEEP = 3,  /**< Bus-keep (weak pull to current state) */
} gpio_pull_t;

/* ── Interrupt edge/level configuration ─────────────────────────────────── */

typedef enum {
    GPIO_IRQ_LEVEL_LOW  = 0x1,
    GPIO_IRQ_LEVEL_HIGH = 0x2,
    GPIO_IRQ_EDGE_FALL  = 0x4,
    GPIO_IRQ_EDGE_RISE  = 0x8,
} gpio_irq_event_t;

/* ── Button identifiers (mapped to physical pins) ───────────────────────── */

typedef enum {
    BTN_UP     = 0,
    BTN_DOWN   = 1,
    BTN_LEFT   = 2,
    BTN_RIGHT  = 3,
    BTN_CENTER = 4,
    BTN_A      = 5,
    BTN_B      = 6,
    BTN_COUNT  = 7,
} gpio_button_t;

/* ── Driver result codes ────────────────────────────────────────────────── */

typedef enum {
    GPIO_OK             = 0,
    GPIO_ERR_INVALID_PIN = -1,   /**< Pin number out of range (>= 48) */
    GPIO_ERR_INVALID_ARG = -2,   /**< Invalid argument value */
    GPIO_ERR_NOT_INIT    = -3,   /**< Driver not initialized */
} gpio_result_t;

/* ── Callback type for GPIO interrupts ──────────────────────────────────── */

typedef void (*gpio_irq_callback_t)(uint8_t pin, uint32_t events);

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * gpio_init — Initialize the GPIO driver
 *
 * Must be called after boot_phase_gpio() and before any other
 * gpio_* function. Resets internal state, clears callback table,
 * and sets up the SIO IRQ configuration.
 *
 * Returns GPIO_OK on success.
 */
gpio_result_t gpio_init(void);

/**
 * gpio_set_function — Set the function select for a GPIO pin
 *
 * @pin:  Pin number (0–47)
 * @func: Function to assign (GPIO_FUNC_SIO, GPIO_FUNC_SPI, etc.)
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_set_function(uint8_t pin, gpio_func_t func);

/**
 * gpio_get_function — Read the current function select for a pin
 *
 * @pin: Pin number (0–47)
 * Returns the gpio_func_t value, or GPIO_FUNC_NONE on invalid pin.
 */
gpio_func_t gpio_get_function(uint8_t pin);

/**
 * gpio_set_dir — Set pin direction (input or output) for SIO pins
 *
 * Only meaningful when the pin function is GPIO_FUNC_SIO.
 *
 * @pin:  Pin number
 * @dir:  GPIO_DIR_INPUT or GPIO_DIR_OUTPUT
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_set_dir(uint8_t pin, gpio_dir_t dir);

/**
 * gpio_get_dir — Get pin direction
 *
 * @pin: Pin number
 * Returns GPIO_DIR_INPUT, GPIO_DIR_OUTPUT, or -1 on invalid pin.
 */
int gpio_get_dir(uint8_t pin);

/**
 * gpio_set_pull — Configure pull resistor for a pin
 *
 * @pin:  Pin number
 * @pull: GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN, or GPIO_PULL_BUSKEEP
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_set_pull(uint8_t pin, gpio_pull_t pull);

/**
 * gpio_get_pull — Get current pull resistor configuration
 *
 * @pin: Pin number
 * Returns gpio_pull_t value, or GPIO_PULL_NONE on invalid pin.
 */
gpio_pull_t gpio_get_pull(uint8_t pin);

/**
 * gpio_put — Drive a SIO output pin high or low
 *
 * @pin:   Pin number
 * @value: true = high, false = low
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_put(uint8_t pin, bool value);

/**
 * gpio_get — Read the digital input level of a pin
 *
 * @pin: Pin number
 * Returns true (high) or false (low). Returns false on invalid pin.
 */
bool gpio_get(uint8_t pin);

/**
 * gpio_toggle — Toggle a SIO output pin
 *
 * @pin: Pin number
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_toggle(uint8_t pin);

/**
 * gpio_set_irq_enabled — Enable or disable interrupts on a pin
 *
 * @pin:    Pin number
 * @events: Bitmask of GPIO_IRQ_EDGE_FALL, GPIO_IRQ_EDGE_RISE, etc.
 * @enable: true to enable, false to disable
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_set_irq_enabled(uint8_t pin, uint32_t events, bool enable);

/**
 * gpio_set_irq_callback — Register a callback for GPIO interrupts
 *
 * The callback is invoked from the ISR context when a configured
 * interrupt fires. Pass NULL to unregister.
 *
 * @callback: Function pointer (pin, events) or NULL
 */
void gpio_set_irq_callback(gpio_irq_callback_t callback);

/**
 * gpio_acknowledge_irq — Acknowledge (clear) a pin's interrupt
 *
 * Must be called from the ISR handler to clear the interrupt
 * so it can fire again.
 *
 * @pin: Pin number
 * Returns GPIO_OK or GPIO_ERR_INVALID_PIN.
 */
gpio_result_t gpio_acknowledge_irq(uint8_t pin);

/**
 * gpio_handle_irq — Internal ISR dispatcher
 *
 * Called from the SIO IRQ handler. Checks all pins for pending
 * interrupts, invokes the callback, and acknowledges each.
 * Exposed for testing and for the real IRQ vector to call.
 */
void gpio_handle_irq(void);

/**
 * gpio_button_pressed — Read a button with debounce
 *
 * Samples the button pin GPIO_DEBOUNCE_SAMPLES times and returns
 * true only if all samples agree that the button is pressed
 * (active-low: pin reads false).
 *
 * @button: Button identifier (BTN_UP, BTN_DOWN, etc.)
 * Returns true if pressed, false if not pressed or invalid button.
 */
bool gpio_button_pressed(gpio_button_t button);

/**
 * gpio_button_raw — Read a button's raw pin state (no debounce)
 *
 * @button: Button identifier
 * Returns true if pressed (active-low pin reads low), false otherwise.
 */
bool gpio_button_raw(gpio_button_t button);

/**
 * gpio_button_pin — Get the physical pin number for a button
 *
 * @button: Button identifier
 * Returns pin number (0–47), or 0xFF on invalid button.
 */
uint8_t gpio_button_pin(gpio_button_t button);

/**
 * gpio_is_initialized — Check if the driver has been initialized
 *
 * Returns true if gpio_init() has been called successfully.
 */
bool gpio_is_initialized(void);

#endif /* GHOSTWISP_GPIO_H */