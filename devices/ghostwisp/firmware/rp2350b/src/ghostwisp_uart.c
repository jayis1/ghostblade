/*
 * ghostwisp_uart.c — GhostWisp UART Driver Implementation
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the UART driver for the GhostWisp RP2350B. UART0 is the
 * debug console and application UART terminal, on the expansion header
 * pins 35/36 at 115200 8N1 by default.
 *
 * Features:
 *   - Blocking TX (polled, waits for FIFO space)
 *   - Ring-buffered RX with interrupt-driven filling
 *   - Baud rate calculation (PL011 fractional divisor)
 *   - Error detection (overrun, framing, parity, break)
 *
 * Host test mode: When DRIVER_HOST_TEST is defined, the UART is
 * simulated with a TX capture buffer and an injectable RX buffer
 * for testing.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 6 (UART)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ghostwisp_uart.h"

/* ======================================================================== */
/*  Per-instance state                                                      */
/* ======================================================================== */

typedef struct {
    bool          initialized;
    uint32_t      base_addr;
    uint32_t      baud_hz;
    uart_config_t config;
    uint32_t      error_count;
    /* RX ring buffer */
    uint8_t       rx_ring[UART_RX_BUF_SIZE];
    uint16_t      rx_head;   /* Written by ISR (next write position) */
    uint16_t      rx_tail;   /* Read by main loop (next read position) */
} uart_instance_state_t;

#ifndef DRIVER_HOST_TEST
static uart_instance_state_t g_uart[1] = {
    { false, UART0_BASE_ADDR, 0, {0}, 0, {0}, 0, 0 },
};
#else
static uart_instance_state_t g_uart[1];
#endif

/* RX ring buffer helpers (power-of-2 mask) */
#define RX_MASK (UART_RX_BUF_SIZE - 1)
#define rx_count(state) ((uint16_t)((state)->rx_head - (state)->rx_tail) & RX_MASK)
#define rx_space(state) ((uint16_t)(UART_RX_BUF_SIZE - 1 - rx_count(state)))

/* ======================================================================== */
/*  Host Test Mode                                                          */
/* ======================================================================== */

#ifdef DRIVER_HOST_TEST

/* Test TX capture buffer */
#define TEST_TX_BUF_SIZE  4096
static uint8_t  test_tx_buf[TEST_TX_BUF_SIZE];
static size_t   test_tx_len = 0;

void uart_test_reset(void) {
    memset(g_uart, 0, sizeof(g_uart));
    g_uart[0].base_addr = UART0_BASE_ADDR;
    test_tx_len = 0;
}

void uart_test_inject_rx(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint16_t next = (g_uart[0].rx_head + 1) & RX_MASK;
        if (next != g_uart[0].rx_tail) {
            g_uart[0].rx_ring[g_uart[0].rx_head] = data[i];
            g_uart[0].rx_head = next;
        }
    }
}

const uint8_t *uart_test_get_tx(size_t *len) {
    if (len) *len = test_tx_len;
    return test_tx_buf;
}

bool uart_test_is_init(void) { return g_uart[0].initialized; }
uint32_t uart_test_get_error_count(void) { return g_uart[0].error_count; }

#endif /* DRIVER_HOST_TEST */

/* ======================================================================== */
/*  Internal helpers (real hardware)                                        */
/* ======================================================================== */

#ifndef DRIVER_HOST_TEST

#define REG32(addr)  (*(volatile uint32_t *)(addr))

static volatile uint32_t *uart_reg(uint8_t instance, uint32_t offset) {
    return (volatile uint32_t *)(g_uart[instance].base_addr + offset);
}

static void uart_delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us * 150; i++)
        __asm__ volatile ("nop");
}

/* Compute baud rate divisor for PL011
 * baud = uart_clk / (16 * (IBRD + FBRD/64))
 * IBRD = int(uart_clk / (16 * baud))
 * FBRD = round(frac * 64)
 */
