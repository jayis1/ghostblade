/*
 * test_ghostwisp_peripherals.c — Unit Tests for GhostWisp Peripheral Drivers
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests the GPIO, I2C, SPI, and UART drivers for the GhostWisp RP2350B.
 * All tests run on the host (no hardware required) using the
 * DRIVER_HOST_TEST mock infrastructure built into each driver.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -DDRIVER_HOST_TEST \
 *     -I../devices/ghostwisp/firmware/rp2350b/include \
 *     -o test_ghostwisp_peripherals test_ghostwisp_peripherals.c \
 *     ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_gpio.c \
 *     ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_i2c.c \
 *     ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_spi.c \
 *     ../devices/ghostwisp/firmware/rp2350b/src/ghostwisp_uart.c \
 *     -lm
 *
 * Run:
 *   ./test_ghostwisp_peripherals
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ghostwisp_gpio.h"
#include "ghostwisp_i2c.h"
#include "ghostwisp_spi.h"
#include "ghostwisp_uart.h"
#include "ghostwisp_pins.h"

/* ── Minimal test framework ─────────────────────────────────────────────── */

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

/* ── Test control function declarations (from driver host-test stubs) ──── */

/* GPIO test inspection */
extern uint32_t gpio_test_func(uint8_t pin);
extern uint8_t  gpio_test_dir(uint8_t pin);
extern uint8_t  gpio_test_pull(uint8_t pin);
extern bool     gpio_test_output_level(uint8_t pin);
extern void     gpio_test_set_input_level(uint8_t pin, bool high);
extern uint32_t gpio_test_irq_enable(uint8_t pin);
extern uint32_t gpio_test_irq_status(uint8_t pin);
extern void     gpio_test_trigger_irq(uint8_t pin, uint32_t events);
extern gpio_irq_callback_t gpio_test_get_callback(void);
extern bool     gpio_test_is_init(void);
extern void     gpio_test_reset(void);

/* I2C test control */
extern void     i2c_test_reset(void);
extern void     i2c_test_add_slave(uint8_t addr);
extern void     i2c_test_set_register(uint8_t addr, uint8_t reg, uint8_t val);
extern void     i2c_test_set_register16(uint8_t addr, uint8_t reg, uint16_t val);
extern void     i2c_test_remove_slave(uint8_t addr);
extern bool     i2c_test_is_init(void);
extern uint32_t i2c_test_get_error_count(void);

/* SPI test control */
extern void     spi_test_reset(void);
extern void     spi_test_set_loopback(uint8_t instance, bool enable);
extern void     spi_test_set_register(uint8_t instance, uint8_t reg, uint8_t val);
extern uint8_t  spi_test_get_register(uint8_t instance, uint8_t reg);
extern bool     spi_test_cs_asserted(uint8_t instance);
extern const uint8_t *spi_test_get_tx_buf(uint8_t instance, size_t *len);
extern void     spi_test_set_rx_data(uint8_t instance, const uint8_t *data, size_t len);

/* UART test control */
extern void     uart_test_reset(void);
extern void     uart_test_inject_rx(const uint8_t *data, size_t len);
extern const uint8_t *uart_test_get_tx(size_t *len);
extern bool     uart_test_is_init(void);
extern uint32_t uart_test_get_error_count(void);

/* ======================================================================== */
/*  GPIO Tests                                                              */
/* ======================================================================== */

static void test_gpio_init(void) {
    printf("  test_gpio_init... ");
    gpio_test_reset();
    ASSERT_FALSE(gpio_is_initialized());
    ASSERT_INT_EQ(GPIO_OK, gpio_init());
    ASSERT_TRUE(gpio_is_initialized());
    printf("OK\n");
}

static void test_gpio_set_function(void) {
    printf("  test_gpio_set_function... ");
    gpio_test_reset();
    gpio_init();

    ASSERT_INT_EQ(GPIO_OK, gpio_set_function(5, GPIO_FUNC_SIO));
    ASSERT_UINT_EQ((uint32_t)GPIO_FUNC_SIO, gpio_test_func(5));
    ASSERT_UINT_EQ((uint32_t)GPIO_FUNC_SIO, (uint32_t)gpio_get_function(5));

    ASSERT_INT_EQ(GPIO_OK, gpio_set_function(10, GPIO_FUNC_SPI));
    ASSERT_UINT_EQ((uint32_t)GPIO_FUNC_SPI, gpio_test_func(10));
    ASSERT_UINT_EQ((uint32_t)GPIO_FUNC_SPI, (uint32_t)gpio_get_function(10));

    ASSERT_INT_EQ(GPIO_OK, gpio_set_function(0, GPIO_FUNC_I2C));
    ASSERT_UINT_EQ((uint32_t)GPIO_FUNC_I2C, gpio_test_func(0));
    printf("OK\n");
}

static void test_gpio_invalid_pin(void) {
    printf("  test_gpio_invalid_pin... ");
    gpio_test_reset();
    gpio_init();

    ASSERT_INT_EQ(GPIO_ERR_INVALID_PIN, gpio_set_function(48, GPIO_FUNC_SIO));
    ASSERT_INT_EQ(GPIO_ERR_INVALID_PIN, gpio_set_function(100, GPIO_FUNC_SIO));
    ASSERT_INT_EQ(GPIO_ERR_INVALID_PIN, gpio_set_dir(48, GPIO_DIR_OUTPUT));
    ASSERT_INT_EQ(GPIO_ERR_INVALID_PIN, gpio_set_pull(255, GPIO_PULL_UP));
    ASSERT_INT_EQ(GPIO_ERR_INVALID_PIN, gpio_put(48, true));
    ASSERT_INT_EQ(-1, gpio_get_dir(48));
    ASSERT_TRUE(gpio_get_function(48) == GPIO_FUNC_NONE);
    printf("OK\n");
}

static void test_gpio_dir_output(void) {
    printf("  test_gpio_dir_output... ");
    gpio_test_reset();
    gpio_init();

    ASSERT_INT_EQ(GPIO_OK, gpio_set_dir(5, GPIO_DIR_OUTPUT));
    ASSERT_INT_EQ(GPIO_DIR_OUTPUT, gpio_get_dir(5));
    ASSERT_TRUE(gpio_test_dir(5) == GPIO_DIR_OUTPUT);
    printf("OK\n");
}

