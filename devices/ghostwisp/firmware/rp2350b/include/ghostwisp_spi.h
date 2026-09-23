/*
 * ghostwisp_spi.h — GhostWisp SPI Master Driver API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a blocking SPI master driver for the GhostWisp RP2350B.
 * GhostWisp has three SPI buses:
 *   SPI0 — CC1101 sub-GHz radio (pins 6–9, up to 10 MHz)
 *   SPI1 — ST25R3916 NFC controller (pins 12–15, up to 10 MHz)
 *   SPI2 — microSD card (pins 17–20, up to 20 MHz)
 *
 * The display uses PIO0 (not a hardware SPI instance) and is
 * managed by a separate display driver.
 *
 * This driver wraps the RP2350B SPI peripheral registers with
 * chip-select management, configurable clock frequency, and
 * blocking full-duplex transfers.
 *
 * Reference: devices/ghostwisp/docs/architecture.md
 *           RP2350B Datasheet: Section 3 (SPI)
 */

#ifndef GHOSTWISP_SPI_H
#define GHOSTWISP_SPI_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

/** SPI instance identifiers */
#define SPI_INSTANCE_0     0    /**< SPI0 — CC1101 radio */
#define SPI_INSTANCE_1     1    /**< SPI1 — ST25R3916 NFC */
#define SPI_INSTANCE_2     2    /**< SPI2 — microSD card */
#define SPI_INSTANCE_COUNT 3

/** RP2350B SPI base addresses */
#define SPI0_BASE_ADDR     0x400C0000UL
#define SPI1_BASE_ADDR     0x400C4000UL
#define SPI2_BASE_ADDR     0x400C8000UL

/** Default baud rates per bus (Hz) */
#define SPI0_DEFAULT_BAUD  10000000UL   /**< 10 MHz for CC1101 */
#define SPI1_DEFAULT_BAUD  10000000UL   /**< 10 MHz for ST25R3916 */
#define SPI2_DEFAULT_BAUD  10000000UL   /**< 10 MHz init (can go to 20 MHz) */

/** Maximum SPI transfer length */
#define SPI_MAX_TRANSFER   4096

/** SPI timeout in microseconds */
#define SPI_TIMEOUT_US     100000UL     /**< 100 ms */

/** Chip-select pins per SPI bus (from ghostwisp_pins.h) */
#define SPI0_CS_PIN        9     /**< CC1101 CSn */
#define SPI1_CS_PIN        15    /**< ST25R3916 CSn */
#define SPI2_CS_PIN        20    /**< microSD CSn */

/* ── SPI mode (CPOL/CPHA) ───────────────────────────────────────────────── */

typedef enum {
    SPI_MODE0 = 0,   /**< CPOL=0, CPHA=0 — clock idle low, sample on rising edge */
    SPI_MODE1 = 1,   /**< CPOL=0, CPHA=1 — clock idle low, sample on falling edge */
    SPI_MODE2 = 2,   /**< CPOL=1, CPHA=0 — clock idle high, sample on falling edge */
    SPI_MODE3 = 3,   /**< CPOL=1, CPHA=1 — clock idle high, sample on rising edge */
} spi_mode_t;

/** Bit order */
typedef enum {
    SPI_MSB_FIRST = 0,
    SPI_LSB_FIRST = 1,
} spi_bit_order_t;

/* ── SPI register offsets (RP2350B PL022 SSP) ───────────────────────────── */

#define SPI_REG_CR0        0x00    /**< Control register 0 */
#define SPI_REG_CR1        0x04    /**< Control register 1 */
#define SPI_REG_DR         0x08    /**< Data register */
#define SPI_REG_SR         0x0C    /**< Status register */
#define SPI_REG_CPSR       0x10    /**< Clock prescale register */
#define SPI_REG_IMSC       0x14    /**< Interrupt mask set/clear */

/* CR0 bits */
#define SPI_CR0_DSS_8BIT   0x07    /**< Data size select: 8 bits */
#define SPI_CR0_FRF_SPI    0x00    /**< Frame format: SPI */
#define SPI_CR0_SPO        (1U << 6)   /**< Serial clock polarity (CPOL) */
#define SPI_CR0_SPH        (1U << 7)   /**< Serial clock phase (CPHA) */

/* CR1 bits */
#define SPI_CR1_LBM        (1U << 0)   /**< Loop-back mode */
#define SPI_CR1_SSE        (1U << 1)   /**< Synchronous serial enable */
#define SPI_CR1_MS         (1U << 2)   /**< Master/slave (0=master) */
#define SPI_CR1_SOD        (1U << 3)   /**< Slave-mode output disable */

/* SR bits */
#define SPI_SR_TFE         (1U << 0)   /**< TX FIFO empty */
#define SPI_SR_TNF         (1U << 1)   /**< TX FIFO not full */
#define SPI_SR_RNE         (1U << 2)   /**< RX FIFO not empty */
#define SPI_SR_RFF         (1U << 3)   /**< RX FIFO full */
#define SPI_SR_BSY         (1U << 4)   /**< Busy */

/* ── SPI result codes ────────────────────────────────────────────────────── */

