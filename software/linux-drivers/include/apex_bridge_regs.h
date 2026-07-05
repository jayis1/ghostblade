/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * apex_bridge_regs.h — MMIO Register Definitions for GhostBlade SPI Bridge
 *
 * Copyright (C) 2026 GhostBlade Project
 *
 * This header defines the memory-mapped I/O registers for the RK3576
 * SPI0 controller (base 0xFE610000), the GPIO1 bank used for bridge
 * control signals, and the SPI bridge protocol constants.
 *
 * Reference: Rockchip RK3576 TRM, Section 28 (SPI Controller)
 *            Rockchip RK3576 TRM, Section 9 (GPIO Controller)
 */

#ifndef APEX_BRIDGE_REGS_H
#define APEX_BRIDGE_REGS_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/io.h>

/* ========================================================================
 * RK3576 SPI0 Controller Register Map
 * Base address: 0xFE610000
 * ======================================================================== */

#define RK3576_SPI0_BASE        0xFE610000UL

/* Control Register 0 (CR0) — Offset 0x0000 */
#define SPI_CR0                  0x0000

#define SPI_CR0_CLK_DIV_SHIFT    0
#define SPI_CR0_CLK_DIV_MASK     GENMASK(7, 0)
#define SPI_CR0_SPH              BIT(8)    /* Clock Phase (CPHA=1) */
#define SPI_CR0_SPO              BIT(9)    /* Clock Polarity (CPOL=1) */
#define SPI_CR0_CSM_KEEP         BIT(10)   /* CS keep after transfer */
#define SPI_CR0_CSM_1CLK         (0x0 << 11) /* CS inactive: 1 SPI clk */
#define SPI_CR0_CSM_2CLK         (0x1 << 11) /* CS inactive: 2 SPI clk */
#define SPI_CR0_CSM_3CLK         (0x2 << 11) /* CS inactive: 3 SPI clk */
#define SPI_CR0_CSM_MASK         GENMASK(12, 11)

/* Data Frame Size */
#define SPI_CR0_DFS_8BIT         (0x0 << 14)
#define SPI_CR0_DFS_16BIT        (0x1 << 14)
#define SPI_CR0_DFS_32BIT        (0x2 << 14)
#define SPI_CR0_DFS_MASK         GENMASK(15, 14)

#define SPI_CR0_BHT_8BIT         BIT(16)   /* Byte-hold time for 8-bit */
#define SPI_CR0_HT_SHIFT         16

/* Transfer Mode */
#define SPI_CR0_XFM_TX           (0x0 << 18) /* TX only */
#define SPI_CR0_XFM_RX           (0x1 << 18) /* RX only */
#define SPI_CR0_XFM_TXRX         (0x2 << 18) /* Full duplex */
#define SPI_CR0_XFM_MASK         GENMASK(19, 18)

#define SPI_CR0_OPM_MASTER       BIT(20)   /* Master mode */
#define SPI_CR0_MBM_DMA          BIT(21)   /* DMA multi-bit mode */

/* Read Stall Delay */
#define SPI_CR0_RSD_1CLK         (0x0 << 22)
#define SPI_CR0_RSD_2CLK         (0x1 << 22)
#define SPI_CR0_RSD_MASK         GENMASK(23, 22)

/* Slave Select Polarity */
#define SPI_CR0_SSD_HALF         (0x0 << 24) /* Active low */
#define SPI_CR0_SSD_HIGH         (0x1 << 24) /* Active high */
#define SPI_CR0_SSD_MASK         BIT(24)

#define SPI_CR0_SSE_ENABLE       BIT(25)   /* SPI Enable */
#define SPI_CR0_BEN_MSB          BIT(26)   /* MSB first */

/* Control Register 1 (CR1) — Offset 0x0004 */
#define SPI_CR1                  0x0004
#define SPI_CR1_NDF_SHIFT        0
#define SPI_CR1_NDF_MASK         GENMASK(15, 0)