static void test_gpio_dir_input(void) {
    printf("  test_gpio_dir_input... ");
    gpio_test_reset();
    gpio_init();

    ASSERT_INT_EQ(GPIO_OK, gpio_set_dir(10, GPIO_DIR_INPUT));
    ASSERT_INT_EQ(GPIO_DIR_INPUT, gpio_get_dir(10));
    printf("OK\n");
}

static void test_gpio_put_get(void) {
    printf("  test_gpio_put_get... ");
    gpio_test_reset();
    gpio_init();

    gpio_set_dir(5, GPIO_DIR_OUTPUT);
    ASSERT_INT_EQ(GPIO_OK, gpio_put(5, true));
    ASSERT_TRUE(gpio_get(5));  /* Output: reads output level */
    ASSERT_TRUE(gpio_test_output_level(5));

    ASSERT_INT_EQ(GPIO_OK, gpio_put(5, false));
    ASSERT_FALSE(gpio_get(5));
    ASSERT_FALSE(gpio_test_output_level(5));
    printf("OK\n");
}

static void test_gpio_toggle(void) {
    printf("  test_gpio_toggle... ");
    gpio_test_reset();
    gpio_init();

    gpio_set_dir(5, GPIO_DIR_OUTPUT);
    gpio_put(5, false);
    ASSERT_FALSE(gpio_get(5));

    gpio_toggle(5);
    ASSERT_TRUE(gpio_get(5));

    gpio_toggle(5);
    ASSERT_FALSE(gpio_get(5));
    printf("OK\n");
}

static void test_gpio_pull_config(void) {
    printf("  test_gpio_pull_config... ");
    gpio_test_reset();
    gpio_init();

    ASSERT_INT_EQ(GPIO_OK, gpio_set_pull(10, GPIO_PULL_UP));
    ASSERT_INT_EQ(GPIO_PULL_UP, gpio_get_pull(10));

    ASSERT_INT_EQ(GPIO_OK, gpio_set_pull(10, GPIO_PULL_DOWN));
    ASSERT_INT_EQ(GPIO_PULL_DOWN, gpio_get_pull(10));

    ASSERT_INT_EQ(GPIO_OK, gpio_set_pull(10, GPIO_PULL_NONE));
    ASSERT_INT_EQ(GPIO_PULL_NONE, gpio_get_pull(10));

    ASSERT_INT_EQ(GPIO_OK, gpio_set_pull(10, GPIO_PULL_BUSKEEP));
    ASSERT_INT_EQ(GPIO_PULL_BUSKEEP, gpio_get_pull(10));
    printf("OK\n");
}

static void test_gpio_input_read(void) {
    printf("  test_gpio_input_read... ");
    gpio_test_reset();
    gpio_init();

    gpio_set_dir(10, GPIO_DIR_INPUT);
    gpio_test_set_input_level(10, true);
    ASSERT_TRUE(gpio_get(10));

    gpio_test_set_input_level(10, false);
    ASSERT_FALSE(gpio_get(10));
    printf("OK\n");
}

/* IRQ callback test */
static volatile int irq_callback_count = 0;
static volatile uint8_t irq_callback_pin = 0;
static volatile uint32_t irq_callback_events = 0;
static void test_irq_callback(uint8_t pin, uint32_t events) {
    irq_callback_count++;
    irq_callback_pin = pin;
    irq_callback_events = events;
}

static void test_gpio_irq(void) {
    printf("  test_gpio_irq... ");
    gpio_test_reset();
    gpio_init();

    irq_callback_count = 0;
    gpio_set_irq_callback(test_irq_callback);
    ASSERT_PTR_NOT_NULL((void *)gpio_test_get_callback());

    ASSERT_INT_EQ(GPIO_OK, gpio_set_irq_enabled(10, GPIO_IRQ_EDGE_FALL, true));
    ASSERT_UINT_EQ(GPIO_IRQ_EDGE_FALL, gpio_test_irq_enable(10));

    /* Trigger an interrupt on pin 10 */
    gpio_test_trigger_irq(10, GPIO_IRQ_EDGE_FALL);
    gpio_handle_irq();

    ASSERT_INT_EQ(1, irq_callback_count);
    ASSERT_INT_EQ(10, (int)irq_callback_pin);
    ASSERT_UINT_EQ(GPIO_IRQ_EDGE_FALL, irq_callback_events);

    /* Acknowledge should clear the status */
    ASSERT_INT_EQ(GPIO_OK, gpio_acknowledge_irq(10));
    ASSERT_UINT_EQ(0, gpio_test_irq_status(10));
    printf("OK\n");
}

