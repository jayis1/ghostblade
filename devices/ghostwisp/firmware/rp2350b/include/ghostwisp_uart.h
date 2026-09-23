/*
 * ghostwisp_uart.h — GhostWisp UART Driver API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a UART driver for the GhostWisp RP2350B. UART0 is the
 * debug console (115200 8N1 on expansion header pins 35/36), and
 * is also used for the application UART terminal and I2C/SPI probe
 * passthrough.
 *
 * The boot sequence configures UART0 for debug output; this driver
 * provides the runtime API for:
 *   - Polled (blocking) TX/RX
 *   - Ring-buffered interrupt-driven RX
 *   - Baud rate changes
 *   - Flow control configuration
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 6 (UART)
 */

#ifndef GHOSTWISP_UART_H
#define GHOSTWISP_UART_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

/** UART instance identifiers */
#define UART_INSTANCE_0    0

/** RP2350B UART0 base address */
#define UART0_BASE_ADDR    0x40070000UL

/** Default baud rate for debug console */
#define UART_DEFAULT_BAUD  115200UL

/** RX ring buffer size (power of 2 for mask efficiency) */
#define UART_RX_BUF_SIZE   256

/** TX timeout in microseconds (1 byte at 115200 ≈ 87 µs, allow margin) */
#define UART_TX_TIMEOUT_US 1000UL

/** RX timeout in microseconds */
#define UART_RX_TIMEOUT_US 100000UL   /**< 100 ms */

/* ── UART register offsets (PL011 PrimeCell) ────────────────────────────── */

#define UART_REG_DR         0x000   /**< Data register */
#define UART_REG_RSR_ECR    0x004   /**< Receive status / error clear */
#define UART_REG_FR         0x018   /**< Flag register */
#define UART_REG_ILPR       0x020   /**< IrDA low-power counter */
#define UART_REG_IBRD       0x024   /**< Integer baud rate divisor */
#define UART_REG_FBRD       0x028   /**< Fractional baud rate divisor */
#define UART_REG_LCR_H      0x02C   /**< Line control register */
#define UART_REG_CR         0x030   /**< Control register */
#define UART_REG_IFLS       0x034   /**< Interrupt FIFO level select */
#define UART_REG_IMSC       0x038   /**< Interrupt mask set/clear */
#define UART_REG_RIS        0x03C   /**< Raw interrupt status */
#define UART_REG_MIS        0x040   /**< Masked interrupt status */
#define UART_REG_ICR        0x044   /**< Interrupt clear */
#define UART_REG_DMACR      0x048   /**< DMA control */

/* FR (Flag Register) bits */
#define UART_FR_CTS         (1U << 0)   /**< Clear to send */
#define UART_FR_BUSY        (1U << 3)   /**< UART busy */
#define UART_FR_RXFE        (1U << 4)   /**< RX FIFO empty */
#define UART_FR_TXFF        (1U << 5)   /**< TX FIFO full */
#define UART_FR_RXFF        (1U << 6)   /**< RX FIFO full */
#define UART_FR_TXFE        (1U << 7)   /**< TX FIFO empty */

/* LCR_H bits */
#define UART_LCR_H_FEN      (1U << 4)   /**< FIFO enable */
#define UART_LCR_H_WLEN_8   (3U << 5)   /**< 8-bit word length */
#define UART_LCR_H_WLEN_7   (2U << 5)   /**< 7-bit word length */
#define UART_LCR_H_EPS      (1U << 2)   /**< Even parity select */
#define UART_LCR_H_PEN      (1U << 1)   /**< Parity enable */
#define UART_LCR_H_STP2     (1U << 3)   /**< Two stop bits */

/* CR bits */
#define UART_CR_UARTEN      (1U << 0)   /**< UART enable */
#define UART_CR_TXE         (1U << 8)   /**< TX enable */
#define UART_CR_RXE         (1U << 9)   /**< RX enable */
#define UART_CR_RTSEN       (1U << 14)  /**< RTS enable */
#define UART_CR_CTSEN       (1U << 15)  /**< CTS enable */

/* ICR bits */
#define UART_ICR_RXIC       (1U << 4)   /**< RX interrupt clear */
#define UART_ICR_TXIC       (1U << 5)   /**< TX interrupt clear */
#define UART_ICR_RTIC       (1U << 6)   /**< Receive timeout interrupt clear */
#define UART_ICR_OEIC       (1U << 10)  /**< Overrun error clear */
#define UART_ICR_BEIC       (1U << 9)   /**< Break error clear */
#define UART_ICR_PEIC       (1U << 8)   /**< Parity error clear */
#define UART_ICR_FEIC       (1U << 7)   /**< Framing error clear */

/* ── UART configuration ─────────────────────────────────────────────────── */

/** Parity configuration */
typedef enum {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN = 1,
    UART_PARITY_ODD  = 2,
} uart_parity_t;

/** Stop bits */
typedef enum {
    UART_STOPBITS_1 = 0,
    UART_STOPBITS_2 = 1,
} uart_stopbits_t;

/** Word length */
typedef enum {
    UART_WORDLEN_7 = 7,
    UART_WORDLEN_8 = 8,
} uart_wordlen_t;

