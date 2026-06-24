/*
 * spi_protocol.c — SPI Bridge Protocol Handler for RP2350B
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file implements the SPI0 slave protocol handler that receives,
 * validates, and executes commands from the RK3576 host processor.
 *
 * The protocol uses framed messages with CRC-64 header integrity and
 * CRC-32 payload integrity. This handler:
 *   1. Accumulates bytes from the SPI0 RX ring buffer into frames
 *   2. Validates sync byte, header CRC-64, and payload CRC-32
 *   3. Dispatches validated commands to the appropriate handler
 *   4. Builds response frames and queues them for SPI0 TX
 *
 * Usage: Call spi_protocol_init() after rp2350b_init().
 *        Call spi_protocol_process() from the main loop.
 *        SPI0 ISR feeds the rx_ring buffer (see rp2350b_init.c).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "spi_protocol.h"

/* Forward declarations from other modules */
extern void watchdog_kick(void);

/* ── Memory barriers for lock-free ring buffer ────────────────────────────
 *
 * The SPI0 RX and TX ring buffers are accessed from both ISR context
 * (rx_head/tx_tail) and main-loop context (rx_tail/tx_head). We use
 * volatile qualifiers for the shared indices, but on ARM Cortex-M33
 * with data cache, we must ensure ordering of reads/writes to avoid
 * stale data. Use DMB (Data Memory Barrier) after ISR writes and
 * before main-loop reads to ensure visibility.
 */
static inline void dmb(void) {
    __asm__ volatile ("dmb" ::: "memory");
}

/**
 * secure_wipe — Securely clear sensitive data from memory
 *
 * Uses volatile pointer to prevent compiler optimization
 * from removing the memset.
 */
static void secure_wipe(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--)
        *p++ = 0;
}

/* ========================================================================
 * Protocol Constants (must match apex_bridge_regs.h on RK3576 side)
 * ======================================================================== */

/* Command opcodes — Host to MCU */
#define CMD_NOP                 0xFF
#define CMD_SDR_TUNE            0x01
#define CMD_SDR_STREAM          0x02
#define CMD_ANT_SELECT          0x03
#define CMD_CC1101_CFG          0x04
#define CMD_NFC_TRANSACT        0x05

/* Command opcodes — MCU to Host */
#define CMD_TELEMETRY           0x81
#define CMD_SDR_IQ_CHUNK        0x82

/* ========================================================================
 * SPI Frame Header Structure (16 bytes, little-endian)
 * ======================================================================== */

struct spi_frame_header {
    uint8_t  sync;           /* 0x00: Sync byte, always 0xAA */
    uint8_t  cmd;            /* 0x01: Command opcode */
    uint16_t len;            /* 0x02-03: Payload length (0-4092), little-endian */
    uint32_t reserved;       /* 0x04-07: Reserved, must be 0 */
    uint64_t hdr_crc;        /* 0x08-0F: CRC-64 over bytes 0-7 */
} __attribute__((packed));

/* ========================================================================
 * SDR Tune Command Payload (8 bytes)
 * ======================================================================== */

struct sdr_tune_cmd {
    uint32_t freq_hz;        /* Frequency in Hz (100 kHz – 3.8 GHz) */
    uint16_t bw_khz;         /* Bandwidth in kHz */
    uint16_t gain_db_x10;    /* LNA gain in dB × 10 */
} __attribute__((packed));

/* ========================================================================
 * CC1101 Config Command Payload (variable)
 * ======================================================================== */

struct cc1101_cfg_cmd {
    uint8_t  reg_addr;       /* CC1101 register start address */
    uint8_t  reg_len;        /* Number of consecutive registers */
    uint8_t  data[];         /* Register data (reg_len bytes) */
} __attribute__((packed));

/* ========================================================================
 * NFC Transaction Command Payload (variable)
 * ======================================================================== */

struct nfc_transact_cmd {
    uint8_t  cmd;            /* NFC command (ISO 14443 A/B, etc.) */
    uint8_t  flags;          /* Transaction flags */
    uint16_t data_len;       /* TX data length, little-endian */
    uint8_t  data[];         /* TX data + RX buffer hint */
} __attribute__((packed));

/* ========================================================================
 * Telemetry Response Payload (16 bytes)
 * ======================================================================== */