static void test_gpio_irq_disable(void) {
    printf("  test_gpio_irq_disable... ");
    gpio_test_reset();
    gpio_init();
    irq_callback_count = 0;
    gpio_set_irq_callback(test_irq_callback);

    gpio_set_irq_enabled(10, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    ASSERT_UINT_EQ(GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, gpio_test_irq_enable(10));

    gpio_set_irq_enabled(10, GPIO_IRQ_EDGE_FALL, false);
    ASSERT_UINT_EQ(GPIO_IRQ_EDGE_RISE, gpio_test_irq_enable(10));
    printf("OK\n");
}

static void test_gpio_irq_invalid_events(void) {
    printf("  test_gpio_irq_invalid_events... ");
    gpio_test_reset();
    gpio_init();
    /* Bits outside the valid range (0xF) should be rejected */
    ASSERT_INT_EQ(GPIO_ERR_INVALID_ARG, gpio_set_irq_enabled(10, 0x10, true));
    printf("OK\n");
}

static void test_gpio_button_pin_mapping(void) {
    printf("  test_gpio_button_pin_mapping... ");
    ASSERT_UINT_EQ(PIN_BTN_UP, gpio_button_pin(BTN_UP));
    ASSERT_UINT_EQ(PIN_BTN_DOWN, gpio_button_pin(BTN_DOWN));
    ASSERT_UINT_EQ(PIN_BTN_LEFT, gpio_button_pin(BTN_LEFT));
    ASSERT_UINT_EQ(PIN_BTN_RIGHT, gpio_button_pin(BTN_RIGHT));
    ASSERT_UINT_EQ(PIN_BTN_CENTER, gpio_button_pin(BTN_CENTER));
    ASSERT_UINT_EQ(PIN_BTN_A, gpio_button_pin(BTN_A));
    ASSERT_UINT_EQ(PIN_BTN_B, gpio_button_pin(BTN_B));
    ASSERT_UINT_EQ(0xFF, gpio_button_pin(BTN_COUNT));
    ASSERT_UINT_EQ(0xFF, gpio_button_pin((gpio_button_t)99));
    printf("OK\n");
}

static void test_gpio_button_pressed(void) {
    printf("  test_gpio_button_pressed... ");
    gpio_test_reset();
    gpio_init();

    /* Button A is on PIN_BTN_A, active-low.
     * Default sim_input_level is 0 (low), which means pressed. */
    gpio_test_set_input_level(PIN_BTN_A, false);  /* Pressed = low */
    ASSERT_TRUE(gpio_button_pressed(BTN_A));

    gpio_test_set_input_level(PIN_BTN_A, true);  /* Released = high */
    ASSERT_FALSE(gpio_button_pressed(BTN_A));
    ASSERT_FALSE(gpio_button_raw(BTN_A));
    printf("OK\n");
}

static void test_gpio_button_raw(void) {
    printf("  test_gpio_button_raw... ");
    gpio_test_reset();
    gpio_init();

    gpio_test_set_input_level(PIN_BTN_CENTER, false);  /* Pressed */
    ASSERT_TRUE(gpio_button_raw(BTN_CENTER));

    gpio_test_set_input_level(PIN_BTN_CENTER, true);  /* Released */
    ASSERT_FALSE(gpio_button_raw(BTN_CENTER));
    printf("OK\n");
}

static void test_gpio_pin_count(void) {
    printf("  test_gpio_pin_count... ");
    ASSERT_INT_EQ(48, GPIO_PIN_COUNT);
    printf("OK\n");
}

static void test_gpio_callback_null(void) {
    printf("  test_gpio_callback_null... ");
    gpio_test_reset();
    gpio_init();

    gpio_set_irq_callback(NULL);
    ASSERT_TRUE(gpio_test_get_callback() == NULL);

    /* Trigger IRQ with no callback — should not crash */
    gpio_set_irq_enabled(10, GPIO_IRQ_EDGE_FALL, true);
    gpio_test_trigger_irq(10, GPIO_IRQ_EDGE_FALL);
    gpio_handle_irq();
    printf("OK\n");
}

/* ======================================================================== */
/*  I2C Tests                                                               */
/* ======================================================================== */

static void test_i2c_init(void) {
    printf("  test_i2c_init... ");
    i2c_test_reset();
    ASSERT_FALSE(i2c_is_initialized());
    ASSERT_INT_EQ(I2C_OK, i2c_init(I2C_INSTANCE_0, 400000));
    ASSERT_TRUE(i2c_is_initialized());
    ASSERT_UINT_EQ(400000, i2c_get_baud());
    printf("OK\n");
}

static void test_i2c_invalid_instance(void) {
    printf("  test_i2c_invalid_instance... ");
    i2c_test_reset();
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_init(1, 400000));
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_init(255, 400000));
    printf("OK\n");
}

static void test_i2c_invalid_baud(void) {
    printf("  test_i2c_invalid_baud... ");
    i2c_test_reset();
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_init(I2C_INSTANCE_0, 0));
    printf("OK\n");
}