static void uart_compute_baud(uint32_t baud_hz, uint32_t *ibrd, uint32_t *fbrd) {
    uint32_t uart_clk = 48000000UL;  /* Peripheral clock */
    uint64_t div = ((uint64_t)uart_clk << 6) / ((uint64_t)baud_hz * 16);
    *ibrd = (uint32_t)(div >> 6);
    *fbrd = (uint32_t)(div & 0x3F);
}

/* Check if TX FIFO is not full */
static bool uart_tx_not_full(uint8_t instance) {
    return (*uart_reg(instance, UART_REG_FR) & UART_FR_TXFF) == 0;
}

/* Check if RX FIFO is not empty */
static bool uart_rx_not_empty(uint8_t instance) {
    return (*uart_reg(instance, UART_REG_FR) & UART_FR_RXFE) == 0;
}

/* Check if UART is busy (transmitting) */
static bool uart_busy(uint8_t instance) {
    return (*uart_reg(instance, UART_REG_FR) & UART_FR_BUSY) != 0;
}

#endif /* DRIVER_HOST_TEST */

/* ======================================================================== */
/*  Implementation                                                          */
/* ======================================================================== */

uart_result_t uart_init(uint8_t instance, const uart_config_t *config) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;
    if (config == NULL)
        return UART_ERR_INVALID_ARG;
    if (config->baud_hz == 0)
        return UART_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    g_uart[0].initialized = true;
    g_uart[0].baud_hz = config->baud_hz;
    g_uart[0].config = *config;
    g_uart[0].error_count = 0;
    g_uart[0].rx_head = 0;
    g_uart[0].rx_tail = 0;
    test_tx_len = 0;
    return UART_OK;
#else
    /* Disable UART during configuration */
    *uart_reg(instance, UART_REG_CR) = 0;

    /* Set baud rate divisors */
    uint32_t ibrd, fbrd;
    uart_compute_baud(config->baud_hz, &ibrd, &fbrd);
    *uart_reg(instance, UART_REG_IBRD) = ibrd;
    *uart_reg(instance, UART_REG_FBRD) = fbrd;

    /* Configure line control: word length, parity, stop bits, FIFOs */
    uint32_t lcr_h = UART_LCR_H_FEN;  /* Enable FIFOs */
    if (config->word_len == UART_WORDLEN_8)
        lcr_h |= UART_LCR_H_WLEN_8;
    else
        lcr_h |= UART_LCR_H_WLEN_7;

    if (config->parity != UART_PARITY_NONE) {
        lcr_h |= UART_LCR_H_PEN;
        if (config->parity == UART_PARITY_EVEN)
            lcr_h |= UART_LCR_H_EPS;
    }

    if (config->stop_bits == UART_STOPBITS_2)
        lcr_h |= UART_LCR_H_STP2;

    *uart_reg(instance, UART_REG_LCR_H) = lcr_h;

    /* Configure control register: enable UART, TX, RX */
    uint32_t cr = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
    if (config->flow_control)
        cr |= (UART_CR_RTSEN | UART_CR_CTSEN);
    *uart_reg(instance, UART_REG_CR) = cr;

    /* Clear any pending interrupts */
    *uart_reg(instance, UART_REG_ICR) = 0x7FF;

    /* Enable RX interrupt (for ring buffer filling) */
    *uart_reg(instance, UART_REG_IMSC) = (1U << 4);  /* RXIM */

    g_uart[instance].initialized = true;
    g_uart[instance].baud_hz = config->baud_hz;
    g_uart[instance].config = *config;
    g_uart[instance].error_count = 0;
    g_uart[instance].rx_head = 0;
    g_uart[instance].rx_tail = 0;
    __asm__ volatile ("dmb" ::: "memory");
    return UART_OK;
#endif
}