/* Slave Enable Register (SER) — Offset 0x0008 */
#define SPI_SER                  0x0008
#define SPI_SER_CS0              BIT(0)
#define SPI_SER_CS1              BIT(1)

/* Status Register (SR) — Offset 0x000C */
#define SPI_SR                   0x000C
#define SPI_SR_DCOL              BIT(0)    /* Data collision */
#define SPI_SR_TXE               BIT(1)    /* TX error */
#define SPI_SR_RXO               BIT(2)    /* RX overrun */
#define SPI_SR_TXO               BIT(3)    /* TX overrun */
#define SPI_SR_BUSY              BIT(4)    /* SPI busy */
#define SPI_SR_TFE               BIT(5)    /* TX FIFO empty */
#define SPI_SR_TFNF              BIT(6)    /* TX FIFO not full */
#define SPI_SR_RFNE              BIT(7)    /* RX FIFO not empty */
#define SPI_SR_RFF               BIT(8)    /* RX FIFO full */

/* Interrupt Mask Register (IMR) — Offset 0x0010 */
#define SPI_IMR                  0x0010
#define SPI_IMR_TXEIM            BIT(0)    /* TX empty IM */
#define SPI_IMR_TXOIM            BIT(1)    /* TX overrun IM */
#define SPI_IMR_RXUIM            BIT(2)    /* RX underrun IM */
#define SPI_IMR_RXOIM            BIT(3)    /* RX overrun IM */
#define SPI_IMR_MIM              BIT(4)    /* Multi-master IM */
#define SPI_IMR_RIM              BIT(5)    /* RX full IM */

/* Interrupt Status Register (ISR) — Offset 0x0014 */
#define SPI_ISR                  0x0014
#define SPI_ISR_TXEIS            BIT(0)
#define SPI_ISR_TXOIS            BIT(1)
#define SPI_ISR_RXUIS            BIT(2)
#define SPI_ISR_RXOIS            BIT(3)
#define SPI_ISR_MIS              BIT(4)
#define SPI_ISR_RIS              BIT(5)

/* Data Register (DR) — Offset 0x0060 */
#define SPI_DR                   0x0060
#define SPI_DR_DATA_MASK         GENMASK(31, 0)

/* FIFO Level Registers */
#define SPI_TXFLR                0x0074
#define SPI_RXFLR                0x0078

/* DMA Control Registers */
#define SPI_RXDMA                0x0100
#define SPI_RXDMA_EN             BIT(0)
#define SPI_TXDMA                0x0104
#define SPI_TXDMA_EN             BIT(0)
#define SPI_DMARDLR              0x0118
#define SPI_DMATDLR              0x011C

/* ========================================================================
 * RK3576 GPIO1 Bank Register Map
 * Base address: 0xFD510000
 * ======================================================================== */

#define RK3576_GPIO1_BASE        0xFD510000UL

/* Port A registers */
#define GPIO1_SWPORTA_DR         0x0000
#define GPIO1_SWPORTA_DDR        0x0004

/* Port B registers */
#define GPIO1_SWPORTB_DR         0x000C
#define GPIO1_SWPORTB_DDR        0x0010

/* Port A/B combined registers */
#define GPIO1_INTEN               0x0030
#define GPIO1_INTMASK             0x0034
#define GPIO1_INTTYPE             0x0038
#define GPIO1_INT_POLARITY        0x003C
#define GPIO1_INT_STATUS          0x0040
#define GPIO1_INT_RAW_STATUS      0x0044
#define GPIO1_DEBOUNCE            0x0048
#define GPIO1_EOI                 0x004C
#define GPIO1_PORTA_EOI           0x0050

/* ========================================================================
 * GPIO Pin Assignments for SPI Bridge
 * ======================================================================== */

