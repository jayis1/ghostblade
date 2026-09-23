/*
 * ghostwisp_i2c.h — GhostWisp I2C Driver API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a blocking I2C master driver for the GhostWisp RP2350B.
 * I2C0 is connected to the MAX17048 fuel gauge (address 0x36) at
 * 400 kHz Fast Mode. This driver wraps the RP2350B I2C peripheral
 * registers with a clean, testable interface.
 *
 * Boot phase 7 (battery check) uses this driver to read the fuel
 * gauge. Other I2C devices on the same bus can use it for runtime
 * communication.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 5 (I2C)
 */

#ifndef GHOSTWISP_I2C_H
#define GHOSTWISP_I2C_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

/** I2C0 instance identifier */
#define I2C_INSTANCE_0     0

/** I2C0 base address (RP2350B) */
#define I2C0_BASE_ADDR     0x40078000UL

/** Default I2C0 clock frequency: 400 kHz Fast Mode */
#define I2C0_DEFAULT_BAUD  400000UL

/** Maximum I2C transfer length (RP2350B FIFO is 16 bytes, we support multi-transfer) */
#define I2C_MAX_TRANSFER   256

/** I2C timeout in microseconds (1 ms at 400 kHz) */
#define I2C_TIMEOUT_US     1000UL

/** Standard I2C addresses used on GhostWisp */
#define I2C_ADDR_FUEL_GAUGE   0x36    /**< MAX17048 fuel gauge (7-bit) */

/* ── I2C result codes ────────────────────────────────────────────────────── */

typedef enum {
    I2C_OK                = 0,
    I2C_ERR_INVALID_ARG   = -1,   /**< Invalid parameter */
    I2C_ERR_NOT_INIT      = -2,   /**< Driver not initialized */
    I2C_ERR_NACK          = -3,   /**< Slave NACK'd address or data */
    I2C_ERR_TIMEOUT       = -4,   /**< Transfer timed out */
    I2C_ERR_BUSY          = -5,   /**< Bus busy and can't acquire */
    I2C_ERR_TOO_LONG      = -6,   /**< Transfer exceeds I2C_MAX_TRANSFER */
    I2C_ERR_BUS_FAULT     = -7,   /**< Bus stuck low (SDA or SCL held low) */
} i2c_result_t;

/* ── I2C register offsets (from RP2350B DW I2C controller) ──────────────── */

#define I2C_REG_CON         0x00    /**< Control register */
#define I2C_REG_TAR         0x04    /**< Target address register */
#define I2C_REG_DATA_CMD    0x10    /**< Data buffer and command register */
#define I2C_REG_ENABLE      0x6C    /**< Enable register */
#define I2C_REG_STATUS      0x70    /**< Status register */
#define I2C_REG_RAW_INT_STAT 0x34   /**< Raw interrupt status */
#define I2C_REG_CLR_TX_ABRT 0x54    /**< Clear TX abort */
#define I2C_REG_ENABLE_STATUS 0x9C  /**< Enable status */

/* CON register bits */
#define I2C_CON_MASTER      (1U << 0)
#define I2C_CON_SPEED_STD   (1U << 1)
#define I2C_CON_SPEED_FAST  (1U << 2)
#define I2C_CON_RESTART_EN  (1U << 5)
#define I2C_CON_SLAVE_DISABLE (1U << 6)

/* DATA_CMD bits */
#define I2C_CMD_READ        (1U << 8)
#define I2C_CMD_STOP        (1U << 9)
#define I2C_CMD_RESTART     (1U << 10)

/* STATUS register bits */
#define I2C_STATUS_ACTIVITY (1U << 0)
#define I2C_STATUS_TFNF     (1U << 1)   /**< TX FIFO not full */
#define I2C_STATUS_TFE      (1U << 2)   /**< TX FIFO empty */
#define I2C_STATUS_RFNE     (1U << 3)   /**< RX FIFO not empty */