typedef enum {
    SPI_OK               = 0,
    SPI_ERR_INVALID_INST  = -1,   /**< Invalid SPI instance (not 0, 1, or 2) */
    SPI_ERR_INVALID_ARG   = -2,   /**< Invalid argument */
    SPI_ERR_NOT_INIT      = -3,   /**< Driver not initialized for this instance */
    SPI_ERR_TIMEOUT       = -4,   /**< Transfer timed out */
    SPI_ERR_TOO_LONG      = -5,   /**< Transfer exceeds SPI_MAX_TRANSFER */
    SPI_ERR_NULL_BUF      = -6,   /**< NULL buffer pointer with non-zero length */
} spi_result_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * spi_init — Initialize an SPI instance as master
 *
 * Configures the SPI peripheral as master with the specified mode,
 * baud rate, and bit order. Asserts chip select high (deselected).
 * Must be called after boot_phase_gpio() has muxed the SPI pins.
 *
 * @instance:  SPI_INSTANCE_0, SPI_INSTANCE_1, or SPI_INSTANCE_2
 * @mode:      SPI_MODE0 through SPI_MODE3
 * @baud_hz:   Desired clock frequency in Hz
 * @bit_order: SPI_MSB_FIRST or SPI_LSB_FIRST
 * Returns SPI_OK on success.
 */
spi_result_t spi_init(uint8_t instance, spi_mode_t mode, uint32_t baud_hz,
                      spi_bit_order_t bit_order);

/**
 * spi_write_blocking — Write data to an SPI slave (blocking)
 *
 * Asserts CS low, writes len bytes, deasserts CS high.
 * Discards received data.
 *
 * @instance: SPI instance
 * @data:     Pointer to data to write
 * @len:      Number of bytes (0–SPI_MAX_TRANSFER)
 * Returns SPI_OK on success.
 */
spi_result_t spi_write_blocking(uint8_t instance, const uint8_t *data, size_t len);

/**
 * spi_read_blocking — Read data from an SPI slave (blocking)
 *
 * Asserts CS low, sends dummy 0xFF bytes while reading len bytes,
 * deasserts CS high.
 *
 * @instance: SPI instance
 * @data:     Pointer to receive buffer
 * @len:      Number of bytes to read
 * Returns SPI_OK on success.
 */
spi_result_t spi_read_blocking(uint8_t instance, uint8_t *data, size_t len);

/**
 * spi_transfer_blocking — Full-duplex SPI transfer (blocking)
 *
 * Asserts CS low, simultaneously writes tx_data and reads rx_data,
 * deasserts CS high. tx_data and rx_data must be the same length.
 *
 * @instance: SPI instance
 * @tx_data:  Pointer to transmit buffer
 * @rx_data:  Pointer to receive buffer (may equal tx_data for in-place)
 * @len:      Number of bytes to transfer
 * Returns SPI_OK on success.
 */
spi_result_t spi_transfer_blocking(uint8_t instance,
                                    const uint8_t *tx_data,
                                    uint8_t *rx_data, size_t len);

/**
 * spi_write_register — Write a byte to a register on an SPI device
 *
 * Sends: [reg_addr | write_bit] [value]
 * The write_bit is OR'd into the register address (common for CC1101: bit7=0 for write).
 *
 * @instance:  SPI instance
 * @reg_addr:  Register address (before write_bit masking)
 * @value:     Byte to write
 * Returns SPI_OK on success.
 */
spi_result_t spi_write_register(uint8_t instance, uint8_t reg_addr, uint8_t value);

/**
 * spi_read_register — Read a byte from a register on an SPI device
 *
 * Sends: [reg_addr | read_bit] [dummy] → receives [discard] [value]
 * The read_bit is OR'd into the register address (common for CC1101: bit7=1 for read).
 *
 * @instance:  SPI instance
 * @reg_addr:  Register address
 * @value:     Pointer to receive the byte
 * Returns SPI_OK on success.
 */
spi_result_t spi_read_register(uint8_t instance, uint8_t reg_addr, uint8_t *value);

/**
 * spi_cs_select — Assert chip select (drive low)
 *
 * Manually assert CS for the given bus. Useful for multi-byte
 * bursts where CS must stay low across multiple transfers.
 *
 * @instance: SPI instance
 * Returns SPI_OK on success.
 */
spi_result_t spi_cs_select(uint8_t instance);

/**
 * spi_cs_deselect — Deassert chip select (drive high)
 *
 * Manually deassert CS for the given bus.
 *
 * @instance: SPI instance
 * Returns SPI_OK on success.
 */
spi_result_t spi_cs_deselect(uint8_t instance);

/**
 * spi_set_baud — Change the clock frequency at runtime
 *
 * @instance: SPI instance
 * @baud_hz:  New baud rate
 * Returns SPI_OK on success.
 */
spi_result_t spi_set_baud(uint8_t instance, uint32_t baud_hz);

/**
 * spi_get_baud — Get the current baud rate
 *
 * @instance: SPI instance
 * Returns baud rate in Hz, or 0 if not initialized.
 */
uint32_t spi_get_baud(uint8_t instance);

/**
 * spi_is_initialized — Check if an SPI instance is initialized
 *
 * @instance: SPI instance
 * Returns true if spi_init() succeeded for this instance.
 */
bool spi_is_initialized(uint8_t instance);

/**
 * spi_deinit — Disable an SPI instance
 *
 * @instance: SPI instance
 * Returns SPI_OK on success.
 */
spi_result_t spi_deinit(uint8_t instance);

/**
 * spi_get_instance_base — Get the register base address for an instance
 *
 * @instance: SPI instance (0, 1, or 2)
 * Returns base address, or 0 on invalid instance.
 */
uint32_t spi_get_instance_base(uint8_t instance);

/**
 * spi_get_instance_cs_pin — Get the chip-select pin for an instance
 *
 * @instance: SPI instance
 * Returns pin number, or 0xFF on invalid instance.
 */
uint8_t spi_get_instance_cs_pin(uint8_t instance);

#endif /* GHOSTWISP_SPI_H */