/** UART config structure */
typedef struct {
    uint32_t baud_hz;        /**< Baud rate (e.g., 115200) */
    uart_wordlen_t word_len; /**< 7 or 8 bits */
    uart_parity_t parity;    /**< None, even, or odd */
    uart_stopbits_t stop_bits; /**< 1 or 2 stop bits */
    bool flow_control;       /**< true = hardware RTS/CTS */
} uart_config_t;

/* ── UART result codes ──────────────────────────────────────────────────── */

typedef enum {
    UART_OK               = 0,
    UART_ERR_INVALID_ARG   = -1,   /**< Invalid parameter */
    UART_ERR_NOT_INIT      = -2,   /**< Driver not initialized */
    UART_ERR_TIMEOUT       = -3,   /**< Operation timed out */
    UART_ERR_OVERRUN       = -4,   /**< RX buffer overrun */
    UART_ERR_FRAMING       = -5,   /**< Framing error detected */
    UART_ERR_PARITY        = -6,   /**< Parity error detected */
    UART_ERR_BREAK         = -7,   /**< Break condition detected */
} uart_result_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * uart_init — Initialize UART0 with a configuration
 *
 * Configures baud rate, word length, parity, stop bits, and flow control.
 * Enables the UART with FIFOs. Must be called after boot_phase_uart().
 *
 * @instance: UART_INSTANCE_0
 * @config:   Pointer to uart_config_t with desired settings
 * Returns UART_OK on success.
 */
uart_result_t uart_init(uint8_t instance, const uart_config_t *config);

/**
 * uart_init_default — Initialize UART0 with default settings (115200 8N1)
 *
 * Convenience function for the common debug console configuration.
 *
 * @instance: UART_INSTANCE_0
 * Returns UART_OK on success.
 */
uart_result_t uart_init_default(uint8_t instance);

/**
 * uart_write_blocking — Write bytes to UART (blocking)
 *
 * Writes len bytes to the TX FIFO, waiting for each byte to be
 * accepted. Blocks until all bytes are written or timeout.
 *
 * @instance: UART instance
 * @data:     Pointer to data to write
 * @len:      Number of bytes
 * Returns UART_OK on success, UART_ERR_TIMEOUT if TX FIFO stalled.
 */
uart_result_t uart_write_blocking(uint8_t instance, const uint8_t *data, size_t len);

/**
 * uart_read_blocking — Read bytes from UART (blocking, with timeout)
 *
 * Reads up to len bytes from the RX ring buffer, blocking until
 * all bytes are available or the timeout expires.
 *
 * @instance: UART instance
 * @data:     Pointer to receive buffer
 * @len:      Number of bytes to read
 * Returns UART_OK on success, UART_ERR_TIMEOUT if not all bytes received.
 */
uart_result_t uart_read_blocking(uint8_t instance, uint8_t *data, size_t len);

/**
 * uart_write_string — Write a null-terminated string (blocking)
 *
 * @instance: UART instance
 * @str:      Null-terminated string
 * Returns UART_OK on success.
 */
uart_result_t uart_write_string(uint8_t instance, const char *str);

/**
 * uart_write_char — Write a single character (blocking)
 *
 * @instance: UART instance
 * @ch:       Character to write
 * Returns UART_OK on success.
 */
uart_result_t uart_write_char(uint8_t instance, char ch);

/**
 * uart_read_char — Read a single character (non-blocking)
 *
 * Checks the RX ring buffer for a pending character.
 *
 * @instance: UART instance
 * @ch:       Pointer to receive the character
 * Returns UART_OK if a character was available, UART_ERR_TIMEOUT if empty.
 */
uart_result_t uart_read_char(uint8_t instance, char *ch);

/**
 * uart_rx_available — Check how many bytes are available in the RX buffer
 *
 * @instance: UART instance
 * Returns the number of bytes available to read.
 */
size_t uart_rx_available(uint8_t instance);

/**
 * uart_rx_flush — Flush the RX ring buffer (discard all pending data)
 *
 * @instance: UART instance
 * Returns UART_OK on success.
 */
uart_result_t uart_rx_flush(uint8_t instance);

/**
 * uart_set_baud — Change the baud rate at runtime
 *
 * @instance: UART instance
 * @baud_hz:  New baud rate
 * Returns UART_OK on success.
 */
uart_result_t uart_set_baud(uint8_t instance, uint32_t baud_hz);

/**
 * uart_get_baud — Get the current baud rate
 *
 * @instance: UART instance
 * Returns baud rate in Hz, or 0 if not initialized.
 */
uint32_t uart_get_baud(uint8_t instance);

/**
 * uart_handle_rx_irq — Process RX interrupt
 *
 * Called from the UART ISR to drain the hardware RX FIFO into
 * the ring buffer. Exposed for the IRQ vector and for testing.
 *
 * @instance: UART instance
 */
void uart_handle_rx_irq(uint8_t instance);

/**
 * uart_is_initialized — Check if the driver is initialized
 *
 * @instance: UART instance
 * Returns true if uart_init() succeeded.
 */
bool uart_is_initialized(uint8_t instance);

/**
 * uart_deinit — Disable the UART
 *
 * @instance: UART instance
 * Returns UART_OK on success.
 */
uart_result_t uart_deinit(uint8_t instance);

/**
 * uart_get_error_count — Get cumulative error count
 *
 * Returns total errors (overrun, framing, parity, break) since init.
 */
uint32_t uart_get_error_count(uint8_t instance);

#endif /* GHOSTWISP_UART_H */