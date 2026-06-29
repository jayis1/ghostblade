/*
 * spi0_isr.c — SPI0 Slave Interrupt Service Routine for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the SPI0 slave interrupt handler and frame
 * assembly state machine for the RP2350B coprocessor. It receives
 * incoming SPI frames from the RK3576 host, validates them, and
 * dispatches them to the appropriate command handler.
 *
 * The SPI0 ISR operates in two phases:
 *   1. Frame assembly: Bytes from the SPI0 RX FIFO are stored in a
 *      linear receive buffer. When the buffer accumulates enough bytes
 *      for a complete frame, the ISR validates CRC-64 (header) and
 *      CRC-32 (payload) and marks the frame as ready.
 *   2. Frame dispatch: The main loop detects ready frames, extracts
 *      the command and payload, and calls the appropriate handler.
 *
 * Thread safety: The ISR writes to spi_rx_buf and advances spi_rx_head.
 * The main loop reads from spi_rx_buf at spi_rx_tail. Since only one
 * writer (ISR) and one reader (main loop) exist, this is a lock-free
 * single-producer single-consumer ring buffer.
 *
 * Reference: docs/spi-protocol-timing.md
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "spi_protocol.h"

/* ========================================================================
 * SPI0 Register Map (RP2350B APB SPI0)
 * ======================================================================== */

#define RP2350B_SPI0_BASE         0x48060000UL

#define SPI_SSPCR0                0x000   /* Control register 0 */
#define SPI_SSPCR1                0x004   /* Control register 1 */
#define SPI_SSPDR                 0x008   /* Data register */
#define SPI_SSPSR                 0x00C   /* Status register */
#define SPI_SSPCPSR               0x010   /* Clock prescale */
#define SPI_SSPIMSC               0x014   /* Interrupt mask */
#define SPI_SSPRIS                0x018   /* Raw interrupt status */
#define SPI_SSPMIS                0x01C   /* Masked interrupt status */
#define SPI_SSPICR                0x020   /* Interrupt clear */
#define SPI_SSPDMACR              0x024   /* DMA control */

/* SSPSR bits */
#define SPI_SSPSR_TFE             (1U << 0)  /* TX FIFO empty */
#define SPI_SSPSR_TNF             (1U << 1)  /* TX FIFO not full */
#define SPI_SSPSR_RNE             (1U << 2)  /* RX FIFO not empty */
#define SPI_SSPSR_RFF             (1U << 3)  /* RX FIFO full */
#define SPI_SSPSR_BSY             (1U << 4)  /* SPI busy */

/* SSPIMSC bits */
#define SPI_SSPIMSC_RXIM          (1U << 2)  /* RX interrupt mask */

/* GPIO pin for interrupt request to host */
#define PIN_INT_REQ               20

/* ========================================================================
 * SPI0 RX Frame Assembly State Machine
 * ======================================================================== */

/*
 * Frame format (from spi_protocol.h):
 *
 *   Byte 0:    Sync byte (0xAA)
 *   Byte 1:    Command byte
 *   Byte 2-3:  Payload length (little-endian, 0-4092)
 *   Byte 4-7:  Reserved (must be 0)
 *   Byte 8-15: CRC-64/ECMA-182 (over bytes 0-7)
 *   Byte 16+:  Payload (0 to 4092 bytes)
 *   Last 4:    CRC-32/ISO-3309 (over payload bytes)
 *
 * Total frame size: 16 (header) + payload_len + 4 (CRC-32)
 * Minimum frame: 20 bytes (NOP with no payload)
 * Maximum frame: 4112 bytes (4092 payload + 20 overhead)
 */

/* Receive buffer size must hold the largest possible frame */
#define SPI_FRAME_BUF_SIZE        (SPI_HDR_SIZE + SPI_MAX_PAYLOAD + SPI_CRC32_SIZE)

/* Frame assembly states */
enum frame_state {
    FRAME_STATE_IDLE = 0,    /* Waiting for sync byte */
    FRAME_STATE_HEADER,      /* Receiving header (bytes 1-15) */
    FRAME_STATE_PAYLOAD,     /* Receiving payload (after header validated) */
    FRAME_STATE_COMPLETE,     /* Complete frame ready for dispatch */
    FRAME_STATE_ERROR,        /* Frame error — discard and resync */
};