uart_result_t uart_init_default(uint8_t instance) {
    uart_config_t default_config = {
        .baud_hz = UART_DEFAULT_BAUD,
        .word_len = UART_WORDLEN_8,
        .parity = UART_PARITY_NONE,
        .stop_bits = UART_STOPBITS_1,
        .flow_control = false,
    };
    return uart_init(instance, &default_config);
}

uart_result_t uart_write_blocking(uint8_t instance, const uint8_t *data, size_t len) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;
    if (!g_uart[instance].initialized)
        return UART_ERR_NOT_INIT;
    if (len > 0 && data == NULL)
        return UART_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    /* Capture TX data */
    for (size_t i = 0; i < len; i++) {
        if (test_tx_len < TEST_TX_BUF_SIZE)
            test_tx_buf[test_tx_len++] = data[i];
    }
    return UART_OK;
#else
    for (size_t i = 0; i < len; i++) {
        uint32_t timeout = UART_TX_TIMEOUT_US;
        while (!uart_tx_not_full(instance)) {
            if (--timeout == 0) {
                g_uart[instance].error_count++;
                return UART_ERR_TIMEOUT;
            }
            uart_delay_us(1);
        }
        *uart_reg(instance, UART_REG_DR) = data[i];
    }
    /* Wait for TX to complete (not busy) */
    uint32_t timeout = UART_TX_TIMEOUT_US * len;
    while (uart_busy(instance)) {
        if (--timeout == 0) {
            g_uart[instance].error_count++;
            return UART_ERR_TIMEOUT;
        }
        uart_delay_us(1);
    }
    return UART_OK;
#endif
}

uart_result_t uart_read_blocking(uint8_t instance, uint8_t *data, size_t len) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;
    if (!g_uart[instance].initialized)
        return UART_ERR_NOT_INIT;
    if (len > 0 && data == NULL)
        return UART_ERR_INVALID_ARG;

    for (size_t i = 0; i < len; i++) {
        /* Wait for a byte in the ring buffer */
#ifdef DRIVER_HOST_TEST
        /* In test mode, no async filling — check if data available */
        if (rx_count(&g_uart[instance]) == 0) {
            return UART_ERR_TIMEOUT;
        }
#else
        uint32_t timeout = UART_RX_TIMEOUT_US;
        while (rx_count(&g_uart[instance]) == 0) {
            if (--timeout == 0) {
                return UART_ERR_TIMEOUT;
            }
            uart_delay_us(1);
        }
#endif
        data[i] = g_uart[instance].rx_ring[g_uart[instance].rx_tail];
        g_uart[instance].rx_tail = (g_uart[instance].rx_tail + 1) & RX_MASK;
    }
    return UART_OK;
}

uart_result_t uart_write_string(uint8_t instance, const char *str) {
    if (str == NULL)
        return UART_ERR_INVALID_ARG;
    return uart_write_blocking(instance, (const uint8_t *)str, strlen(str));
}

uart_result_t uart_write_char(uint8_t instance, char ch) {
    return uart_write_blocking(instance, (const uint8_t *)&ch, 1);
}

uart_result_t uart_read_char(uint8_t instance, char *ch) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;
    if (!g_uart[instance].initialized)
        return UART_ERR_NOT_INIT;
    if (ch == NULL)
        return UART_ERR_INVALID_ARG;

    if (rx_count(&g_uart[instance]) == 0)
        return UART_ERR_TIMEOUT;

    *ch = (char)g_uart[instance].rx_ring[g_uart[instance].rx_tail];
    g_uart[instance].rx_tail = (g_uart[instance].rx_tail + 1) & RX_MASK;
    return UART_OK;
}

size_t uart_rx_available(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return 0;
    if (!g_uart[instance].initialized)
        return 0;
    return rx_count(&g_uart[instance]);
}

uart_result_t uart_rx_flush(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;
    if (!g_uart[instance].initialized)
        return UART_ERR_NOT_INIT;

    g_uart[instance].rx_head = 0;
    g_uart[instance].rx_tail = 0;
    return UART_OK;
}

