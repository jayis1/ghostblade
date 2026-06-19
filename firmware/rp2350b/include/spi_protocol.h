/*
 * spi_protocol.h — SPI Bridge Protocol Handler API
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Manages the full-duplex SPI0 slave interface between the RP2350B
 * and the RK3576 host. Frames use a 16-byte header with CRC-64
 * integrity and an optional CRC-32 payload trailer.
 *
 * See docs/spi-protocol-timing.md for frame format and timing details.
 */

#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

#include <stdint.h>

/* ── SPI protocol constants ──────────────────────────────────────────────── */

/** Sync byte — every valid frame starts with 0xAA */
#define SPI_SYNC_BYTE           0xAA

/** Maximum payload length per frame */
#define SPI_MAX_PAYLOAD_LEN     4092

/** Alias: SPI_MAX_PAYLOAD is the short name used in some modules */
#define SPI_MAX_PAYLOAD         SPI_MAX_PAYLOAD_LEN

/** Header size: sync(1) + cmd(1) + len(2) + reserved(4) + crc64(8) */
#define SPI_HEADER_SIZE         16

/** Alias: SPI_HDR_SIZE is the short name used in some modules */
#define SPI_HDR_SIZE            SPI_HEADER_SIZE

/** CRC-32 trailer size */
#define SPI_CRC32_SIZE          4

/** Maximum total frame size */
#define SPI_FRAME_SIZE_MAX      (SPI_HEADER_SIZE + SPI_MAX_PAYLOAD_LEN + SPI_CRC32_SIZE)

/** Minimum frame size (header + CRC-32, zero-length payload) */
#define SPI_FRAME_SIZE_MIN      (SPI_HEADER_SIZE + SPI_CRC32_SIZE)

/* ── Command opcodes ────────────────────────────────────────────────────── */

/** Host → MCU: No operation (keep-alive / status poll) */
#define SPI_CMD_NOP             0xFF

/** Host → MCU: Tune SDR to frequency/bandwidth/gain */
#define SPI_CMD_SDR_TUNE        0x01

/** Host → MCU: Start or stop SDR IQ streaming */
#define SPI_CMD_SDR_STREAM      0x02

/** Host → MCU: Select RF antenna path */
#define SPI_CMD_ANT_SELECT      0x03

/** Host → MCU: Configure CC1101 registers */
#define SPI_CMD_CC1101_CFG      0x04

/** Host → MCU: Perform NFC transaction */
#define SPI_CMD_NFC_TRANSACT    0x05

/** Host → MCU: Request telemetry data */
#define SPI_CMD_TELEMETRY_REQ   0x06

/** MCU → Host: Telemetry response */
#define SPI_CMD_TELEMETRY       0x81

/** MCU → Host: SDR IQ data chunk */
#define SPI_CMD_SDR_IQ_CHUNK    0x82

/* ── SPI frame header structure (16 bytes) ──────────────────────────────── */

struct __attribute__((packed)) spi_frame_header {
    uint8_t  sync;          /**< Must be SPI_SYNC_BYTE (0xAA) */
    uint8_t  cmd;           /**< Command opcode */
    uint16_t len;           /**< Payload length (0–4092, little-endian) */
    uint32_t reserved;      /**< Reserved, must be 0x00000000 */
    uint64_t hdr_crc64;     /**< CRC-64 over bytes 0–7 (reflected ECMA-182) */
};

/* ── Telemetry data structure ───────────────────────────────────────────── */

struct __attribute__((packed)) spi_telemetry_data {
    int16_t  rssi_dbm_x10;     /**< SDR RSSI in dBm × 10 */
    int16_t  temp_c_x10;       /**< Die temperature in °C × 10 */
    uint16_t vbat_mv;          /**< Battery voltage in mV */
    int16_t  cc1101_rssi_x10;  /**< CC1101 RSSI in dBm × 10 */
    uint16_t nfc_field_mv;     /**< NFC field strength in mV */
    uint16_t flags;            /**< Status flags bitmap */
    uint32_t uptime_ms;        /**< MCU uptime in ms */
};

/* ── SDR tune command payload ────────────────────────────────────────────── */

struct __attribute__((packed)) spi_sdr_tune_cmd {
    uint32_t freq_hz;       /**< Center frequency in Hz */
    uint16_t bw_khz;        /**< Bandwidth in kHz */
    uint16_t gain_db_x10;   /**< LNA gain in dB × 10 */
};

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * spi_protocol_init — Initialize SPI0 slave peripheral and protocol state
 *
 * Configures SPI0 as a slave with Mode 0, sets up DMA channels for
 * RX/TX, initializes the CRC lookup tables, and prepares the protocol
 * state machine. Must be called before spi_protocol_process().
 */
void spi_protocol_init(void);

/**
 * spi_protocol_process — Process one SPI transaction if pending
 *
 * Checks if a complete SPI frame has been received via DMA.
 * If so, validates CRC integrity, parses the command, and
 * dispatches to the appropriate handler. Should be called
 * frequently from the main loop.
 *
 * Returns: 0 if no frame processed, positive if frame handled,
 *          negative on error.
 */
int spi_protocol_process(void);

/**
 * spi_protocol_send_telemetry — Send telemetry data to the RK3576 host
 *
 * Constructs a SPI_CMD_TELEMETRY frame with the latest telemetry
 * values and queues it for the next SPI transfer. The host will
 * receive it on the next SPI transaction.
 */
void spi_protocol_send_telemetry(void);

/**
 * spi_protocol_update_telemetry — Update the cached telemetry values
 *
 * @rssi_dbm_x10:   SDR RSSI in dBm × 10 (0 if N/A)
 * @temp_c_x10:     Die temperature in °C × 10
 * @vbat_mv:        Battery voltage in mV
 * @cc_rssi_x10:    CC1101 RSSI in dBm × 10 (0 if N/A)
 * @nfc_field_mv:   NFC field strength in mV (0 if N/A)
 */
void spi_protocol_update_telemetry(uint16_t rssi_dbm_x10,
                                    uint16_t temp_c_x10,
                                    uint16_t vbat_mv,
                                    uint16_t cc_rssi_x10,
                                    uint16_t nfc_field_mv);

/**
 * spi_protocol_tick — Advance the protocol tick counter
 *
 * Increments the internal uptime counter and checks for
 * protocol timeouts. Should be called once per main loop
 * iteration.
 */
void spi_protocol_tick(void);

#endif /* SPI_PROTOCOL_H */