/* Frame assembly context */
static struct {
    uint8_t buf[SPI_FRAME_BUF_SIZE];  /* Linear receive buffer */
    uint16_t pos;                       /* Current write position */
    enum frame_state state;             /* Assembly state */
    uint16_t payload_len;              /* Payload length from header */
    volatile bool frame_ready;          /* True when a complete frame is assembled */
} spi0_rx;

/* Transmit response buffer */
static struct {
    uint8_t buf[SPI_FRAME_BUF_SIZE];   /* TX response buffer */
    uint16_t len;                        /* Response length */
    volatile bool response_pending;      /* True when response is queued */
} spi0_tx;

/* ISR statistics */
static struct {
    uint32_t frames_received;
    uint32_t frames_validated;
    uint32_t frames_rejected_sync;
    uint32_t frames_rejected_hdr_crc;
    uint32_t frames_rejected_pay_crc;
    uint32_t frames_rejected_len;
    uint32_t rx_overflows;
    uint32_t bytes_received;
} spi0_stats;

/* ========================================================================
 * CRC Lookup Tables (initialized from spi_protocol.c)
 * ======================================================================== */

extern void crc64_init_table(void);
extern void crc32_init_table(void);
extern uint64_t crc64_compute(const uint8_t *data, uint32_t len);
extern uint32_t crc32_compute(const uint8_t *data, uint32_t len);

/* ========================================================================
 * Host Interrupt Request (INT_REQ) Control
 * ======================================================================== */

#define RP2350B_GPIO_BASE         0x400D0000UL
#define GPIO_OUT_OFFSET           0x00
#define GPIO_OE_OFFSET            0x10

/**
 * spi0_assert_int_req — Assert interrupt request to RK3576 host
 *
 * Drives PIN_INT_REQ low (active-low) to signal the host that
 * the MCU has a response frame ready to be read.
 */
static void spi0_assert_int_req(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + GPIO_OUT_OFFSET);
    *gpio_out &= ~(1UL << PIN_INT_REQ);  /* Active-low: drive LOW */
}

/**
 * spi0_deassert_int_req — Deassert interrupt request to RK3576 host
 *
 * Releases PIN_INT_REQ (drives HIGH) after the host has read
 * the response frame.
 */
static void spi0_deassert_int_req(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + GPIO_OUT_OFFSET);
    *gpio_out |= (1UL << PIN_INT_REQ);   /* Active-low: drive HIGH (deasserted) */
}

/* ========================================================================
 * Frame Validation Helpers
 * ======================================================================== */

/**
 * validate_sync_byte — Check if a byte is a valid sync marker
 *
 * @byte: The byte to check
 * Returns: true if byte equals SPI_SYNC_BYTE (0xAA)
 */
static bool validate_sync_byte(uint8_t byte) {
    return byte == SPI_SYNC_BYTE;
}

/**
 * validate_header_crc64 — Validate CRC-64 over the frame header
 *
 * @buf: Buffer containing at least SPI_HDR_SIZE bytes
 * Returns: true if CRC-64 is valid
 */
static bool validate_header_crc64(const uint8_t *buf) {
    /* CRC-64 covers bytes 0-7 (sync + command + length + reserved) */
    uint64_t computed = crc64_compute(buf, 8);

    /* Extract expected CRC-64 from bytes 8-15 (little-endian) */
    uint64_t expected = (uint64_t)buf[8]        |
                        ((uint64_t)buf[9] << 8)   |
                        ((uint64_t)buf[10] << 16)  |
                        ((uint64_t)buf[11] << 24)  |
                        ((uint64_t)buf[12] << 32)  |
                        ((uint64_t)buf[13] << 40)  |
                        ((uint64_t)buf[14] << 48)  |
                        ((uint64_t)buf[15] << 56);

    return computed == expected;
}

/**
 * validate_payload_crc32 — Validate CRC-32 over the frame payload
 *
 * @buf:        Buffer containing the complete frame
 * @payload_len: Length of the payload (must be <= SPI_MAX_PAYLOAD)
 * Returns: true if CRC-32 is valid
 */