/*
 * GPIO1 Port A (bits 0-15):
 *   A0 = SPI0_MOSI (output, SPI data out to MCU)
 *   A1 = SPI0_MISO (input, SPI data in from MCU)
 *   A2 = SPI0_SCK  (output, SPI clock)
 *   A3 = SPI0_CSn  (output, SPI chip select, active-low)
 *
 * GPIO1 Port B (bits 0-15):
 *   B0 = INT_REQ   (input, MCU interrupt request, active-low)
 *   B1 = HOST_RDY  (output, host ready signal, active-low)
 *   B2 = MCU_RESET (output, MCU reset, active-low)
 */

#define APEX_GPIO_SPI_MOSI       0   /* GPIO1_A0 */
#define APEX_GPIO_SPI_MISO       1   /* GPIO1_A1 */
#define APEX_GPIO_SPI_SCK        2   /* GPIO1_A2 */
#define APEX_GPIO_SPI_CS         3   /* GPIO1_A3 */

#define APEX_GPIO_INT_REQ        8   /* GPIO1_B0 */
#define APEX_GPIO_HOST_READY     9   /* GPIO1_B1 */
#define APEX_GPIO_MCU_RESET      10  /* GPIO1_B2 */

#define APEX_INT_REQ_BIT         BIT(8)
#define APEX_HOST_READY_BIT      BIT(9)
#define APEX_MCU_RESET_BIT       BIT(10)

/* ========================================================================
 * SPI Bridge Protocol Definitions
 * ======================================================================== */

/* Protocol constants */
#define APEX_SPI_SYNC_BYTE        0xAA
#define APEX_SPI_HDR_SIZE         16
#define APEX_SPI_MAX_PAYLOAD      4092
#define APEX_SPI_CRC32_SIZE       4
#define APEX_SPI_FRAME_SIZE_MAX   (APEX_SPI_HDR_SIZE + APEX_SPI_MAX_PAYLOAD + \
                                   APEX_SPI_CRC32_SIZE)

/* SPI clock divider for 50 MHz target (assuming 300 MHz peripheral clock) */
#define APEX_SPI_CLK_DIV_50MHZ    6  /* 300MHz / (2 * (6+1)) = ~21.4 MHz */
#define APEX_SPI_CLK_DIV_25MHZ    12 /* 300MHz / (2 * (12+1)) = ~11.5 MHz */

#define APEX_CMD_NOP              0xFF
#define APEX_CMD_SDR_TUNE         0x01
#define APEX_CMD_SDR_STREAM       0x02
#define APEX_CMD_ANT_SELECT       0x03
#define APEX_CMD_CC1101_CFG       0x04
#define APEX_CMD_NFC_TRANSACT     0x05
#define APEX_CMD_TELEMETRY_REQ    0x06
#define APEX_CMD_RESET_MCU        0x07

/* Reset confirmation magic value — must match SPI_RESET_MAGIC in MCU firmware */
#define APEX_RESET_MAGIC          0x52534554UL  /* "RSET" */

/* Command opcodes — MCU to Host */
#define APEX_CMD_TELEMETRY        0x81
#define APEX_CMD_SDR_IQ_CHUNK     0x82

/* SPI frame header structure (16 bytes) */
struct apex_spi_header {
    __u8  sync;           /* 0x00: Sync byte, always 0xAA */
    __u8  cmd;            /* 0x01: Command opcode */
    __le16 len;           /* 0x02-03: Payload length (0-4092) */
    __le32 reserved;      /* 0x04-07: Reserved, must be 0 */
    __le64 hdr_crc;       /* 0x08-0F: CRC-64 over bytes 0-7 */
} __packed;

/* Telemetry payload (16 bytes) */
struct apex_telemetry {
    __le16 rssi_dbm_x10;       /* SDR RSSI in dBm × 10 */
    __le16 temp_c_x10;         /* MCU die temperature in °C × 10 */
    __le16 vbat_mv;            /* Battery voltage in mV */
    __le16 cc1101_rssi_x10;    /* CC1101 RSSI in dBm × 10 */
    __le16 nfc_field_mv;       /* NFC field strength in mV */
    __le16 flags;              /* Status flags bitmap */
    __le32 uptime_ms;         /* MCU uptime in milliseconds */
} __packed;