/* ENABLE register */
#define I2C_ENABLE_CTRL     (1U << 0)

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * i2c_init — Initialize the I2C0 master peripheral
 *
 * Configures I2C0 as a master at the specified baud rate (default
 * 400 kHz). Enables the I2C controller and clears any pending
 * interrupts. Must be called after boot_phase_gpio() has muxed
 * the I2C0 pins.
 *
 * @instance: I2C_INSTANCE_0 (only instance on GhostWisp)
 * @baud_hz:  Desired clock frequency in Hz (e.g., 400000 for Fast Mode)
 * Returns I2C_OK on success.
 */
i2c_result_t i2c_init(uint8_t instance, uint32_t baud_hz);

/**
 * i2c_write_blocking — Write data to an I2C slave (blocking)
 *
 * Sends start + address + data + stop. Blocks until the transfer
 * completes or times out.
 *
 * @addr:     7-bit slave address (e.g., 0x36 for fuel gauge)
 * @data:     Pointer to data buffer
 * @len:      Number of bytes to write (0–I2C_MAX_TRANSFER)
 * Returns I2C_OK on success, error code on failure.
 */
i2c_result_t i2c_write_blocking(uint8_t addr, const uint8_t *data, size_t len);

/**
 * i2c_read_blocking — Read data from an I2C slave (blocking)
 *
 * Sends start + address (read) + reads len bytes + stop.
 *
 * @addr:     7-bit slave address
 * @data:     Pointer to receive buffer
 * @len:      Number of bytes to read (0–I2C_MAX_TRANSFER)
 * Returns I2C_OK on success, error code on failure.
 */
i2c_result_t i2c_read_blocking(uint8_t addr, uint8_t *data, size_t len);

/**
 * i2c_write_register — Write a single byte to a register (write-then-read pattern)
 *
 * Sends: START + addr(W) + reg + data + STOP
 * Common pattern for MAX17048 and similar sensors.
 *
 * @addr: 7-bit slave address
 * @reg:  Register address
 * @val:  Byte to write
 * Returns I2C_OK on success.
 */
i2c_result_t i2c_write_register(uint8_t addr, uint8_t reg, uint8_t val);

/**
 * i2c_read_register — Read a single byte from a register (write-then-read)
 *
 * Sends: START + addr(W) + reg + REPEATED_START + addr(R) + data + STOP
 * Common pattern for MAX17048 and similar sensors.
 *
 * @addr: 7-bit slave address
 * @reg:  Register address
 * @out:  Pointer to receive the byte
 * Returns I2C_OK on success.
 */
i2c_result_t i2c_read_register(uint8_t addr, uint8_t reg, uint8_t *out);

/**
 * i2c_read_register16 — Read a 16-bit big-endian register (write-then-read)
 *
 * For MAX17048 which returns 16-bit values in big-endian order.
 * Performs: write reg → read 2 bytes → combine as (buf[0] << 8) | buf[1].
 *
 * @addr:  7-bit slave address
 * @reg:   Register address
 * @out16: Pointer to receive the 16-bit value
 * Returns I2C_OK on success.
 */
i2c_result_t i2c_read_register16(uint8_t addr, uint8_t reg, uint16_t *out16);

/**
 * i2c_probe — Check if a slave is present on the bus
 *
 * Performs a zero-length write (address only). If the slave
 * ACKs, it is present. Used by POST to detect peripherals.
 *
 * @addr: 7-bit slave address
 * Returns I2C_OK if present, I2C_ERR_NACK if absent, other error on bus fault.
 */
i2c_result_t i2c_probe(uint8_t addr);

/**
 * i2c_deinit — Disable the I2C peripheral
 *
 * Disables the controller and clears state. Call before sleep.
 *
 * Returns I2C_OK on success.
 */
i2c_result_t i2c_deinit(void);

/**
 * i2c_is_initialized — Check if the driver has been initialized
 *
 * Returns true if i2c_init() has been called successfully.
 */
bool i2c_is_initialized(void);

/**
 * i2c_get_baud — Get the configured baud rate
 *
 * Returns the actual baud rate in Hz, or 0 if not initialized.
 */
uint32_t i2c_get_baud(void);

/**
 * i2c_get_error_count — Get the cumulative error count
 *
 * Increments on each failed transfer. Useful for diagnostics.
 *
 * Returns the total number of failed transfers since init.
 */
uint32_t i2c_get_error_count(void);

#endif /* GHOSTWISP_I2C_H */