uart_result_t uart_set_baud(uint8_t instance, uint32_t baud_hz) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;
    if (baud_hz == 0)
        return UART_ERR_INVALID_ARG;
    if (!g_uart[instance].initialized)
        return UART_ERR_NOT_INIT;

#ifdef DRIVER_HOST_TEST
    g_uart[instance].baud_hz = baud_hz;
    g_uart[instance].config.baud_hz = baud_hz;
    return UART_OK;
#else
    /* Disable UART, reconfigure baud, re-enable */
    uint32_t cr = *uart_reg(instance, UART_REG_CR);
    *uart_reg(instance, UART_REG_CR) = 0;

    uint32_t ibrd, fbrd;
    uart_compute_baud(baud_hz, &ibrd, &fbrd);
    *uart_reg(instance, UART_REG_IBRD) = ibrd;
    *uart_reg(instance, UART_REG_FBRD) = fbrd;

    *uart_reg(instance, UART_REG_CR) = cr;
    g_uart[instance].baud_hz = baud_hz;
    g_uart[instance].config.baud_hz = baud_hz;
    __asm__ volatile ("dmb" ::: "memory");
    return UART_OK;
#endif
}

uint32_t uart_get_baud(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return 0;
    return g_uart[instance].baud_hz;
}

void uart_handle_rx_irq(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return;
    if (!g_uart[instance].initialized)
        return;

#ifdef DRIVER_HOST_TEST
    /* In test mode, RX injection is done directly by test code.
     * This function is a no-op but exists for API completeness. */
#else
    /* Drain the hardware RX FIFO into the ring buffer */
    while (uart_rx_not_empty(instance)) {
        uint32_t dr = *uart_reg(instance, UART_REG_DR);

        /* Check for errors (bits 8–11 in DR) */
        if (dr & (1U << 8)) {  /* Overrun error */
            g_uart[instance].error_count++;
        }
        if (dr & (1U << 9)) {  /* Break error */
            g_uart[instance].error_count++;
        }
        if (dr & (1U << 10)) {  /* Parity error */
            g_uart[instance].error_count++;
        }
        if (dr & (1U << 11)) {  /* Framing error */
            g_uart[instance].error_count++;
        }

        uint8_t byte = (uint8_t)(dr & 0xFF);
        uint16_t next = (g_uart[instance].rx_head + 1) & RX_MASK;
        if (next != g_uart[instance].rx_tail) {
            g_uart[instance].rx_ring[g_uart[instance].rx_head] = byte;
            g_uart[instance].rx_head = next;
        } else {
            /* Ring buffer full — increment error count */
            g_uart[instance].error_count++;
        }
    }

    /* Clear RX interrupt */
    *uart_reg(instance, UART_REG_ICR) = UART_ICR_RXIC | UART_ICR_RTIC;
#endif
}

bool uart_is_initialized(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return false;
    return g_uart[instance].initialized;
}

uart_result_t uart_deinit(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return UART_ERR_INVALID_ARG;

#ifdef DRIVER_HOST_TEST
    g_uart[0].initialized = false;
    return UART_OK;
#else
    if (!g_uart[instance].initialized)
        return UART_OK;

    /* Wait for TX to complete, then disable */
    while (uart_busy(instance))
        uart_delay_us(1);

    *uart_reg(instance, UART_REG_CR) = 0;
    *uart_reg(instance, UART_REG_IMSC) = 0;
    *uart_reg(instance, UART_REG_ICR) = 0x7FF;
    __asm__ volatile ("dmb" ::: "memory");

    g_uart[instance].initialized = false;
    return UART_OK;
#endif
}

uint32_t uart_get_error_count(uint8_t instance) {
    if (instance != UART_INSTANCE_0)
        return 0;
    return g_uart[instance].error_count;
}