/* Telemetry flags */
#define APEX_FLAG_SDR_RX_ACTIVE     BIT(0)
#define APEX_FLAG_SDR_TX_ACTIVE     BIT(1)
#define APEX_FLAG_CC1101_RX         BIT(2)
#define APEX_FLAG_CC1101_TX         BIT(3)
#define APEX_FLAG_NFC_ACTIVE        BIT(4)
#define APEX_FLAG_NFC_TAG_PRESENT   BIT(5)
#define APEX_FLAG_OVERTEMP          BIT(6)
#define APEX_FLAG_LOW_BATTERY       BIT(7)
#define APEX_FLAG_SPI_ERR           BIT(8)
#define APEX_FLAG_DMA_ERR           BIT(9)

/* SDR tune command payload (8 bytes) */
struct apex_sdr_tune_cmd {
    __le32 freq_hz;            /* Frequency in Hz (100 kHz – 3.8 GHz) */
    __le16 bw_khz;             /* Bandwidth in kHz (e.g., 20000 = 20 MHz) */
    __le16 gain_db_x10;        /* LNA gain in dB × 10 */
} __packed;

/* Antenna select command payload (1 byte) */
#define APEX_ANT_MIMO_TX    0   /* PE42422 RF1 → SMA_ANT0 */
#define APEX_ANT_MIMO_RX    1   /* PE42422 RF2 → SMA_ANT1 */
#define APEX_ANT_SUBGHZ     2   /* PE42422 RF3 → CC1101/u.FL */
#define APEX_ANT_TERMINATED 3   /* PE42422 RF4 → 50Ω load */

/* CC1101 configuration payload (variable length) */
#define APEX_CC1101_MAX_REG_LEN   64    /* Maximum consecutive register writes */
struct apex_cc1101_cfg {
    __u8  reg_addr;            /* CC1101 register address */
    __u8  reg_len;             /* Number of consecutive registers to write */
    __u8  data[];              /* Register data (reg_len bytes) */
} __packed;

/* NFC transaction payload (variable length) */
#define APEX_NFC_MAX_DATA_LEN     256   /* Maximum NFC transaction data bytes */
struct apex_nfc_transact {
    __u8  cmd;                 /* NFC command (ISO 14443 A/B, etc.) */
    __u8  flags;               /* Transaction flags */
    __le16 data_len;           /* TX data length */
    __u8  data[];              /* TX data + RX buffer hint */
} __packed;

/* ioctl commands for /dev/apex_bridge0 */
#define APEX_IOC_MAGIC         'A'

#define APEX_IOC_SDR_TUNE      _IOW(APEX_IOC_MAGIC, 1, struct apex_sdr_tune_cmd)
#define APEX_IOC_SDR_STREAM    _IOW(APEX_IOC_MAGIC, 2, __u8)  /* 0=stop, 1=start */
#define APEX_IOC_ANT_SELECT    _IOW(APEX_IOC_MAGIC, 3, __u8)
#define APEX_IOC_CC1101_CFG    _IOW(APEX_IOC_MAGIC, 4, struct apex_cc1101_cfg)
#define APEX_IOC_NFC_TRANSACT  _IOW(APEX_IOC_MAGIC, 5, struct apex_nfc_transact)
#define APEX_IOC_GET_TELEMETRY _IOR(APEX_IOC_MAGIC, 6, struct apex_telemetry)
#define APEX_IOC_MCU_RESET     _IOW(APEX_IOC_MAGIC, 7, __u8)  /* 0=deassert, 1=assert reset */
#define APEX_IOC_GET_STATUS    _IOR(APEX_IOC_MAGIC, 8, __u32)

/* DMA scatter-gather ioctl commands */
#define APEX_IOC_SG_START      _IOW(APEX_IOC_MAGIC, 9, struct apex_sg_config)
#define APEX_IOC_SG_STOP       _IO(APEX_IOC_MAGIC, 10)
#define APEX_IOC_SG_GET_STATUS _IOR(APEX_IOC_MAGIC, 11, struct apex_sg_status)
#define APEX_IOC_SOFT_RESET    _IOW(APEX_IOC_MAGIC, 12, __u32)  /* Soft reset MCU (requires APEX_RESET_MAGIC) */