static bool validate_payload_crc32(const uint8_t *buf, uint16_t payload_len) {
    /* CRC-32 covers payload bytes only (not header) */
    uint32_t computed;

    if (payload_len == 0) {
        /* Zero-length payload: CRC-32 is computed over empty data */
        computed = crc32_compute(buf, 0);
    } else {
        computed = crc32_compute(&buf[SPI_HDR_SIZE], payload_len);
    }

    /* Extract expected CRC-32 from last 4 bytes (little-endian) */
    uint32_t offset = SPI_HDR_SIZE + payload_len;
    uint32_t expected = (uint32_t)buf[offset]       |
                        ((uint32_t)buf[offset + 1] << 8)  |
                        ((uint32_t)buf[offset + 2] << 16) |
                        ((uint32_t)buf[offset + 3] << 24);

    return computed == expected;
}

/**
 * extract_payload_length — Extract payload length from header bytes
 *
 * @buf: Buffer containing at least 4 bytes (bytes 2-3 are length)
 * Returns: Payload length (0-4092)
 */
static uint16_t extract_payload_length(const uint8_t *buf) {
    return (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
}

/* ========================================================================
 * SPI0 ISR — Byte-by-byte Frame Assembly
 * ======================================================================== */

/**
 * spi0_process_byte — Process one received byte through the state machine
 *
 * This function implements the frame assembly state machine. It is called
 * from the SPI0 ISR for each byte received from the RK3576 host.
 *
 * State transitions:
 *   IDLE → HEADER:     Sync byte (0xAA) received
 *   HEADER → PAYLOAD:  Header complete and CRC-64 valid, payload_len > 0
 *   HEADER → COMPLETE: Header complete and CRC-64 valid, payload_len == 0
 *   HEADER → ERROR:     CRC-64 validation failed
 *   PAYLOAD → COMPLETE: Payload + CRC-32 received
 *   PAYLOAD → ERROR:   CRC-32 validation failed
 *   ERROR → IDLE:      Resync by looking for next sync byte
 *
 * @byte: The received byte to process
 */
static void spi0_process_byte(uint8_t byte) {
    spi0_stats.bytes_received++;

    switch (spi0_rx.state) {
    case FRAME_STATE_IDLE:
        /* Looking for sync byte */
        if (validate_sync_byte(byte)) {
            spi0_rx.buf[0] = byte;
            spi0_rx.pos = 1;
            spi0_rx.state = FRAME_STATE_HEADER;
        } else {
            spi0_stats.frames_rejected_sync++;
        }
        break;

    case FRAME_STATE_HEADER:
        /* Accumulating header bytes (1-15) */
        if (spi0_rx.pos < SPI_HDR_SIZE) {
            spi0_rx.buf[spi0_rx.pos++] = byte;

            if (spi0_rx.pos == SPI_HDR_SIZE) {
                /* Header complete — validate CRC-64 */
                if (!validate_header_crc64(spi0_rx.buf)) {
                    spi0_stats.frames_rejected_hdr_crc++;
                    /* Try to resync: look for sync byte in current buffer */
                    spi0_rx.state = FRAME_STATE_ERROR;
                    break;
                }

                /* Header is valid — extract payload length */
                spi0_rx.payload_len = extract_payload_length(spi0_rx.buf);

                /* Validate payload length */
                if (spi0_rx.payload_len > SPI_MAX_PAYLOAD) {
                    spi0_stats.frames_rejected_len++;
                    spi0_rx.state = FRAME_STATE_ERROR;
                    break;
                }

                if (spi0_rx.payload_len == 0) {
                    /* No payload — check CRC-32 (trailer only) */
                    /* Need 4 more bytes for CRC-32 */
                    spi0_rx.state = FRAME_STATE_PAYLOAD;
                    /* payload_len == 0 means we need just the 4-byte CRC-32 */
                } else {
                    spi0_rx.state = FRAME_STATE_PAYLOAD;
                }
            }
        }
        break;

    case FRAME_STATE_PAYLOAD:
        /* Accumulating payload + CRC-32 bytes */
        if (spi0_rx.pos < SPI_HDR_SIZE + spi0_rx.payload_len + SPI_CRC32_SIZE) {
            spi0_rx.buf[spi0_rx.pos++] = byte;

            /* Check if complete frame received */
            if (spi0_rx.pos == SPI_HDR_SIZE + spi0_rx.payload_len + SPI_CRC32_SIZE) {
                /* Validate payload CRC-32 */
                if (validate_payload_crc32(spi0_rx.buf, spi0_rx.payload_len)) {
                    spi0_stats.frames_validated++;
                    spi0_stats.frames_received++;
                    /* Ensure buffer writes are visible before setting
                     * frame_ready flag that the main loop polls. */
                    __asm__ volatile ("dmb" ::: "memory");
                    spi0_rx.state = FRAME_STATE_COMPLETE;
                    spi0_rx.frame_ready = true;
                } else {
                    spi0_stats.frames_rejected_pay_crc++;
                    spi0_rx.state = FRAME_STATE_ERROR;
                }
            }
        } else {
            /* Buffer overflow — should never happen if payload_len is validated */
            spi0_rx.state = FRAME_STATE_ERROR;
        }
        break;

    case FRAME_STATE_COMPLETE:
        /*
         * Previous frame not yet consumed by main loop.
         * Discard this byte. The main loop should have processed
         * the frame before the next SPI transaction.
         */
        spi0_stats.rx_overflows++;
        break;

    case FRAME_STATE_ERROR:
        /*
         * Error state: look for sync byte to resync.
         * This allows recovery from corrupted frames without
         * requiring a bus reset.
         */
        if (validate_sync_byte(byte)) {
            spi0_rx.buf[0] = byte;
            spi0_rx.pos = 1;
            spi0_rx.state = FRAME_STATE_HEADER;
        }
        break;
    }
}

/**
 * spi0_handler — SPI0 interrupt handler
 *
 * Called when the SPI0 RX FIFO has data. Reads all available
 * bytes and processes them through the frame assembly state
 * machine.
 */
void spi0_handler(void) {
    volatile uint32_t *spi = (volatile uint32_t *)RP2350B_SPI0_BASE;
    uint32_t max_iterations = 256;  /* Safety: limit bytes processed per ISR invocation */

    /* Read all available data from SPI0 RX FIFO.
     * The max_iterations limit prevents an infinite loop if the RNE
     * bit is stuck due to a hardware fault. The RP2350B SSP has a
     * 16-entry RX FIFO, so 256 is far more than needed in normal
     * operation but provides headroom for burst scenarios. */
    while (spi[SPI_SSPSR / 4] & SPI_SSPSR_RNE) {
        uint8_t byte = (uint8_t)(spi[SPI_SSPDR / 4] & 0xFF);
        spi0_process_byte(byte);

        if (--max_iterations == 0) {
            /* Safety bail-out: read too many bytes in one ISR invocation.
             * This should never happen in normal operation. */
            break;
        }
    }

    /* Clear all pending SPI0 interrupts */
    spi[SPI_SSPICR / 4] = spi[SPI_SSPRIS / 4];

    /* If we have a response pending, load TX FIFO */
    if (spi0_tx.response_pending) {
        /* Write response bytes to SPI0 TX FIFO for full-duplex transfer.
         * The host will clock out the response during the next SPI
         * transaction. We load up to 16 bytes (TX FIFO depth) at a time.
         *
         * Limit the loop to TX FIFO depth (16 entries on RP2350B SSP)
         * to prevent a stuck TNF bit from causing an infinite loop in
         * the ISR. If TNF stays asserted beyond the FIFO size, the
         * peripheral is malfunctioning and we bail out defensively. */
        uint16_t tx_pos = 0;
        while (tx_pos < spi0_tx.len && tx_pos < 16 &&
               (spi[SPI_SSPSR / 4] & SPI_SSPSR_TNF)) {
            spi[SPI_SSPDR / 4] = spi0_tx.buf[tx_pos++];
        }
        if (tx_pos >= spi0_tx.len) {
            spi0_tx.response_pending = false;
            spi0_deassert_int_req();
        }
    }
}

/* ========================================================================
 * Public API — Frame Dispatch
 * ======================================================================== */

/**
 * spi0_rx_get_frame — Get the next assembled frame for dispatch
 *
 * Called from the main loop to check if a complete frame has been
 * assembled by the ISR. If a frame is ready, the caller can extract
 * the command and payload using validate_spi_frame().
 *
 * @buf:   Output: pointer to the frame data (within ISR buffer)
 * @len:   Output: length of the frame in bytes
 *
 * Returns: true if a frame is available
 */
bool spi0_rx_get_frame(const uint8_t **buf, uint16_t *len) {
    if (!spi0_rx.frame_ready)
        return false;

    /* Ensure we see the ISR's data before returning the buffer.
     * The ISR sets frame_ready after writing the buffer, but
     * without a barrier we might see stale data. */
    __asm__ volatile ("dmb" ::: "memory");

    *buf = spi0_rx.buf;
    *len = spi0_rx.pos;

    return true;
}

/**
 * spi0_rx_release_frame — Release the current frame and reset state machine
 *
 * Called after the main loop has finished processing a frame.
 * Resets the assembly state machine to IDLE so the ISR can
 * begin receiving the next frame.
 */
void spi0_rx_release_frame(void) {
    /* Securely wipe the frame buffer to prevent residual sensitive
     * data (NFC keys, RF config) from persisting in SRAM. */
    volatile uint8_t *p = (volatile uint8_t *)spi0_rx.buf;
    for (uint16_t i = 0; i < spi0_rx.pos; i++)
        p[i] = 0;

    /* Ensure wipe completes before resetting state (visible to ISR) */
    __asm__ volatile ("dmb" ::: "memory");

    spi0_rx.frame_ready = false;
    spi0_rx.pos = 0;
    spi0_rx.payload_len = 0;
    spi0_rx.state = FRAME_STATE_IDLE;
}

/**
 * spi0_tx_queue_response — Queue a response frame for transmission
 *
 * @frame: Pointer to the response frame data
 * @len:   Length of the response frame in bytes
 *
 * The response is loaded into the SPI0 TX FIFO on the next
 * SPI transaction. An INT_REQ signal is asserted to notify
 * the host that data is available.
 */
void spi0_tx_queue_response(const uint8_t *frame, uint16_t len) {
    if (len > SPI_FRAME_BUF_SIZE)
        return;

    memcpy(spi0_tx.buf, frame, len);
    spi0_tx.len = len;
    /* Ensure TX data is visible before setting response_pending flag.
     * Without this barrier, the ISR could start reading the TX buffer
     * before the memcpy completes on ARM Cortex-M33 with data cache. */
    __asm__ volatile ("dmb" ::: "memory");
    spi0_tx.response_pending = true;

    /* Assert INT_REQ to notify host */
    spi0_assert_int_req();
}

/**
 * spi0_rx_reset — Reset the SPI0 receive state machine
 *
 * Clears all buffers and resets the state machine to IDLE.
 * Called on initialization and after error recovery.
 */
void spi0_rx_reset(void) {
    memset(&spi0_rx, 0, sizeof(spi0_rx));
    spi0_rx.state = FRAME_STATE_IDLE;
}

/**
 * spi0_tx_reset — Reset the SPI0 transmit state
 *
 * Clears the response buffer and deasserts INT_REQ.
 */
void spi0_tx_reset(void) {
    memset(&spi0_tx, 0, sizeof(spi0_tx));
    spi0_deassert_int_req();
}

/**
 * spi0_get_stats — Get SPI0 communication statistics
 *
 * @frames_received:      Total frames received (byte count triggers)
 * @frames_validated:     Frames that passed all validation
 * @frames_rejected_sync: Frames rejected for bad sync byte
 * @frames_rejected_hdr:  Frames rejected for bad header CRC-64
 * @frames_rejected_pay:  Frames rejected for bad payload CRC-32
 * @frames_rejected_len:  Frames rejected for invalid payload length
 * @rx_overflows:         Bytes dropped due to frame not consumed
 * @bytes_received:       Total bytes received from SPI0
 */
void spi0_get_stats(uint32_t *frames_received,
                     uint32_t *frames_validated,
                     uint32_t *frames_rejected_sync,
                     uint32_t *frames_rejected_hdr,
                     uint32_t *frames_rejected_pay,
                     uint32_t *frames_rejected_len,
                     uint32_t *rx_overflows,
                     uint32_t *bytes_received) {
    if (frames_received)      *frames_received      = spi0_stats.frames_received;
    if (frames_validated)     *frames_validated     = spi0_stats.frames_validated;
    if (frames_rejected_sync) *frames_rejected_sync = spi0_stats.frames_rejected_sync;
    if (frames_rejected_hdr)  *frames_rejected_hdr  = spi0_stats.frames_rejected_hdr_crc;
    if (frames_rejected_pay)  *frames_rejected_pay  = spi0_stats.frames_rejected_pay_crc;
    if (frames_rejected_len)  *frames_rejected_len  = spi0_stats.frames_rejected_len;
    if (rx_overflows)         *rx_overflows         = spi0_stats.rx_overflows;
    if (bytes_received)       *bytes_received       = spi0_stats.bytes_received;
}