struct telemetry_payload {
    uint16_t rssi_dbm_x10;       /* SDR RSSI in dBm × 10 */
    uint16_t temp_c_x10;         /* MCU die temperature in °C × 10 */
    uint16_t vbat_mv;            /* Battery voltage in mV */
    uint16_t cc1101_rssi_x10;    /* CC1101 RSSI in dBm × 10 */
    uint16_t nfc_field_mv;       /* NFC field strength in mV */
    uint16_t flags;              /* Status flags bitmap */
    uint32_t uptime_ms;          /* MCU uptime in milliseconds */
} __attribute__((packed));

/* Telemetry flags */
#define TELEM_FLAG_SDR_RX_ACTIVE     (1 << 0)
#define TELEM_FLAG_SDR_TX_ACTIVE     (1 << 1)
#define TELEM_FLAG_CC1101_RX         (1 << 2)
#define TELEM_FLAG_CC1101_TX         (1 << 3)
#define TELEM_FLAG_NFC_ACTIVE        (1 << 4)
#define TELEM_FLAG_NFC_TAG_PRESENT   (1 << 5)
#define TELEM_FLAG_OVERTEMP          (1 << 6)
#define TELEM_FLAG_LOW_BATTERY       (1 << 7)
#define TELEM_FLAG_SPI_ERR           (1 << 8)
#define TELEM_FLAG_DMA_ERR           (1 << 9)

/* ========================================================================
 * CRC Computation (must match kernel driver)
 * ======================================================================== */

/* CRC-64 using polynomial 0x42F0E1EBA9EA3693 (ECMA-182) */
static uint64_t crc64_table[256];
static int crc64_initialized = 0;

static void crc64_init(void) {
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc64_table[i] = crc;
    }
    crc64_initialized = 1;
}