/* ── DMA Scatter-Gather Structures ──────────────────────────────────────── */

/*
 * DMA scatter-gather configuration for high-throughput SDR IQ streaming.
 *
 * The SG engine allocates a pool of DMA-coherent buffers and uses the
 * SPI controller's DMA capability to transfer IQ data directly from the
 * RP2350B into userspace-mappable buffers without intermediate copies.
 *
 * This is the preferred path for SDR IQ streaming at sample rates above
 * 2 MSPS where the character device read() path becomes a bottleneck.
 *
 * Usage:
 *   1. Open /dev/apex_bridge0
 *   2. APEX_IOC_SG_START with desired config (buffer count, size, timeout)
 *   3. mmap() the SG buffers to userspace
 *   4. Poll for APEX_SG_BUF_READY events or use read() on the SG fd
 *   5. Process IQ data in userspace
 *   6. APEX_IOC_SG_STOP when done
 */

/* Maximum number of scatter-gather buffers */
#define APEX_SG_MAX_BUFS        64

/* Minimum/maximum buffer sizes (must be multiple of 4 for IQ alignment) */
#define APEX_SG_BUF_SIZE_MIN    (4 * 1024)      /* 4 KiB */
#define APEX_SG_BUF_SIZE_MAX    (256 * 1024)    /* 256 KiB */
#define APEX_SG_BUF_SIZE_ALIGN  4               /* Must be 4-byte aligned */

/* SG buffer states */
#define APEX_SG_BUF_FREE        0   /* Available for DMA */
#define APEX_SG_BUF_DMA_ACTIVE  1   /* Currently being filled by DMA */
#define APEX_SG_BUF_READY       2   /* Filled, waiting for userspace read */
#define APEX_SG_BUF_USERSPACE   3   /* Currently mapped by userspace */

/* SG engine states */
#define APEX_SG_STATE_IDLE      0   /* Not streaming */
#define APEX_SG_STATE_RUNNING   1   /* Actively streaming IQ data */
#define APEX_SG_STATE_ERROR     2   /* Error state, needs reset */

/* SG configuration (passed to APEX_IOC_SG_START) */
struct apex_sg_config {
    __u32    buf_count;         /* Number of SG buffers (2–64) */
    __u32    buf_size;          /* Size of each buffer in bytes */
    __u32    timeout_ms;        /* DMA completion timeout in ms (0=infinite) */
    __u32    spi_speed_hz;      /* SPI clock for SG transfers (0=use default) */
    __u8     continuous;        /* 0=single batch, 1=continuous streaming */
    __u8     reserved[3];      /* Reserved, must be 0 */
};

/* SG buffer descriptor (kernel-internal, exposed via mmap) */
struct apex_sg_buf_desc {
    __u32    buf_index;         /* Buffer index (0..buf_count-1) */
    __u32    data_len;          /* Actual data length in this buffer */
    __u64    timestamp_ns;      /* ktime_get_ns() when DMA completed */
    __u32    sequence;          /* Monotonic sequence number */
    __u32    state;             /* APEX_SG_BUF_* state */
};

/* SG engine status (returned by APEX_IOC_SG_GET_STATUS) */
struct apex_sg_status {
    __u32    state;             /* APEX_SG_STATE_* */
    __u32    buf_count;         /* Number of SG buffers */
    __u32    buf_size;          /* Size of each buffer */
    __u64    total_transferred;  /* Total bytes transferred since start */
    __u32    overruns;          /* DMA overrun count */
    __u32    errors;            /* Transfer error count */
    __u32    frames_rx;         /* Valid frames received */
    __u32    frames_crc_err;    /* CRC error count */
};

#endif /* APEX_BRIDGE_REGS_H */