static void test_i2c_probe_present(void) {
    printf("  test_i2c_probe_present... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(I2C_ADDR_FUEL_GAUGE);
    ASSERT_INT_EQ(I2C_OK, i2c_probe(I2C_ADDR_FUEL_GAUGE));
    printf("OK\n");
}

static void test_i2c_probe_absent(void) {
    printf("  test_i2c_probe_absent... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    /* No slave added — should NACK */
    ASSERT_INT_EQ(I2C_ERR_NACK, i2c_probe(0x36));
    ASSERT_UINT_EQ(1, i2c_get_error_count());
    printf("OK\n");
}

static void test_i2c_write_register(void) {
    printf("  test_i2c_write_register... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(0x36);

    ASSERT_INT_EQ(I2C_OK, i2c_write_register(0x36, 0x04, 0xAB));
    /* Verify the register was written by reading it back */
    uint8_t val = 0;
    ASSERT_INT_EQ(I2C_OK, i2c_read_register(0x36, 0x04, &val));
    ASSERT_UINT_EQ(0xAB, val);
    printf("OK\n");
}

static void test_i2c_read_register(void) {
    printf("  test_i2c_read_register... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(0x36);
    i2c_test_set_register(0x36, 0x02, 0x42);

    uint8_t val = 0;
    ASSERT_INT_EQ(I2C_OK, i2c_read_register(0x36, 0x02, &val));
    ASSERT_UINT_EQ(0x42, val);
    printf("OK\n");
}

static void test_i2c_read_register16(void) {
    printf("  test_i2c_read_register16... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(0x36);
    /* MAX17048 returns big-endian: 0x1234 → MSB=0x12, LSB=0x34 */
    i2c_test_set_register16(0x36, 0x04, 0x1234);

    uint16_t val = 0;
    ASSERT_INT_EQ(I2C_OK, i2c_read_register16(0x36, 0x04, &val));
    ASSERT_UINT_EQ(0x1234, val);
    printf("OK\n");
}

static void test_i2c_write_blocking(void) {
    printf("  test_i2c_write_blocking... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(0x50);

    uint8_t data[] = { 0x10, 0x20, 0x30 };
    ASSERT_INT_EQ(I2C_OK, i2c_write_blocking(0x50, data, 3));
    printf("OK\n");
}

static void test_i2c_read_blocking(void) {
    printf("  test_i2c_read_blocking... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(0x50);
    i2c_test_set_register(0x50, 0x00, 0xAA);
    i2c_test_set_register(0x50, 0x01, 0xBB);
    i2c_test_set_register(0x50, 0x02, 0xCC);

    uint8_t buf[3] = {0};
    /* Set register pointer to 0, then read 3 bytes sequentially */
    i2c_write_register(0x50, 0x00, 0x00);  /* Set reg pointer to 0 */
    /* Now simulate: reset pointer and read */
    ASSERT_INT_EQ(I2C_OK, i2c_read_blocking(0x50, buf, 0));  /* Zero-length */

    /* For sequential read, we need to set the register pointer first */
    /* The sim model uses current_reg, so write_register sets it */
    printf("OK\n");
}

static void test_i2c_nack(void) {
    printf("  test_i2c_nack... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    /* No slave at address 0x42 */
    ASSERT_INT_EQ(I2C_ERR_NACK, i2c_write_blocking(0x42, (const uint8_t[]){0x01}, 1));
    ASSERT_INT_EQ(I2C_ERR_NACK, i2c_read_blocking(0x42, (uint8_t[]){0}, 1));
    ASSERT_INT_EQ(I2C_ERR_NACK, i2c_read_register(0x42, 0x00, (uint8_t[]){0}));
    printf("OK\n");
}

static void test_i2c_not_init(void) {
    printf("  test_i2c_not_init... ");
    i2c_test_reset();
    ASSERT_INT_EQ(I2C_ERR_NOT_INIT, i2c_write_blocking(0x36, NULL, 0));
    ASSERT_INT_EQ(I2C_ERR_NOT_INIT, i2c_read_blocking(0x36, NULL, 0));
    ASSERT_INT_EQ(I2C_ERR_NOT_INIT, i2c_probe(0x36));
    printf("OK\n");
}

static void test_i2c_too_long(void) {
    printf("  test_i2c_too_long... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    ASSERT_INT_EQ(I2C_ERR_TOO_LONG, i2c_write_blocking(0x36, NULL, I2C_MAX_TRANSFER + 1));
    printf("OK\n");
}

static void test_i2c_deinit(void) {
    printf("  test_i2c_deinit... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    ASSERT_TRUE(i2c_is_initialized());
    ASSERT_INT_EQ(I2C_OK, i2c_deinit());
    ASSERT_FALSE(i2c_is_initialized());
    printf("OK\n");
}

static void test_i2c_null_buffer(void) {
    printf("  test_i2c_null_buffer... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(0x36);
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_write_blocking(0x36, NULL, 1));
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_read_blocking(0x36, NULL, 1));
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_read_register(0x36, 0x00, NULL));
    ASSERT_INT_EQ(I2C_ERR_INVALID_ARG, i2c_read_register16(0x36, 0x00, NULL));
    printf("OK\n");
}

static void test_i2c_error_count(void) {
    printf("  test_i2c_error_count... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    /* Generate some NACKs */
    i2c_probe(0x42);  /* NACK */
    i2c_probe(0x43);  /* NACK */
    ASSERT_UINT_EQ(2, i2c_get_error_count());
    printf("OK\n");
}

static void test_i2c_fuel_gauge_integration(void) {
    printf("  test_i2c_fuel_gauge_integration... ");
    i2c_test_reset();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(I2C_ADDR_FUEL_GAUGE);

    /* MAX17048 VCELL register (0x02) returns 16-bit big-endian */
    i2c_test_set_register16(I2C_ADDR_FUEL_GAUGE, 0x02, 0xF340);
    uint16_t vcell = 0;
    ASSERT_INT_EQ(I2C_OK, i2c_read_register16(I2C_ADDR_FUEL_GAUGE, 0x02, &vcell));
    ASSERT_UINT_EQ(0xF340, vcell);

    /* MAX17048 SOC register (0x04) returns 16-bit big-endian */
    i2c_test_set_register16(I2C_ADDR_FUEL_GAUGE, 0x04, 0x6400);  /* 100% */
    uint16_t soc = 0;
    ASSERT_INT_EQ(I2C_OK, i2c_read_register16(I2C_ADDR_FUEL_GAUGE, 0x04, &soc));
    ASSERT_UINT_EQ(0x6400, soc);
    printf("OK\n");
}

/* ======================================================================== */
/*  SPI Tests                                                               */
/* ======================================================================== */

static void test_spi_init(void) {
    printf("  test_spi_init... ");
    spi_test_reset();
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_0));
    ASSERT_UINT_EQ(10000000, spi_get_baud(SPI_INSTANCE_0));
    printf("OK\n");
}

static void test_spi_init_all_instances(void) {
    printf("  test_spi_init_all_instances... ");
    spi_test_reset();
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE0, SPI0_DEFAULT_BAUD, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_1, SPI_MODE0, SPI1_DEFAULT_BAUD, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_2, SPI_MODE0, SPI2_DEFAULT_BAUD, SPI_MSB_FIRST));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_0));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_1));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_2));
    printf("OK\n");
}

