/*
 * rp2350b_init.h — Low-Level Hardware Initialization API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Provides initialization and GPIO control for the RP2350B coprocessor
 * on the GhostBlade board. Called once at boot before any peripheral
 * drivers are initialized.
 */

#ifndef RP2350B_INIT_H
#define RP2350B_INIT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * rp2350b_init — Initialize all low-level hardware
 *
 * Configures system clocks (150 MHz core, 48 MHz peripheral),
 * GPIO pin muxing (SPI0 slave, SPI1 master, SPI2 master, I2C1,
 * ADC, UART0), and enables the ARM Cortex-M33 FPU.
 *
 * Must be called before any other peripheral init.
 */
void rp2350b_init(void);

/**
 * rp2350b_gpio_set — Set a GPIO output pin high or low
 *
 * @pin:   RP2350B pin number (see board_pins.h)
 * @value: true = drive high, false = drive low
 */
void rp2350b_gpio_set(uint8_t pin, bool value);

/**
 * rp2350b_gpio_get — Read a GPIO input pin
 *
 * @pin:   RP2350B pin number (see board_pins.h)
 * Returns: true if pin is high, false if low
 */
bool rp2350b_gpio_get(uint8_t pin);

#endif /* RP2350B_INIT_H */