static uint64_t crc64_compute(const uint8_t *data, uint32_t len) {
    if (!crc64_initialized)
        crc64_init();
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc64_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/* CRC-32 using polynomial 0xEDB88320 (ISO 3309) */
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void crc32_init(void) {
    const uint32_t poly = 0xEDB88320UL;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    if (!crc32_initialized)
        crc32_init();
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* TX Response Ring Buffer (protocol handler writes, SPI0 ISR reads)
 *
 * Both ring sizes MUST be powers of 2 for the masking arithmetic
 * (head/tail wrapping) to work correctly. The _Static_assert
 * constraints below enforce this at compile time.
 */
#define RX_RING_SIZE  8192
#define TX_RING_SIZE  4096

_Static_assert((RX_RING_SIZE & (RX_RING_SIZE - 1)) == 0,
               "RX_RING_SIZE must be a power of 2");
_Static_assert((TX_RING_SIZE & (TX_RING_SIZE - 1)) == 0,
               "TX_RING_SIZE must be a power of 2");

static uint8_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head = 0;  /* ISR writes here */
static volatile uint32_t rx_tail = 0;  /* Protocol handler reads here */

/* The SPI0 ISR feeds bytes from the SPI0 RX FIFO into the ring buffer
 * (spi_rx_buf/spi_rx_head/spi_rx_tail). The protocol handler drains
 * bytes from that buffer in spi_protocol_process().
 * TX responses are written to the local tx_ring buffer and read by the
 * ISR during SPI transactions.
 *
 * The SPI0 ISR does NOT use the tx_ring/tx_head/tx_tail defined here;
 * responses are sent by the host polling the SPI bus or via INT_REQ
 * assertion followed by the host initiating a transfer. */

static uint8_t tx_ring[TX_RING_SIZE];
static volatile uint32_t tx_head = 0;  /* Protocol handler writes here */
static volatile uint32_t tx_tail = 0;  /* ISR reads from here */

/* ========================================================================
 * Frame Assembly State Machine
 * ======================================================================== */

enum rx_state {
    RX_STATE_IDLE = 0,
    RX_STATE_HEADER,     /* Accumulating header bytes (0-15) */
    RX_STATE_PAYLOAD,    /* Accumulating payload bytes */
    RX_STATE_CRC32,      /* Accumulating CRC-32 trailer */
    RX_STATE_COMPLETE,    /* Frame complete, ready for dispatch */
    RX_STATE_ERROR        /* Frame error, discard */
};

static struct {
    enum rx_state state;
    uint8_t  hdr_buf[SPI_HDR_SIZE];  /* Header assembly buffer */
    uint8_t  payload_buf[SPI_MAX_PAYLOAD]; /* Payload assembly buffer */
    uint8_t  crc32_buf[SPI_CRC32_SIZE];    /* CRC-32 assembly buffer */
    uint32_t hdr_idx;       /* Bytes accumulated into hdr_buf */
    uint32_t payload_idx;   /* Bytes accumulated into payload_buf */
    uint32_t crc32_idx;     /* Bytes accumulated into crc32_buf */
    uint16_t expected_payload_len;  /* From header LEN field */
    uint32_t frames_received;
    uint32_t frames_crc_error;
    uint32_t frames_sync_error;
    uint32_t frames_dispatched;
} rx_ctx;

/* ========================================================================
 * Protocol Statistics
 * ======================================================================== */

static struct {
    uint32_t cmd_nop_rx;
    uint32_t cmd_sdr_tune_rx;
    uint32_t cmd_sdr_stream_rx;
    uint32_t cmd_ant_select_rx;
    uint32_t cmd_cc1101_cfg_rx;
    uint32_t cmd_nfc_transact_rx;
    uint32_t telemetry_sent;
    uint32_t iq_chunks_sent;
    uint32_t tx_overruns;
} proto_stats;

/* ========================================================================
 * Current Device State
 * ======================================================================== */

static struct {
    bool     sdr_streaming;       /* SDR IQ streaming active */
    bool     sdr_tx_enabled;       /* SDR TX path enabled */
    bool     sdr_rx_enabled;       /* SDR RX path enabled */
    bool     cc1101_rx_active;     /* CC1101 receiving */
    bool     cc1101_tx_active;     /* CC1101 transmitting */
    bool     nfc_active;           /* NFC polling active */
    bool     nfc_tag_present;      /* NFC tag detected */
    uint32_t uptime_ms;            /* System uptime counter */
    int16_t  last_rssi_dbm_x10;   /* Last SDR RSSI (signed dBm × 10) */
    uint16_t last_vbat_mv;         /* Last battery voltage */
    int16_t  last_temp_c_x10;      /* Last die temperature (signed °C × 10) */
    int16_t  last_cc_rssi_x10;     /* Last CC1101 RSSI (signed dBm × 10) */
    uint16_t last_nfc_field_mv;    /* Last NFC field strength */
} device_state;

/* ========================================================================
 * Forward Declarations (implemented in rp2350b_init.c or other modules)
 * ======================================================================== */

extern void apex_antenna_select(uint8_t ant_id);
extern void apex_cc1101_write_burst(uint8_t addr, const uint8_t *data, uint8_t len);
extern void apex_cc1101_read_burst(uint8_t addr, uint8_t *data, uint8_t len);
extern void apex_nfc_write_register(uint8_t addr, uint8_t val);
extern uint8_t apex_nfc_read_register(uint8_t addr);
extern void apex_sdr_reset_assert(void);
extern void apex_sdr_reset_release(void);
extern void apex_sdr_tx_enable(bool enable);
extern void apex_sdr_rx_enable(bool enable);
extern void apex_sdr_lna_enable(bool enable);

/* External SPI0 RX ring buffer from rp2350b_init.c */
extern uint8_t spi_rx_buf[];
extern volatile uint32_t spi_rx_head;
extern volatile uint32_t spi_rx_tail;
#define SPI_RX_BUF_SIZE 8192

/* SPI_RX_BUF_SIZE must be a power of 2 for the ring buffer masking
 * in spi_protocol_process() to work correctly. */
_Static_assert((SPI_RX_BUF_SIZE & (SPI_RX_BUF_SIZE - 1)) == 0,
               "SPI_RX_BUF_SIZE must be a power of 2");

/* External GPIO base for INT_REQ */
#define RP2350B_GPIO_BASE     0x400D0000UL
#define PIN_INT_REQ           20

/* ========================================================================
 * Helper: Little-endian unaligned access
 * ======================================================================== */

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static void write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void write_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

/* ========================================================================
 * INT_REQ Signal Control
 * ======================================================================== */

static void int_req_assert(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out &= ~(1UL << PIN_INT_REQ);  /* Active-low: drive LOW */
}

static void int_req_deassert(void) {
    volatile uint32_t *gpio_out = (volatile uint32_t *)(RP2350B_GPIO_BASE + 0x00);
    *gpio_out |= (1UL << PIN_INT_REQ);   /* Active-low: drive HIGH */
}

/* ========================================================================
 * Frame Builder (for responses to RK3576)
 * ======================================================================== */

/**
 * build_response_frame — Build a response frame in the TX ring buffer
 *
 * @cmd: Response command opcode (CMD_TELEMETRY, CMD_SDR_IQ_CHUNK, etc.)
 * @payload: Payload data (may be NULL if payload_len == 0)
 * @payload_len: Length of payload in bytes
 *
 * Returns: 0 on success, -1 on error (TX ring full)
 */
static int build_response_frame(uint8_t cmd, const uint8_t *payload,
                                 uint16_t payload_len)
{
    uint8_t frame[SPI_FRAME_SIZE_MAX];
    int total_len;
    uint64_t crc64_val;
    uint32_t crc32_val;

    if (payload_len > SPI_MAX_PAYLOAD)
        return -1;

    /* Build header */
    frame[0] = SPI_SYNC_BYTE;
    frame[1] = cmd;
    write_le16(&frame[2], payload_len);
    write_le32(&frame[4], 0);  /* reserved */

    /* Compute header CRC-64 over bytes 0-7 */
    crc64_val = crc64_compute(frame, 8);
    write_le64(&frame[8], crc64_val);

    /* Copy payload */
    if (payload_len > 0 && payload != NULL)
        memcpy(&frame[SPI_HDR_SIZE], payload, payload_len);

    /* Compute payload CRC-32 */
    crc32_val = crc32_compute(&frame[SPI_HDR_SIZE], payload_len);
    write_le32(&frame[SPI_HDR_SIZE + payload_len], crc32_val);

    total_len = SPI_HDR_SIZE + payload_len + SPI_CRC32_SIZE;

    /* Check if TX ring has space.
     * Available space = TX_RING_SIZE - used - 1, where used = (tx_head - tx_tail) mod TX_RING_SIZE.
     * The mask arithmetic is safe because TX_RING_SIZE is a power of 2 (verified at compile time). */
    uint32_t used = (tx_head - tx_tail) & (TX_RING_SIZE - 1);
    uint32_t avail = TX_RING_SIZE - used - 1;
    if ((uint32_t)total_len > avail) {
        proto_stats.tx_overruns++;
        return -1;
    }

    /* Copy frame to TX ring */
    for (int i = 0; i < total_len; i++) {
        tx_ring[tx_head] = frame[i];
        tx_head = (tx_head + 1) & (TX_RING_SIZE - 1);
    }

    /* Assert INT_REQ to signal RK3576 that response data is available.
     * Ensure TX ring data is visible before asserting the interrupt. */
    dmb();
    int_req_assert();

    return 0;
}

/* ========================================================================
 * Command Handlers
 * ======================================================================== */

/**
 * handle_cmd_nop — Handle NOP/keepalive command
 *
 * Sends a NOP acknowledgment back to the host.
 */
static void handle_cmd_nop(void) {
    proto_stats.cmd_nop_rx++;
    /* NOP requires no action; optionally send telemetry as response */
}

/**
 * handle_cmd_sdr_tune — Handle SDR frequency tuning command
 *
 * Payload format: struct sdr_tune_cmd (8 bytes)
 *   - freq_hz:   Target frequency in Hz
 *   - bw_khz:    Bandwidth in kHz
 *   - gain_db_x10: LNA gain in dB × 10
 */
static void handle_cmd_sdr_tune(const uint8_t *payload, uint16_t len) {
    struct sdr_tune_cmd tune;

    proto_stats.cmd_sdr_tune_rx++;

    if (len < sizeof(tune))
        return;

    memcpy(&tune, payload, sizeof(tune));

    /* Validate frequency range (100 kHz – 3.8 GHz) */
    if (tune.freq_hz < 100000UL || tune.freq_hz > 3800000000UL)
        return;

    /* Configure LMS7002M via SPI1:
     * 1. Write frequency to PLL registers
     * 2. Set bandwidth filter
     * 3. Set LNA gain
     *
     * Detailed LMS7002M register programming is implemented in
     * lms7002m_driver.c (future module). For now, store parameters
     * and set GPIO enables.
     */
    (void)tune;  /* Will be used when LMS7002M driver is integrated */
}

/**
 * handle_cmd_sdr_stream — Handle SDR IQ stream start/stop command
 *
 * Payload: 1 byte (0 = stop, 1 = start)
 */
static void handle_cmd_sdr_stream(const uint8_t *payload, uint16_t len) {
    proto_stats.cmd_sdr_stream_rx++;

    if (len < 1)
        return;

    uint8_t enable = payload[0];

    if (enable && !device_state.sdr_streaming) {
        /* Start SDR IQ streaming */
        device_state.sdr_streaming = true;
        device_state.sdr_rx_enabled = true;
        apex_sdr_rx_enable(true);
        apex_sdr_lna_enable(true);
    } else if (!enable && device_state.sdr_streaming) {
        /* Stop SDR IQ streaming */
        device_state.sdr_streaming = false;
        device_state.sdr_rx_enabled = false;
        apex_sdr_rx_enable(false);
        apex_sdr_lna_enable(false);
    }
}

/**
 * handle_cmd_ant_select — Handle antenna selection command
 *
 * Payload: 1 byte (0=MIMO_TX, 1=MIMO_RX, 2=SUBGHZ, 3=TERMINATED)
 */
static void handle_cmd_ant_select(const uint8_t *payload, uint16_t len) {
    proto_stats.cmd_ant_select_rx++;

    if (len < 1)
        return;

    uint8_t ant_id = payload[0];
    if (ant_id > 3)
        return;

    apex_antenna_select(ant_id);
}

/**
 * handle_cmd_cc1101_cfg — Handle CC1101 register configuration command
 *
 * Payload format: struct cc1101_cfg_cmd (variable length)
 *   - reg_addr: CC1101 register start address
 *   - reg_len:   Number of consecutive registers
 *   - data[]:    Register values
 */
static void handle_cmd_cc1101_cfg(const uint8_t *payload, uint16_t len) {
    const struct cc1101_cfg_cmd *cfg;
    proto_stats.cmd_cc1101_cfg_rx++;

    if (len < 2)  /* At minimum: addr + len */
        return;

    cfg = (struct cc1101_cfg_cmd *)payload;

    /* Validate register address range (0x00-0x2E config, 0x30-0x3D command/status).
     * Address 0x2F is reserved/invalid on CC1101. */
    if (cfg->reg_addr > 0x3D || cfg->reg_addr == 0x2F)
        return;

    /* Validate reg_len does not exceed remaining payload */
    if (len < (uint16_t)(2 + cfg->reg_len))
        return;

    /* Prevent burst write from wrapping past the register space boundary.
     * CC1101 burst writes auto-increment the address, so we must ensure
     * reg_addr + reg_len stays within a valid range. */
    if (cfg->reg_addr <= 0x2E && (uint16_t)cfg->reg_addr + cfg->reg_len > 0x2F)
        return;
    if (cfg->reg_addr >= 0x30 && (uint16_t)cfg->reg_addr + cfg->reg_len > 0x3E)
        return;

    apex_cc1101_write_burst(cfg->reg_addr, cfg->data, cfg->reg_len);
}

/**
 * handle_cmd_nfc_transact — Handle NFC transaction command
 *
 * Payload format: struct nfc_transact_cmd (variable length)
 *   - cmd:      NFC command
 *   - flags:    Transaction flags
 *   - data_len: TX data length
 *   - data[]:   TX data
 */
static void handle_cmd_nfc_transact(const uint8_t *payload, uint16_t len) {
    struct nfc_transact_cmd *nfc;
    proto_stats.cmd_nfc_transact_rx++;

    if (len < 4)  /* At minimum: cmd + flags + data_len */
        return;

    nfc = (struct nfc_transact_cmd *)payload;
    uint16_t data_len = read_le16((const uint8_t *)&nfc->data_len);

    /* Cap data_len to prevent buffer overrun (max 256 bytes) */
    if (data_len > 256)
        return;

    if (len < (uint16_t)(4 + data_len))
        return;

    /* ST25R3916 transaction implementation:
     * 1. Write command to ST25R3916 via SPI2
     * 2. Transmit data via antenna driver
     * 3. Read response via SPI2
     * 4. Build response frame with NFC data
     *
     * Full implementation in nfc_driver.c (future module).
     */
    (void)nfc;
    device_state.nfc_active = true;
}

/* ========================================================================
 * Frame Validation
 * ======================================================================== */

/**
 * validate_frame_header — Validate the header portion of an assembled frame
 *
 * Returns: 0 on success, -1 on CRC error, -2 on sync error
 */
static int validate_frame_header(void) {
    const struct spi_frame_header *hdr = (const struct spi_frame_header *)rx_ctx.hdr_buf;

    /* Check sync byte */
    if (hdr->sync != SPI_SYNC_BYTE) {
        rx_ctx.frames_sync_error++;
        return -2;
    }

    /* Check header CRC-64 */
    uint64_t expected_crc = read_le64(&rx_ctx.hdr_buf[8]);
    uint64_t actual_crc = crc64_compute(rx_ctx.hdr_buf, 8);
    if (expected_crc != actual_crc) {
        rx_ctx.frames_crc_error++;
        return -1;
    }

    return 0;
}

/**
 * validate_frame_payload — Validate the payload CRC-32 of an assembled frame
 *
 * Returns: 0 on success, -1 on CRC error
 */
static int validate_frame_payload(void) {
    if (rx_ctx.expected_payload_len == 0)
        return 0;  /* No payload to validate */

    uint32_t expected_crc = read_le32(rx_ctx.crc32_buf);
    uint32_t actual_crc = crc32_compute(rx_ctx.payload_buf,
                                          rx_ctx.expected_payload_len);
    if (expected_crc != actual_crc) {
        rx_ctx.frames_crc_error++;
        return -1;
    }

    return 0;
}

/* ========================================================================
 * Frame Dispatch
 * ======================================================================== */

static void dispatch_frame(void) {
    const struct spi_frame_header *hdr = (const struct spi_frame_header *)rx_ctx.hdr_buf;
    uint8_t cmd = hdr->cmd;
    uint16_t len = rx_ctx.expected_payload_len;

    rx_ctx.frames_dispatched++;

    switch (cmd) {
    case CMD_NOP:
        handle_cmd_nop();
        break;
    case CMD_SDR_TUNE:
        handle_cmd_sdr_tune(rx_ctx.payload_buf, len);
        break;
    case CMD_SDR_STREAM:
        handle_cmd_sdr_stream(rx_ctx.payload_buf, len);
        break;
    case CMD_ANT_SELECT:
        handle_cmd_ant_select(rx_ctx.payload_buf, len);
        break;
    case CMD_CC1101_CFG:
        handle_cmd_cc1101_cfg(rx_ctx.payload_buf, len);
        break;
    case CMD_NFC_TRANSACT:
        handle_cmd_nfc_transact(rx_ctx.payload_buf, len);
        break;
    default:
        /* Unknown command — ignore */
        break;
    }

    /* Securely wipe payload buffer after dispatch to prevent
     * residual sensitive data (RF config, NFC keys) from
     * persisting in SRAM between frames. */
    if (len > 0)
        secure_wipe(rx_ctx.payload_buf, len);
}

/* ========================================================================
 * Telemetry Sender
 * ======================================================================== */

/**
 * send_telemetry — Build and queue a telemetry response frame
 *
 * Called periodically (every 100 ms recommended) or in response to NOP.
 */
void spi_protocol_send_telemetry(void) {
    struct telemetry_payload telem;

    /* Populate telemetry from device state.
     * Signed values (RSSI, temperature) are cast to their wire format
     * as uint16_t for transport; the host driver interprets them as int16_t. */
    telem.rssi_dbm_x10   = (uint16_t)device_state.last_rssi_dbm_x10;
    telem.temp_c_x10     = (uint16_t)device_state.last_temp_c_x10;
    telem.vbat_mv        = device_state.last_vbat_mv;
    telem.cc1101_rssi_x10 = (uint16_t)device_state.last_cc_rssi_x10;
    telem.nfc_field_mv   = device_state.last_nfc_field_mv;

    telem.flags = 0;
    if (device_state.sdr_rx_enabled)    telem.flags |= TELEM_FLAG_SDR_RX_ACTIVE;
    if (device_state.sdr_tx_enabled)    telem.flags |= TELEM_FLAG_SDR_TX_ACTIVE;
    if (device_state.cc1101_rx_active)  telem.flags |= TELEM_FLAG_CC1101_RX;
    if (device_state.cc1101_tx_active)  telem.flags |= TELEM_FLAG_CC1101_TX;
    if (device_state.nfc_active)        telem.flags |= TELEM_FLAG_NFC_ACTIVE;
    if (device_state.nfc_tag_present)   telem.flags |= TELEM_FLAG_NFC_TAG_PRESENT;
    telem.uptime_ms = device_state.uptime_ms;

    build_response_frame(CMD_TELEMETRY, (const uint8_t *)&telem,
                         sizeof(telem));
    proto_stats.telemetry_sent++;
}

/* ========================================================================
 * SDR IQ Chunk Sender
 * ======================================================================== */

/**
 * spi_protocol_send_iq_chunk — Queue an SDR IQ data chunk for the host
 *
 * @data: Pointer to IQ sample data (12-bit packed format)
 * @len: Length of IQ data in bytes
 *
 * Called from the SDR DMA completion callback when a buffer of IQ
 * samples is ready to stream to the RK3576.
 */
void spi_protocol_send_iq_chunk(const uint8_t *data, uint16_t len) {
    if (!device_state.sdr_streaming)
        return;

    if (len > SPI_MAX_PAYLOAD)
        len = SPI_MAX_PAYLOAD;  /* Truncate to max payload */

    build_response_frame(CMD_SDR_IQ_CHUNK, data, len);
    proto_stats.iq_chunks_sent++;
}

/* ========================================================================
 * Frame Assembly State Machine
 * ======================================================================== */

/**
 * spi_protocol_process — Main processing loop for SPI protocol
 *
 * Call this from the main loop. It drains bytes from the SPI0 RX
 * ring buffer, assembles complete frames, validates them, and
 * dispatches commands.
 *
 * Returns: number of complete frames processed, or 0 if none
 */
int spi_protocol_process(void) {
    int frames_processed = 0;
    struct spi_frame_header *hdr;

    /* Drain available bytes from the SPI0 RX ring buffer.
     * Insert a data memory barrier before reading to ensure we
     * see the latest data written by the ISR. */
    dmb();
    while (spi_rx_head != spi_rx_tail) {
        uint8_t byte = spi_rx_buf[spi_rx_tail];
        spi_rx_tail = (spi_rx_tail + 1) & (SPI_RX_BUF_SIZE - 1);

        switch (rx_ctx.state) {
        case RX_STATE_IDLE:
            /* Look for sync byte */
            if (byte == SPI_SYNC_BYTE) {
                rx_ctx.hdr_buf[0] = byte;
                rx_ctx.hdr_idx = 1;
                rx_ctx.state = RX_STATE_HEADER;
            }
            /* Ignore non-sync bytes (inter-frame garbage) */
            break;

        case RX_STATE_HEADER:
            rx_ctx.hdr_buf[rx_ctx.hdr_idx++] = byte;

            if (rx_ctx.hdr_idx >= SPI_HDR_SIZE) {
                /* Header complete — validate it */
                int ret = validate_frame_header();
                if (ret == 0) {
                    hdr = (struct spi_frame_header *)rx_ctx.hdr_buf;
                    rx_ctx.expected_payload_len = read_le16((uint8_t *)&hdr->len);

                    if (rx_ctx.expected_payload_len > SPI_MAX_PAYLOAD) {
                        /* Invalid payload length — discard frame */
                        rx_ctx.state = RX_STATE_IDLE;
                        rx_ctx.frames_crc_error++;
                        break;
                    }

                    if (rx_ctx.expected_payload_len > 0) {
                        rx_ctx.payload_idx = 0;
                        rx_ctx.state = RX_STATE_PAYLOAD;
                    } else {
                        /* No payload — go straight to CRC-32 (which is also 0 bytes) */
                        rx_ctx.crc32_idx = 0;
                        rx_ctx.state = RX_STATE_CRC32;
                    }
                } else {
                    /* Header CRC failed — discard frame, resync */
                    rx_ctx.state = RX_STATE_IDLE;
                }
            }
            break;

        case RX_STATE_PAYLOAD:
            rx_ctx.payload_buf[rx_ctx.payload_idx++] = byte;

            if (rx_ctx.payload_idx >= rx_ctx.expected_payload_len) {
                rx_ctx.crc32_idx = 0;
                rx_ctx.state = RX_STATE_CRC32;
            }
            break;

        case RX_STATE_CRC32:
            rx_ctx.crc32_buf[rx_ctx.crc32_idx++] = byte;

            if (rx_ctx.crc32_idx >= SPI_CRC32_SIZE) {
                /* Full frame assembled — validate payload CRC-32 */
                rx_ctx.frames_received++;

                if (validate_frame_payload() == 0) {
                    /* Frame valid — dispatch */
                    dispatch_frame();
                    frames_processed++;
                }
                /* Reset for next frame */
                rx_ctx.state = RX_STATE_IDLE;
            }
            break;

        default:
            rx_ctx.state = RX_STATE_IDLE;
            break;
        }
    }

    return frames_processed;
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

/**
 * spi_protocol_init — Initialize the SPI protocol handler
 *
 * Must be called after rp2350b_init() and after CRC tables are
 * initialized. Resets all state machines and statistics.
 */
void spi_protocol_init(void) {
    /* Initialize CRC tables */
    crc64_init();
    crc32_init();

    /* Reset RX state machine */
    memset(&rx_ctx, 0, sizeof(rx_ctx));
    rx_ctx.state = RX_STATE_IDLE;

    /* Reset ring buffer pointers */
    rx_head = 0;
    rx_tail = 0;
    tx_head = 0;
    tx_tail = 0;

    /* Reset statistics */
    memset(&proto_stats, 0, sizeof(proto_stats));

    /* Reset device state */
    memset(&device_state, 0, sizeof(device_state));

    /* Deassert INT_REQ (no data pending) */
    int_req_deassert();
}

/**
 * spi_protocol_get_stats — Get protocol statistics
 *
 * Useful for diagnostics and telemetry reporting.
 */
struct proto_stats_report {
    uint32_t frames_received;
    uint32_t frames_crc_error;
    uint32_t frames_sync_error;
    uint32_t frames_dispatched;
    uint32_t cmd_nop_rx;
    uint32_t cmd_sdr_tune_rx;
    uint32_t cmd_sdr_stream_rx;
    uint32_t cmd_ant_select_rx;
    uint32_t cmd_cc1101_cfg_rx;
    uint32_t cmd_nfc_transact_rx;
    uint32_t telemetry_sent;
    uint32_t iq_chunks_sent;
    uint32_t tx_overruns;
};

void spi_protocol_get_stats(struct proto_stats_report *report) {
    report->frames_received    = rx_ctx.frames_received;
    report->frames_crc_error   = rx_ctx.frames_crc_error;
    report->frames_sync_error  = rx_ctx.frames_sync_error;
    report->frames_dispatched  = rx_ctx.frames_dispatched;
    report->cmd_nop_rx         = proto_stats.cmd_nop_rx;
    report->cmd_sdr_tune_rx    = proto_stats.cmd_sdr_tune_rx;
    report->cmd_sdr_stream_rx = proto_stats.cmd_sdr_stream_rx;
    report->cmd_ant_select_rx  = proto_stats.cmd_ant_select_rx;
    report->cmd_cc1101_cfg_rx  = proto_stats.cmd_cc1101_cfg_rx;
    report->cmd_nfc_transact_rx = proto_stats.cmd_nfc_transact_rx;
    report->telemetry_sent     = proto_stats.telemetry_sent;
    report->iq_chunks_sent     = proto_stats.iq_chunks_sent;
    report->tx_overruns        = proto_stats.tx_overruns;
}

/**
 * spi_protocol_update_telemetry — Update telemetry data fields
 *
 * Call from ADC/battery monitor and other sensor reading functions.
 */
void spi_protocol_update_telemetry(uint16_t rssi_dbm_x10,
                                    uint16_t temp_c_x10,
                                    uint16_t vbat_mv,
                                    uint16_t cc_rssi_x10,
                                    uint16_t nfc_field_mv) {
    device_state.last_rssi_dbm_x10 = rssi_dbm_x10;
    device_state.last_temp_c_x10   = temp_c_x10;
    device_state.last_vbat_mv      = vbat_mv;
    device_state.last_cc_rssi_x10  = cc_rssi_x10;
    device_state.last_nfc_field_mv = nfc_field_mv;
}

/**
 * spi_protocol_tick — 1 ms system tick
 *
 * Call from a timer interrupt or main loop to update uptime.
 * Note: Watchdog feeding is handled separately in the main loop
 * via watchdog_kick(), not here, to avoid double-feeding.
 *
 * uptime_ms wraps at UINT32_MAX (~49.7 days at 1 ms resolution).
 * This is acceptable for telemetry purposes — the host should treat
 * uptime_ms as a monotonically increasing value that wraps.
 */
void spi_protocol_tick(void) {
    device_state.uptime_ms++;
}