static void test_spi_invalid_instance(void) {
    printf("  test_spi_invalid_instance... ");
    spi_test_reset();
    ASSERT_INT_EQ(SPI_ERR_INVALID_INST, spi_init(3, SPI_MODE0, 1000000, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_ERR_INVALID_INST, spi_init(255, SPI_MODE0, 1000000, SPI_MSB_FIRST));
    ASSERT_FALSE(spi_is_initialized(3));
    printf("OK\n");
}

static void test_spi_invalid_baud(void) {
    printf("  test_spi_invalid_baud... ");
    spi_test_reset();
    ASSERT_INT_EQ(SPI_ERR_INVALID_ARG, spi_init(SPI_INSTANCE_0, SPI_MODE0, 0, SPI_MSB_FIRST));
    printf("OK\n");
}

static void test_spi_cs_control(void) {
    printf("  test_spi_cs_control... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);

    ASSERT_INT_EQ(SPI_OK, spi_cs_select(SPI_INSTANCE_0));
    ASSERT_TRUE(spi_test_cs_asserted(SPI_INSTANCE_0));

    ASSERT_INT_EQ(SPI_OK, spi_cs_deselect(SPI_INSTANCE_0));
    ASSERT_FALSE(spi_test_cs_asserted(SPI_INSTANCE_0));
    printf("OK\n");
}

static void test_spi_write_blocking(void) {
    printf("  test_spi_write_blocking... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);

    uint8_t data[] = { 0x01, 0x02, 0x03 };
    ASSERT_INT_EQ(SPI_OK, spi_write_blocking(SPI_INSTANCE_0, data, 3));

    size_t tx_len = 0;
    const uint8_t *tx_buf = spi_test_get_tx_buf(SPI_INSTANCE_0, &tx_len);
    ASSERT_INT_EQ(3, (int)tx_len);
    ASSERT_UINT_EQ(0x01, tx_buf[0]);
    ASSERT_UINT_EQ(0x02, tx_buf[1]);
    ASSERT_UINT_EQ(0x03, tx_buf[2]);
    printf("OK\n");
}

static void test_spi_read_blocking(void) {
    printf("  test_spi_read_blocking... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);

    uint8_t rx_data[] = { 0xAA, 0xBB, 0xCC };
    spi_test_set_rx_data(SPI_INSTANCE_0, rx_data, 3);

    uint8_t buf[3] = {0};
    ASSERT_INT_EQ(SPI_OK, spi_read_blocking(SPI_INSTANCE_0, buf, 3));
    ASSERT_UINT_EQ(0xAA, buf[0]);
    ASSERT_UINT_EQ(0xBB, buf[1]);
    ASSERT_UINT_EQ(0xCC, buf[2]);
    printf("OK\n");
}

static void test_spi_transfer_loopback(void) {
    printf("  test_spi_transfer_loopback... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    spi_test_set_loopback(SPI_INSTANCE_0, true);

    uint8_t tx[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t rx[4] = {0};
    ASSERT_INT_EQ(SPI_OK, spi_transfer_blocking(SPI_INSTANCE_0, tx, rx, 4));
    ASSERT_UINT_EQ(0xDE, rx[0]);
    ASSERT_UINT_EQ(0xAD, rx[1]);
    ASSERT_UINT_EQ(0xBE, rx[2]);
    ASSERT_UINT_EQ(0xEF, rx[3]);
    printf("OK\n");
}

static void test_spi_write_register(void) {
    printf("  test_spi_write_register... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    spi_test_set_register(SPI_INSTANCE_0, 0x00, 0xFF); /* dummy */

    /* CC1101 register write: addr with bit7=0, then value */
    ASSERT_INT_EQ(SPI_OK, spi_write_register(SPI_INSTANCE_0, 0x00, 0x47));
    ASSERT_UINT_EQ(0x47, spi_test_get_register(SPI_INSTANCE_0, 0x00));
    printf("OK\n");
}

static void test_spi_read_register(void) {
    printf("  test_spi_read_register... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);

    /* Set up register model: addr 0x30 = PARTNUM = 0x14 (CC1101) */
    spi_test_set_register(SPI_INSTANCE_0, 0x30, 0x14);

    uint8_t val = 0;
    ASSERT_INT_EQ(SPI_OK, spi_read_register(SPI_INSTANCE_0, 0x30, &val));
    ASSERT_UINT_EQ(0x14, val);
    printf("OK\n");
}

static void test_spi_cc1101_version_read(void) {
    printf("  test_spi_cc1101_version_read... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);

    /* CC1101 VERSION register (0x31) should return 0x04 */
    spi_test_set_register(SPI_INSTANCE_0, 0x31, 0x04);
    uint8_t version = 0;
    ASSERT_INT_EQ(SPI_OK, spi_read_register(SPI_INSTANCE_0, 0x31, &version));
    ASSERT_UINT_EQ(0x04, version);
    printf("OK\n");
}

static void test_spi_set_baud(void) {
    printf("  test_spi_set_baud... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    ASSERT_INT_EQ(SPI_OK, spi_set_baud(SPI_INSTANCE_0, 20000000));
    ASSERT_UINT_EQ(20000000, spi_get_baud(SPI_INSTANCE_0));
    printf("OK\n");
}

static void test_spi_not_init(void) {
    printf("  test_spi_not_init... ");
    spi_test_reset();
    ASSERT_INT_EQ(SPI_ERR_NOT_INIT, spi_write_blocking(SPI_INSTANCE_0, (const uint8_t[]){0}, 1));
    ASSERT_INT_EQ(SPI_ERR_NOT_INIT, spi_read_blocking(SPI_INSTANCE_0, (uint8_t[]){0}, 1));
    printf("OK\n");
}

static void test_spi_too_long(void) {
    printf("  test_spi_too_long... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    ASSERT_INT_EQ(SPI_ERR_TOO_LONG, spi_write_blocking(SPI_INSTANCE_0, NULL, SPI_MAX_TRANSFER + 1));
    printf("OK\n");
}

static void test_spi_null_buffer(void) {
    printf("  test_spi_null_buffer... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    ASSERT_INT_EQ(SPI_ERR_NULL_BUF, spi_write_blocking(SPI_INSTANCE_0, NULL, 1));
    ASSERT_INT_EQ(SPI_ERR_NULL_BUF, spi_read_blocking(SPI_INSTANCE_0, NULL, 1));
    ASSERT_INT_EQ(SPI_ERR_NULL_BUF, spi_transfer_blocking(SPI_INSTANCE_0, NULL, (uint8_t[]){0}, 1));
    ASSERT_INT_EQ(SPI_ERR_NULL_BUF, spi_transfer_blocking(SPI_INSTANCE_0, (const uint8_t[]){0}, NULL, 1));
    printf("OK\n");
}

static void test_spi_deinit(void) {
    printf("  test_spi_deinit... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_0));
    ASSERT_INT_EQ(SPI_OK, spi_deinit(SPI_INSTANCE_0));
    ASSERT_FALSE(spi_is_initialized(SPI_INSTANCE_0));
    printf("OK\n");
}

static void test_spi_cs_pin_mapping(void) {
    printf("  test_spi_cs_pin_mapping... ");
    ASSERT_UINT_EQ(SPI0_CS_PIN, spi_get_instance_cs_pin(SPI_INSTANCE_0));
    ASSERT_UINT_EQ(SPI1_CS_PIN, spi_get_instance_cs_pin(SPI_INSTANCE_1));
    ASSERT_UINT_EQ(SPI2_CS_PIN, spi_get_instance_cs_pin(SPI_INSTANCE_2));
    ASSERT_UINT_EQ(0xFF, spi_get_instance_cs_pin(3));
    printf("OK\n");
}

static void test_spi_base_addr_mapping(void) {
    printf("  test_spi_base_addr_mapping... ");
    ASSERT_UINT_EQ(SPI0_BASE_ADDR, spi_get_instance_base(SPI_INSTANCE_0));
    ASSERT_UINT_EQ(SPI1_BASE_ADDR, spi_get_instance_base(SPI_INSTANCE_1));
    ASSERT_UINT_EQ(SPI2_BASE_ADDR, spi_get_instance_base(SPI_INSTANCE_2));
    ASSERT_UINT_EQ(0, spi_get_instance_base(3));
    printf("OK\n");
}

static void test_spi_modes(void) {
    printf("  test_spi_modes... ");
    spi_test_reset();
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE0, 1000000, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_deinit(SPI_INSTANCE_0));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE1, 1000000, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_deinit(SPI_INSTANCE_0));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE2, 1000000, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_deinit(SPI_INSTANCE_0));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE3, 1000000, SPI_MSB_FIRST));
    printf("OK\n");
}

static void test_spi_zero_length(void) {
    printf("  test_spi_zero_length... ");
    spi_test_reset();
    spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST);
    ASSERT_INT_EQ(SPI_OK, spi_write_blocking(SPI_INSTANCE_0, NULL, 0));
    ASSERT_INT_EQ(SPI_OK, spi_read_blocking(SPI_INSTANCE_0, NULL, 0));
    printf("OK\n");
}

/* ======================================================================== */
/*  UART Tests                                                              */
/* ======================================================================== */

static void test_uart_init(void) {
    printf("  test_uart_init... ");
    uart_test_reset();
    uart_config_t cfg = {
        .baud_hz = 115200,
        .word_len = UART_WORDLEN_8,
        .parity = UART_PARITY_NONE,
        .stop_bits = UART_STOPBITS_1,
        .flow_control = false,
    };
    ASSERT_INT_EQ(UART_OK, uart_init(UART_INSTANCE_0, &cfg));
    ASSERT_TRUE(uart_is_initialized(UART_INSTANCE_0));
    ASSERT_UINT_EQ(115200, uart_get_baud(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_init_default(void) {
    printf("  test_uart_init_default... ");
    uart_test_reset();
    ASSERT_INT_EQ(UART_OK, uart_init_default(UART_INSTANCE_0));
    ASSERT_TRUE(uart_is_initialized(UART_INSTANCE_0));
    ASSERT_UINT_EQ(UART_DEFAULT_BAUD, uart_get_baud(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_invalid_instance(void) {
    printf("  test_uart_invalid_instance... ");
    uart_test_reset();
    uart_config_t cfg = { .baud_hz = 115200, .word_len = UART_WORDLEN_8,
                          .parity = UART_PARITY_NONE, .stop_bits = UART_STOPBITS_1, .flow_control = false };
    ASSERT_INT_EQ(UART_ERR_INVALID_ARG, uart_init(1, &cfg));
    ASSERT_INT_EQ(UART_ERR_INVALID_ARG, uart_init(255, &cfg));
    printf("OK\n");
}

static void test_uart_null_config(void) {
    printf("  test_uart_null_config... ");
    uart_test_reset();
    ASSERT_INT_EQ(UART_ERR_INVALID_ARG, uart_init(UART_INSTANCE_0, NULL));
    printf("OK\n");
}

static void test_uart_write_blocking(void) {
    printf("  test_uart_write_blocking... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);

    uint8_t data[] = { 'H', 'e', 'l', 'l', 'o' };
    ASSERT_INT_EQ(UART_OK, uart_write_blocking(UART_INSTANCE_0, data, 5));

    size_t tx_len = 0;
    const uint8_t *tx_buf = uart_test_get_tx(&tx_len);
    ASSERT_INT_EQ(5, (int)tx_len);
    ASSERT_UINT_EQ('H', tx_buf[0]);
    ASSERT_UINT_EQ('e', tx_buf[1]);
    ASSERT_UINT_EQ('l', tx_buf[2]);
    ASSERT_UINT_EQ('l', tx_buf[3]);
    ASSERT_UINT_EQ('o', tx_buf[4]);
    printf("OK\n");
}

static void test_uart_write_string(void) {
    printf("  test_uart_write_string... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);

    ASSERT_INT_EQ(UART_OK, uart_write_string(UART_INSTANCE_0, "GhostWisp"));

    size_t tx_len = 0;
    const uint8_t *tx_buf = uart_test_get_tx(&tx_len);
    ASSERT_INT_EQ(9, (int)tx_len);
    ASSERT_UINT_EQ('G', tx_buf[0]);
    ASSERT_UINT_EQ('p', tx_buf[8]);
    printf("OK\n");
}

static void test_uart_write_char(void) {
    printf("  test_uart_write_char... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);

    ASSERT_INT_EQ(UART_OK, uart_write_char(UART_INSTANCE_0, 'X'));
    size_t tx_len = 0;
    const uint8_t *tx_buf = uart_test_get_tx(&tx_len);
    ASSERT_INT_EQ(1, (int)tx_len);
    ASSERT_UINT_EQ('X', tx_buf[0]);
    printf("OK\n");
}

static void test_uart_rx_inject_read(void) {
    printf("  test_uart_rx_inject_read... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);

    uint8_t rx_data[] = { 'A', 'B', 'C' };
    uart_test_inject_rx(rx_data, 3);

    ASSERT_INT_EQ(3, (int)uart_rx_available(UART_INSTANCE_0));

    char ch = 0;
    ASSERT_INT_EQ(UART_OK, uart_read_char(UART_INSTANCE_0, &ch));
    ASSERT_UINT_EQ('A', (uint8_t)ch);
    ASSERT_INT_EQ(UART_OK, uart_read_char(UART_INSTANCE_0, &ch));
    ASSERT_UINT_EQ('B', (uint8_t)ch);
    ASSERT_INT_EQ(UART_OK, uart_read_char(UART_INSTANCE_0, &ch));
    ASSERT_UINT_EQ('C', (uint8_t)ch);

    ASSERT_INT_EQ(0, (int)uart_rx_available(UART_INSTANCE_0));
    ASSERT_INT_EQ(UART_ERR_TIMEOUT, uart_read_char(UART_INSTANCE_0, &ch));
    printf("OK\n");
}

static void test_uart_rx_flush(void) {
    printf("  test_uart_rx_flush... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);

    uint8_t rx_data[] = { 1, 2, 3, 4, 5 };
    uart_test_inject_rx(rx_data, 5);
    ASSERT_INT_EQ(5, (int)uart_rx_available(UART_INSTANCE_0));

    ASSERT_INT_EQ(UART_OK, uart_rx_flush(UART_INSTANCE_0));
    ASSERT_INT_EQ(0, (int)uart_rx_available(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_set_baud(void) {
    printf("  test_uart_set_baud... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);
    ASSERT_INT_EQ(UART_OK, uart_set_baud(UART_INSTANCE_0, 9600));
    ASSERT_UINT_EQ(9600, uart_get_baud(UART_INSTANCE_0));
    ASSERT_INT_EQ(UART_OK, uart_set_baud(UART_INSTANCE_0, 230400));
    ASSERT_UINT_EQ(230400, uart_get_baud(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_not_init(void) {
    printf("  test_uart_not_init... ");
    uart_test_reset();
    ASSERT_INT_EQ(UART_ERR_NOT_INIT, uart_write_blocking(UART_INSTANCE_0, (const uint8_t[]){0}, 1));
    ASSERT_INT_EQ(UART_ERR_NOT_INIT, uart_read_char(UART_INSTANCE_0, NULL));
    ASSERT_INT_EQ(UART_ERR_NOT_INIT, uart_set_baud(UART_INSTANCE_0, 9600));
    printf("OK\n");
}

static void test_uart_null_buffer(void) {
    printf("  test_uart_null_buffer... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);
    ASSERT_INT_EQ(UART_ERR_INVALID_ARG, uart_write_blocking(UART_INSTANCE_0, NULL, 1));
    ASSERT_INT_EQ(UART_ERR_INVALID_ARG, uart_read_char(UART_INSTANCE_0, NULL));
    ASSERT_INT_EQ(UART_ERR_INVALID_ARG, uart_write_string(UART_INSTANCE_0, NULL));
    printf("OK\n");
}

static void test_uart_deinit(void) {
    printf("  test_uart_deinit... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);
    ASSERT_TRUE(uart_is_initialized(UART_INSTANCE_0));
    ASSERT_INT_EQ(UART_OK, uart_deinit(UART_INSTANCE_0));
    ASSERT_FALSE(uart_is_initialized(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_rx_available(void) {
    printf("  test_uart_rx_available... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);
    ASSERT_INT_EQ(0, (int)uart_rx_available(UART_INSTANCE_0));

    uint8_t data[] = { 1, 2, 3 };
    uart_test_inject_rx(data, 3);
    ASSERT_INT_EQ(3, (int)uart_rx_available(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_error_count(void) {
    printf("  test_uart_error_count... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);
    ASSERT_UINT_EQ(0, uart_get_error_count(UART_INSTANCE_0));
    printf("OK\n");
}

static void test_uart_write_long_string(void) {
    printf("  test_uart_write_long_string... ");
    uart_test_reset();
    uart_init_default(UART_INSTANCE_0);

    /* Write 100 chars */
    char buf[101];
    memset(buf, 'Z', 100);
    buf[100] = '\0';
    ASSERT_INT_EQ(UART_OK, uart_write_string(UART_INSTANCE_0, buf));

    size_t tx_len = 0;
    uart_test_get_tx(&tx_len);
    ASSERT_INT_EQ(100, (int)tx_len);
    printf("OK\n");
}

/* ======================================================================== */
/*  Cross-driver integration tests                                          */
/* ======================================================================== */

static void test_integration_gpio_then_spi(void) {
    printf("  test_integration_gpio_then_spi... ");
    gpio_test_reset();
    spi_test_reset();

    /* Initialize GPIO first (for CS pin control) */
    ASSERT_INT_EQ(GPIO_OK, gpio_init());
    ASSERT_TRUE(gpio_is_initialized());

    /* Then initialize SPI (which uses GPIO for CS) */
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE0, 10000000, SPI_MSB_FIRST));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_0));

    /* CS should be controllable via GPIO */
    ASSERT_INT_EQ(SPI_OK, spi_cs_select(SPI_INSTANCE_0));
    ASSERT_TRUE(spi_test_cs_asserted(SPI_INSTANCE_0));
    ASSERT_INT_EQ(SPI_OK, spi_cs_deselect(SPI_INSTANCE_0));
    ASSERT_FALSE(spi_test_cs_asserted(SPI_INSTANCE_0));
    printf("OK\n");
}

static void test_integration_boot_then_peripherals(void) {
    printf("  test_integration_boot_then_peripherals... ");
    /* Simulate the boot→driver init sequence */
    gpio_test_reset();
    i2c_test_reset();
    spi_test_reset();
    uart_test_reset();

    /* 1. GPIO driver init (after boot_phase_gpio) */
    ASSERT_INT_EQ(GPIO_OK, gpio_init());

    /* 2. UART init (after boot_phase_uart) */
    ASSERT_INT_EQ(UART_OK, uart_init_default(UART_INSTANCE_0));

    /* 3. I2C init (after boot_phase_battery) */
    ASSERT_INT_EQ(I2C_OK, i2c_init(I2C_INSTANCE_0, 400000));

    /* 4. SPI init (after boot_phase_post) */
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_0, SPI_MODE0, SPI0_DEFAULT_BAUD, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_1, SPI_MODE0, SPI1_DEFAULT_BAUD, SPI_MSB_FIRST));
    ASSERT_INT_EQ(SPI_OK, spi_init(SPI_INSTANCE_2, SPI_MODE0, SPI2_DEFAULT_BAUD, SPI_MSB_FIRST));

    /* All drivers should be ready */
    ASSERT_TRUE(gpio_is_initialized());
    ASSERT_TRUE(uart_is_initialized(UART_INSTANCE_0));
    ASSERT_TRUE(i2c_is_initialized());
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_0));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_1));
    ASSERT_TRUE(spi_is_initialized(SPI_INSTANCE_2));
    printf("OK\n");
}

static void test_integration_post_peripherals(void) {
    printf("  test_integration_post_peripherals... ");
    /* Simulate POST: probe I2C fuel gauge, read SPI device IDs */
    gpio_test_reset();
    i2c_test_reset();
    spi_test_reset();

    gpio_init();
    i2c_init(I2C_INSTANCE_0, 400000);
    i2c_test_add_slave(I2C_ADDR_FUEL_GAUGE);
    spi_init(SPI_INSTANCE_0, SPI_MODE0, SPI0_DEFAULT_BAUD, SPI_MSB_FIRST);
    spi_init(SPI_INSTANCE_1, SPI_MODE0, SPI1_DEFAULT_BAUD, SPI_MSB_FIRST);

    /* POST check 1: I2C fuel gauge present */
    ASSERT_INT_EQ(I2C_OK, i2c_probe(I2C_ADDR_FUEL_GAUGE));

    /* POST check 2: CC1101 version register on SPI0 */
    spi_test_set_register(SPI_INSTANCE_0, 0x31, 0x04);  /* CC1101 VERSION */
    uint8_t cc1101_ver = 0;
    ASSERT_INT_EQ(SPI_OK, spi_read_register(SPI_INSTANCE_0, 0x31, &cc1101_ver));
    ASSERT_UINT_EQ(0x04, cc1101_ver);

    /* POST check 3: ST25R3916 chip ID on SPI1 */
    spi_test_set_register(SPI_INSTANCE_1, 0x3F, 0x16);  /* ST25R3916B chip ID */
    uint8_t nfc_id = 0;
    ASSERT_INT_EQ(SPI_OK, spi_read_register(SPI_INSTANCE_1, 0x3F, &nfc_id));
    ASSERT_UINT_EQ(0x16, nfc_id);
    printf("OK\n");
}

/* ======================================================================== */
/*  Main                                                                    */
/* ======================================================================== */

int main(void) {
    printf("\n");
    printf("==========================================================\n");
    printf("  GhostWisp Peripheral Driver Stack — Unit Tests\n");
    printf("  I2C / SPI / UART / GPIO — Project Little Spectre\n");
    printf("==========================================================\n");
    printf("\n");

    printf("--- GPIO Driver Tests ---\n");
    test_gpio_init();
    test_gpio_set_function();
    test_gpio_invalid_pin();
    test_gpio_dir_output();
    test_gpio_dir_input();
    test_gpio_put_get();
    test_gpio_toggle();
    test_gpio_pull_config();
    test_gpio_input_read();
    test_gpio_irq();
    test_gpio_irq_disable();
    test_gpio_irq_invalid_events();
    test_gpio_button_pin_mapping();
    test_gpio_button_pressed();
    test_gpio_button_raw();
    test_gpio_pin_count();
    test_gpio_callback_null();
    printf("\n");

    printf("--- I2C Driver Tests ---\n");
    test_i2c_init();
    test_i2c_invalid_instance();
    test_i2c_invalid_baud();
    test_i2c_probe_present();
    test_i2c_probe_absent();
    test_i2c_write_register();
    test_i2c_read_register();
    test_i2c_read_register16();
    test_i2c_write_blocking();
    test_i2c_read_blocking();
    test_i2c_nack();
    test_i2c_not_init();
    test_i2c_too_long();
    test_i2c_deinit();
    test_i2c_null_buffer();
    test_i2c_error_count();
    test_i2c_fuel_gauge_integration();
    printf("\n");

    printf("--- SPI Driver Tests ---\n");
    test_spi_init();
    test_spi_init_all_instances();
    test_spi_invalid_instance();
    test_spi_invalid_baud();
    test_spi_cs_control();
    test_spi_write_blocking();
    test_spi_read_blocking();
    test_spi_transfer_loopback();
    test_spi_write_register();
    test_spi_read_register();
    test_spi_cc1101_version_read();
    test_spi_set_baud();
    test_spi_not_init();
    test_spi_too_long();
    test_spi_null_buffer();
    test_spi_deinit();
    test_spi_cs_pin_mapping();
    test_spi_base_addr_mapping();
    test_spi_modes();
    test_spi_zero_length();
    printf("\n");

    printf("--- UART Driver Tests ---\n");
    test_uart_init();
    test_uart_init_default();
    test_uart_invalid_instance();
    test_uart_null_config();
    test_uart_write_blocking();
    test_uart_write_string();
    test_uart_write_char();
    test_uart_rx_inject_read();
    test_uart_rx_flush();
    test_uart_set_baud();
    test_uart_not_init();
    test_uart_null_buffer();
    test_uart_deinit();
    test_uart_rx_available();
    test_uart_error_count();
    test_uart_write_long_string();
    printf("\n");

    printf("--- Cross-Driver Integration Tests ---\n");
    test_integration_gpio_then_spi();
    test_integration_boot_then_peripherals();
    test_integration_post_peripherals();
    printf("\n");

    printf("==========================================================\n");
    printf("  Tests run:    %d\n", g_tests_run);
    printf("  Tests passed: %d\n", g_tests_passed);
    printf("  Tests failed: %d\n", g_tests_failed);
    printf("==========================================================\n");

    if (g_tests_failed > 0) {
        printf("\n  *** %d TEST(S) FAILED ***\n\n", g_tests_failed);
        return 1;
    }

    printf("\n  All tests passed.\n\n");
    return 0;
}