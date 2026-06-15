/*
 * apex_bridge.c — Linux Kernel Driver for GhostBlade SPI Bridge
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This driver implements a character device interface for the SPI bridge
 * between the RK3576 SoC (Linux host) and the RP2350B coprocessor (MCU).
 *
 * The driver provides:
 *   - /dev/apex_bridge0 character device with read/write/ioctl operations
 *   - SPI protocol framing with CRC-64 header and CRC-32 payload integrity
 *   - Interrupt-driven reception (MCU asserts INT_REQ GPIO when data available)
 *   - DMA-compatible transmit path for high-throughput SDR IQ streaming
 *   - IOCTL commands for SDR tuning, antenna selection, NFC, and telemetry
 *
 * Device tree binding:
 *   compatible = "apex,apex-bridge";
 *   reg = <0>;  (SPI chip select 0)
 *   spi-max-frequency = <50000000>;  (50 MHz)
 *   interrupt-parent = <&gpio1>;
 *   interrupts = <8 IRQ_TYPE_EDGE_FALLING>;  (GPIO1_B0 = INT_REQ)
 *
 * Usage:
 *   open("/dev/apex_bridge0", O_RDWR);
 *   write(fd, &cmd, sizeof(cmd));   // Send command to MCU
 *   read(fd, &resp, sizeof(resp));  // Read response from MCU
 *   ioctl(fd, APEX_IOC_SDR_TUNE, &tune);  // Tune SDR frequency
 *   ioctl(fd, APEX_IOC_GET_TELEMETRY, &telem);  // Get MCU telemetry
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "apex_bridge_regs.h"

#define DRIVER_NAME     "apex_bridge"
#define DEVICE_NAME     "apex_bridge"
#define CLASS_NAME      "apex"

/* Module parameters */
static int spi_speed_hz = 50000000;  /* 50 MHz default */
module_param(spi_speed_hz, int, 0644);
MODULE_PARM_DESC(spi_speed_hz, "SPI bus speed in Hz (default: 50000000)");

/* Number of minor devices */
#define APEX_BRIDGE_DEVS  1

/* RX/TX buffer sizes */
#define APEX_RX_FIFO_SIZE  (64 * 1024)  /* 64 KB receive FIFO */
#define APEX_TX_FIFO_SIZE  (64 * 1024)  /* 64 KB transmit FIFO */

/* ========================================================================
 * Driver State Structure
 * ======================================================================== */

struct apex_bridge_dev {
    struct spi_device  *spi;          /* SPI device */
    dev_t              devt;          /* Device number */
    struct cdev        cdev;          /* Character device */
    struct device      *dev;          /* Device class */
    struct class       *class;        /* Device class */
    struct mutex       lock;          /* Mutex for SPI bus access */
    spinlock_t         rx_lock;       /* Spinlock for RX FIFO */
    wait_queue_head_t  rx_waitq;      /* Wait queue for read() */
    wait_queue_head_t  tx_waitq;      /* Wait queue for write() */
    struct completion   xfer_done;     /* SPI transfer completion */
    struct work_struct  rx_work;       /* Work item for interrupt handling */
    int                irq;            /* INT_REQ GPIO IRQ number */
    int                gpio_int_req;   /* INT_REQ GPIO number */
    int                gpio_host_rdy; /* HOST_RDY GPIO number */
    int                gpio_mcu_reset; /* MCU_RESET GPIO number */
    atomic_t            open_count;     /* Atomic open counter (0 or 1) */
    struct kfifo       rx_fifo;       /* RX data FIFO */
    struct kfifo       tx_fifo;       /* TX data FIFO */
    struct apex_telemetry last_telem; /* Last telemetry snapshot */
    unsigned long      flags;         /* Driver state flags */
};

static struct apex_bridge_dev *apex_dev;

/* Driver state flags */
#define APEX_FLAG_MCU_READY     0
#define APEX_FLAG_MCU_RESET    1
#define APEX_FLAG_SPI_ERROR    2

/* ========================================================================
 * CRC Computation (Kernel Implementation)
 * ======================================================================== */

/*
 * CRC-64 using polynomial 0x42F0E1EBA9EA3693 (ECMA-182)
 * Used for SPI frame header integrity check.
 */
static uint64_t apex_crc64(const uint8_t *data, size_t len)
{
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    size_t i;
    int j;

    for (i = 0; i < len; i++) {
        crc ^= (uint64_t)data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/*
 * CRC-32 using polynomial 0xEDB88320 (ISO 3309, same as Ethernet)
 * Used for SPI frame payload integrity check.
 */
static uint32_t apex_crc32(const uint8_t *data, size_t len)
{
    const uint32_t poly = 0xEDB88320UL;
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;
    int j;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * SPI Transfer Functions
 * ======================================================================== */

/*
 * apex_spi_xfer — Perform a full-duplex SPI transfer
 *
 * This function sends a command frame and receives a response frame
 * in a single SPI transaction. The MCU acts as SPI slave.
 *
 * The SPI protocol uses a 16-byte header followed by an optional payload
 * and a 4-byte CRC-32 trailer.
 *
 * Returns: number of response bytes received, or negative error code
 */
static int apex_spi_xfer(struct apex_bridge_dev *dev,
                         const uint8_t *tx_buf, size_t tx_len,
                         uint8_t *rx_buf, size_t rx_buf_size)
{
    struct spi_transfer xfer;
    struct spi_message msg;
    int ret;

    if (tx_len > APEX_SPI_FRAME_SIZE_MAX)
        return -EMSGSIZE;

    memset(&xfer, 0, sizeof(xfer));
    xfer.tx_buf = tx_buf;
    xfer.rx_buf = rx_buf;
    xfer.len = tx_len;
    xfer.speed_hz = spi_speed_hz;
    xfer.bits_per_word = 8;
    xfer.cs_change = 0;

    spi_message_init_with_transfers(&msg, &xfer, 1);

    mutex_lock(&dev->lock);
    ret = spi_sync(dev->spi, &msg);
    mutex_unlock(&dev->lock);

    if (ret < 0) {
        dev_err(&dev->spi->dev, "SPI transfer failed: %d\n", ret);
        set_bit(APEX_FLAG_SPI_ERROR, &dev->flags);
        return ret;
    }

    return (xfer.len > rx_buf_size) ? rx_buf_size : xfer.len;
}

/*
 * apex_build_frame — Build an SPI protocol frame from command + payload
 *
 * Frame format:
 *   [0x00] SYNC     = 0xAA
 *   [0x01] CMD      = command opcode
 *   [0x02] LEN_LO   = payload length low byte
 *   [0x03] LEN_HI   = payload length high byte
 *   [0x04] RESERVED = 0x00000000
 *   [0x08] HDR_CRC  = CRC-64 over bytes 0-7
 *   [0x10] PAYLOAD  = variable length (0 to 4092 bytes)
 *   [0x10+LEN] CRC32 = CRC-32 over payload
 *
 * Returns: total frame size, or negative error code
 */
static int apex_build_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t payload_len, uint8_t *frame_buf,
                            size_t frame_buf_size)
{
    struct apex_spi_header *hdr;
    size_t total_len;
    uint32_t crc32_val;
    uint64_t crc64_val;

    if (payload_len > APEX_SPI_MAX_PAYLOAD)
        return -EMSGSIZE;

    total_len = APEX_SPI_HDR_SIZE + payload_len + APEX_SPI_CRC32_SIZE;
    if (total_len > frame_buf_size)
        return -EMSGSIZE;

    /* Build header */
    hdr = (struct apex_spi_header *)frame_buf;
    hdr->sync = APEX_SPI_SYNC_BYTE;
    hdr->cmd = cmd;
    hdr->len = cpu_to_le16(payload_len);
    hdr->reserved = 0;

    /* Compute header CRC-64 */
    crc64_val = apex_crc64(frame_buf, 8);
    put_unaligned_le64(crc64_val, &frame_buf[8]);

    /* Copy payload */
    if (payload_len > 0 && payload != NULL)
        memcpy(&frame_buf[APEX_SPI_HDR_SIZE], payload, payload_len);

    /* Compute payload CRC-32 */
    crc32_val = apex_crc32(&frame_buf[APEX_SPI_HDR_SIZE], payload_len);
    put_unaligned_le32(crc32_val, &frame_buf[APEX_SPI_HDR_SIZE + payload_len]);

    return total_len;
}

/*
 * apex_validate_frame — Validate an incoming SPI protocol frame
 *
 * Returns: 0 on success, negative error code on failure
 */
static int apex_validate_frame(const uint8_t *frame, size_t frame_len,
                               uint8_t *cmd, uint16_t *payload_len,
                               const uint8_t **payload)
{
    const struct apex_spi_header *hdr;
    uint16_t len;
    uint64_t expected_crc64, actual_crc64;
    uint32_t expected_crc32, actual_crc32;

    if (frame_len < APEX_SPI_HDR_SIZE)
        return -EBADMSG;

    hdr = (const struct apex_spi_header *)frame;

    /* Check sync byte */
    if (hdr->sync != APEX_SPI_SYNC_BYTE) {
        pr_err("apex_bridge: invalid sync byte: 0x%02x (expected 0xAA)\n",
               hdr->sync);
        return -EBADMSG;
    }

    /* Check header CRC-64 */
    actual_crc64 = apex_crc64(frame, 8);
    expected_crc64 = get_unaligned_le64(&frame[8]);
    if (actual_crc64 != expected_crc64) {
        pr_err("apex_bridge: header CRC-64 mismatch (expected 0x%016llx, got 0x%016llx)\n",
               expected_crc64, actual_crc64);
        return -EBADMSG;
    }

    /* Extract payload length */
    len = le16_to_cpu(hdr->len);
    if (len > APEX_SPI_MAX_PAYLOAD)
        return -EBADMSG;

    /* Check total frame size */
    if (frame_len < APEX_SPI_HDR_SIZE + len + APEX_SPI_CRC32_SIZE)
        return -EBADMSG;

    /* Check payload CRC-32 */
    if (len > 0) {
        actual_crc32 = apex_crc32(&frame[APEX_SPI_HDR_SIZE], len);
        expected_crc32 = get_unaligned_le32(&frame[APEX_SPI_HDR_SIZE + len]);
        if (actual_crc32 != expected_crc32) {
            pr_err("apex_bridge: payload CRC-32 mismatch (expected 0x%08x, got 0x%08x)\n",
                   expected_crc32, actual_crc32);
            return -EBADMSG;
        }
    }

    /* Return parsed fields */
    *cmd = hdr->cmd;
    *payload_len = len;
    *payload = &frame[APEX_SPI_HDR_SIZE];

    return 0;
}

/* ========================================================================
 * GPIO Control Functions
 * ======================================================================== */

static int apex_gpio_init(struct apex_bridge_dev *dev)
{
    struct device_node *np = dev->spi->dev.of_node;
    int ret;

    /* Get GPIO numbers from device tree */
    dev->gpio_int_req = of_get_named_gpio(np, "apex,int-req-gpio", 0);
    dev->gpio_host_rdy = of_get_named_gpio(np, "apex,host-rdy-gpio", 0);
    dev->gpio_mcu_reset = of_get_named_gpio(np, "apex,mcu-reset-gpio", 0);

    if (!gpio_is_valid(dev->gpio_int_req) ||
        !gpio_is_valid(dev->gpio_host_rdy) ||
        !gpio_is_valid(dev->gpio_mcu_reset)) {
        dev_err(&dev->spi->dev, "Invalid GPIO numbers in device tree\n");
        return -EINVAL;
    }

    /* Request GPIOs */
    ret = devm_gpio_request_one(&dev->spi->dev, dev->gpio_int_req,
                                 GPIOF_IN, "apex_int_req");
    if (ret) {
        dev_err(&dev->spi->dev, "Failed to request INT_REQ GPIO: %d\n", ret);
        return ret;
    }

    ret = devm_gpio_request_one(&dev->spi->dev, dev->gpio_host_rdy,
                                 GPIOF_OUT_INIT_HIGH, "apex_host_rdy");
    if (ret) {
        dev_err(&dev->spi->dev, "Failed to request HOST_RDY GPIO: %d\n", ret);
        return ret;
    }

    ret = devm_gpio_request_one(&dev->spi->dev, dev->gpio_mcu_reset,
                                 GPIOF_OUT_INIT_LOW, "apex_mcu_reset");
    if (ret) {
        dev_err(&dev->spi->dev, "Failed to request MCU_RESET GPIO: %d\n", ret);
        return ret;
    }

    /* MCU is initially in reset; will be released after SPI init */
    set_bit(APEX_FLAG_MCU_RESET, &dev->flags);

    return 0;
}

static void apex_mcu_reset_assert(struct apex_bridge_dev *dev)
{
    gpio_set_value(dev->gpio_mcu_reset, 0);  /* Active-low: assert reset */
    set_bit(APEX_FLAG_MCU_RESET, &dev->flags);
    msleep(10);  /* Hold reset for 10 ms */
}

static void apex_mcu_reset_release(struct apex_bridge_dev *dev)
{
    gpio_set_value(dev->gpio_mcu_reset, 1);  /* Active-low: release reset */
    clear_bit(APEX_FLAG_MCU_RESET, &dev->flags);

    /* Wait for MCU to boot and stabilize */
    msleep(100);

    /* Signal host readiness */
    gpio_set_value(dev->gpio_host_rdy, 0);  /* Active-low: assert HOST_RDY */
    set_bit(APEX_FLAG_MCU_READY, &dev->flags);
}

/* ========================================================================
 * Interrupt Handler & Work Queue
 * ======================================================================== */

/*
 * The RP2350B asserts INT_REQ (active-low) when it has data to send
 * to the host. This interrupt triggers a work item that reads the
 * pending data from the SPI bus.
 */

static void apex_rx_work_handler(struct work_struct *work)
{
    struct apex_bridge_dev *dev;
    uint8_t *rx_frame;
    uint8_t *tx_frame;
    int rx_len;
    uint8_t cmd;
    uint16_t payload_len;
    const uint8_t *payload;
    int ret;

    dev = container_of(work, struct apex_bridge_dev, rx_work);

    rx_frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!rx_frame)
        return;

    tx_frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!tx_frame) {
        kfree(rx_frame);
        return;
    }

    /* Send NOP command to trigger MCU to clock out its pending data */
    ret = apex_build_frame(APEX_CMD_NOP, NULL, 0, tx_frame,
                           APEX_SPI_FRAME_SIZE_MAX);
    if (ret < 0) {
        dev_err(&dev->spi->dev, "Failed to build NOP frame: %d\n", ret);
        goto out_free;
    }

    /* Perform full-duplex SPI transfer:
     * TX: NOP frame (16 bytes header only)
     * RX: Response frame from MCU
     * We send the full max frame size because we don't know the response
     * length in advance. The MCU will clock out its response while we
     * clock in the NOP.
     */
    rx_len = apex_spi_xfer(dev, tx_frame, APEX_SPI_FRAME_SIZE_MAX,
                           rx_frame, APEX_SPI_FRAME_SIZE_MAX);
    if (rx_len < 0) {
        dev_err(&dev->spi->dev, "SPI transfer failed: %d\n", rx_len);
        goto out_free;
    }

    /* Validate and parse the received frame */
    ret = apex_validate_frame(rx_frame, rx_len, &cmd, &payload_len, &payload);
    if (ret < 0) {
        dev_warn(&dev->spi->dev, "Invalid frame received: %d\n", ret);
        goto out_free;
    }

    /* Process the response based on command */
    switch (cmd) {
    case APEX_CMD_TELEMETRY:
        if (payload_len == sizeof(struct apex_telemetry)) {
            spin_lock(&dev->rx_lock);
            memcpy(&dev->last_telem, payload,
                   sizeof(struct apex_telemetry));
            /* Also push to RX FIFO for user-space read() */
            kfifo_in(&dev->rx_fifo, payload, payload_len);
            spin_unlock(&dev->rx_lock);
            wake_up_interruptible(&dev->rx_waitq);
        }
        break;

    case APEX_CMD_SDR_IQ_CHUNK:
        /* SDR IQ data — push to RX FIFO */
        spin_lock(&dev->rx_lock);
        kfifo_in(&dev->rx_fifo, payload, payload_len);
        spin_unlock(&dev->rx_lock);
        wake_up_interruptible(&dev->rx_waitq);
        break;

    default:
        /* Generic response — push entire payload to RX FIFO */
        spin_lock(&dev->rx_lock);
        kfifo_in(&dev->rx_fifo, payload, payload_len);
        spin_unlock(&dev->rx_lock);
        wake_up_interruptible(&dev->rx_waitq);
        break;
    }

out_free:
    kfree(rx_frame);
    kfree(tx_frame);
}

static irqreturn_t apex_irq_handler(int irq, void *dev_id)
{
    struct apex_bridge_dev *dev = dev_id;

    /* Schedule work item to process the interrupt */
    schedule_work(&dev->rx_work);

    return IRQ_HANDLED;
}

/* ========================================================================
 * Character Device Operations
 * ======================================================================== */

static int apex_bridge_open(struct inode *inode, struct file *filp)
{
    struct apex_bridge_dev *dev;

    dev = container_of(inode->i_cdev, struct apex_bridge_dev, cdev);
    if (!dev) {
        pr_err("apex_bridge: device not found\n");
        return -ENODEV;
    }

    filp->private_data = dev;

    /* Use cmpxchg for atomic open check — dev->open is bool, but
     * test_and_set_bit() requires unsigned long alignment. Use a
     * proper atomic compare-and-swap instead.
     */
    if (cmpxchg(&dev->open_count, 0, 1) != 0)
        return -EBUSY;  /* Only one user at a time */
    /* Resume device from runtime suspend */
    pm_runtime_get_sync(&dev->spi->dev);

    return 0;
}

static int apex_bridge_release(struct inode *inode, struct file *filp)
{
    struct apex_bridge_dev *dev = filp->private_data;

    /* Allow device to runtime suspend */
    pm_runtime_put_autosuspend(&dev->spi->dev);
    atomic_set(&dev->open_count, 0);
    filp->private_data = NULL;

    return 0;
}

static ssize_t apex_bridge_read(struct file *filp, char __user *buf,
                                 size_t count, loff_t *f_pos)
{
    struct apex_bridge_dev *dev = filp->private_data;
    unsigned int copied;
    int ret;

    if (kfifo_is_empty(&dev->rx_fifo)) {
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /* Wait for data */
        ret = wait_event_interruptible(dev->rx_waitq,
                                        !kfifo_is_empty(&dev->rx_fifo));
        if (ret)
            return ret;
    }

    spin_lock(&dev->rx_lock);
    ret = kfifo_to_user(&dev->rx_fifo, buf, count, &copied);
    spin_unlock(&dev->rx_lock);

    if (ret)
        return ret;

    return copied;
}

static ssize_t apex_bridge_write(struct file *filp, const char __user *buf,
                                 size_t count, loff_t *f_pos)
{
    struct apex_bridge_dev *dev = filp->private_data;
    uint8_t *kbuf;
    uint8_t *frame;
    uint8_t *rx_buf;
    int frame_len;
    int ret;

    /* Validate write size */
    if (count < 1)
        return -EINVAL;
    if (count > APEX_SPI_MAX_PAYLOAD)
        return -EMSGSIZE;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree_sensitive(kbuf);
        return -EFAULT;
    }

    frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!frame) {
        kfree(kbuf);
        return -ENOMEM;
    }

    rx_buf = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!rx_buf) {
        kfree(kbuf);
        kfree(frame);
        return -ENOMEM;
    }

    /* Build frame from user data.
     * The first byte of user data is treated as the command opcode.
     * The remaining bytes are the payload.
     */
    frame_len = apex_build_frame(kbuf[0], &kbuf[1], count - 1,
                                  frame, APEX_SPI_FRAME_SIZE_MAX);
    if (frame_len < 0) {
        ret = frame_len;
        goto out;
    }

    /* Perform SPI transfer */
    ret = apex_spi_xfer(dev, frame, frame_len, rx_buf,
                        APEX_SPI_FRAME_SIZE_MAX);

    /* Parse any response and push to RX FIFO */
    if (ret > 0) {
        uint8_t resp_cmd;
        uint16_t resp_len;
        const uint8_t *resp_payload;
        int validate_ret;

        validate_ret = apex_validate_frame(rx_buf, ret, &resp_cmd,
                                            &resp_len, &resp_payload);
        if (validate_ret == 0 && resp_len > 0) {
            spin_lock(&dev->rx_lock);
            kfifo_in(&dev->rx_fifo, resp_payload, resp_len);
            spin_unlock(&dev->rx_lock);
            wake_up_interruptible(&dev->rx_waitq);
        }
    }

    ret = count;  /* Report full write as consumed */

out:
    /* Securely wipe buffers that may contain protocol data before freeing */
    kfree_sensitive(kbuf);
    kfree_sensitive(frame);
    kfree_sensitive(rx_buf);
    return ret;
}

static long apex_bridge_ioctl(struct file *filp, unsigned int cmd,
                               unsigned long arg)
{
    struct apex_bridge_dev *dev = filp->private_data;
    uint8_t *frame;
    uint8_t *rx_buf;
    int frame_len, ret;

    frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!frame)
        return -ENOMEM;

    rx_buf = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!rx_buf) {
        kfree(frame);
        return -ENOMEM;
    }

    switch (cmd) {
    case APEX_IOC_SDR_TUNE: {
        struct apex_sdr_tune_cmd tune;

        if (copy_from_user(&tune, (struct apex_sdr_tune_cmd __user *)arg,
                           sizeof(tune))) {
            ret = -EFAULT;
            break;
        }

        frame_len = apex_build_frame(APEX_CMD_SDR_TUNE,
                                      (const uint8_t *)&tune,
                                      sizeof(tune), frame,
                                      APEX_SPI_FRAME_SIZE_MAX);
        if (frame_len < 0) {
            ret = frame_len;
            break;
        }

        ret = apex_spi_xfer(dev, frame, frame_len, rx_buf,
                             APEX_SPI_FRAME_SIZE_MAX);
        break;
    }

    case APEX_IOC_SDR_STREAM: {
        uint8_t enable;

        if (copy_from_user(&enable, (uint8_t __user *)arg, sizeof(enable))) {
            ret = -EFAULT;
            break;
        }

        frame_len = apex_build_frame(APEX_CMD_SDR_STREAM, &enable,
                                      sizeof(enable), frame,
                                      APEX_SPI_FRAME_SIZE_MAX);
        if (frame_len < 0) {
            ret = frame_len;
            break;
        }

        ret = apex_spi_xfer(dev, frame, frame_len, rx_buf,
                             APEX_SPI_FRAME_SIZE_MAX);
        break;
    }

    case APEX_IOC_ANT_SELECT: {
        uint8_t ant_id;

        if (copy_from_user(&ant_id, (uint8_t __user *)arg,
                           sizeof(ant_id))) {
            ret = -EFAULT;
            break;
        }

        if (ant_id > APEX_ANT_TERMINATED) {
            ret = -EINVAL;
            break;
        }

        frame_len = apex_build_frame(APEX_CMD_ANT_SELECT, &ant_id,
                                      sizeof(ant_id), frame,
                                      APEX_SPI_FRAME_SIZE_MAX);
        if (frame_len < 0) {
            ret = frame_len;
            break;
        }

        ret = apex_spi_xfer(dev, frame, frame_len, rx_buf,
                             APEX_SPI_FRAME_SIZE_MAX);
        break;
    }

    case APEX_IOC_CC1101_CFG: {
        struct apex_cc1101_cfg cfg;
        uint8_t *cfg_buf;
        size_t cfg_total;

        if (copy_from_user(&cfg, (struct apex_cc1101_cfg __user *)arg,
                           sizeof(cfg))) {
            ret = -EFAULT;
            break;
        }

        /* Validate reg_len to prevent buffer overflow */
        if (cfg.reg_len == 0 || cfg.reg_len > sizeof(cfg.data)) {
            ret = -EINVAL;
            break;
        }

        cfg_total = sizeof(cfg.reg_addr) + sizeof(cfg.reg_len) + cfg.reg_len;
        cfg_buf = kmalloc(cfg_total, GFP_KERNEL);
        if (!cfg_buf) {
            ret = -ENOMEM;
            break;
        }

        cfg_buf[0] = cfg.reg_addr;
        cfg_buf[1] = cfg.reg_len;
        if (copy_from_user(&cfg_buf[2],
                           (uint8_t __user *)(arg + offsetof(struct apex_cc1101_cfg, data)),
                           cfg.reg_len)) {
            kfree_sensitive(cfg_buf);
            ret = -EFAULT;
            break;
        }

        frame_len = apex_build_frame(APEX_CMD_CC1101_CFG, cfg_buf,
                                      cfg_total, frame,
                                      APEX_SPI_FRAME_SIZE_MAX);
        kfree_sensitive(cfg_buf);

        if (frame_len < 0) {
            ret = frame_len;
            break;
        }

        ret = apex_spi_xfer(dev, frame, frame_len, rx_buf,
                             APEX_SPI_FRAME_SIZE_MAX);
        break;
    }

    case APEX_IOC_NFC_TRANSACT: {
        struct apex_nfc_transact nfc;
        uint8_t *nfc_buf;
        size_t nfc_total;

        if (copy_from_user(&nfc, (struct apex_nfc_transact __user *)arg,
                           sizeof(nfc))) {
            ret = -EFAULT;
            break;
        }

        /* Validate data_len to prevent buffer overflow */
        if (le16_to_cpu(nfc.data_len) > sizeof(nfc.data)) {
            ret = -EINVAL;
            break;
        }

        nfc_total = sizeof(nfc.cmd) + sizeof(nfc.flags) +
                     sizeof(nfc.data_len) + le16_to_cpu(nfc.data_len);
        nfc_buf = kmalloc(nfc_total, GFP_KERNEL);
        if (!nfc_buf) {
            ret = -ENOMEM;
            break;
        }

        nfc_buf[0] = nfc.cmd;
        nfc_buf[1] = nfc.flags;
        put_unaligned_le16(nfc.data_len, &nfc_buf[2]);
        if (copy_from_user(&nfc_buf[4],
                           (uint8_t __user *)(arg + offsetof(struct apex_nfc_transact, data)),
                           le16_to_cpu(nfc.data_len))) {
            kfree_sensitive(nfc_buf);
            ret = -EFAULT;
            break;
        }

        frame_len = apex_build_frame(APEX_CMD_NFC_TRANSACT, nfc_buf,
                                      nfc_total, frame,
                                      APEX_SPI_FRAME_SIZE_MAX);
        kfree_sensitive(nfc_buf);

        if (frame_len < 0) {
            ret = frame_len;
            break;
        }

        ret = apex_spi_xfer(dev, frame, frame_len, rx_buf,
                             APEX_SPI_FRAME_SIZE_MAX);
        break;
    }

    case APEX_IOC_GET_TELEMETRY: {
        struct apex_telemetry telem;

        spin_lock(&dev->rx_lock);
        memcpy(&telem, &dev->last_telem, sizeof(telem));
        spin_unlock(&dev->rx_lock);

        if (copy_to_user((struct apex_telemetry __user *)arg,
                         &telem, sizeof(telem))) {
            ret = -EFAULT;
            break;
        }

        ret = 0;
        break;
    }

    case APEX_IOC_MCU_RESET: {
        uint8_t reset_assert;

        if (copy_from_user(&reset_assert, (uint8_t __user *)arg,
                           sizeof(reset_assert))) {
            ret = -EFAULT;
            break;
        }

        if (reset_assert) {
            apex_mcu_reset_assert(dev);
        } else {
            apex_mcu_reset_release(dev);
        }

        ret = 0;
        break;
    }

    case APEX_IOC_GET_STATUS: {
        uint32_t status = 0;

        if (test_bit(APEX_FLAG_MCU_READY, &dev->flags))
            status |= BIT(0);
        if (test_bit(APEX_FLAG_MCU_RESET, &dev->flags))
            status |= BIT(1);
        if (test_bit(APEX_FLAG_SPI_ERROR, &dev->flags))
            status |= BIT(2);

        if (copy_to_user((uint32_t __user *)arg, &status, sizeof(status))) {
            ret = -EFAULT;
            break;
        }

        ret = 0;
        break;
    }

    default:
        ret = -ENOTTY;
        break;
    }

    kfree(frame);
    kfree(rx_buf);
    return ret;
}

static __poll_t apex_bridge_poll(struct file *filp,
                                  struct poll_table_struct *wait)
{
    struct apex_bridge_dev *dev = filp->private_data;
    __poll_t mask = 0;

    poll_wait(filp, &dev->rx_waitq, wait);

    if (!kfifo_is_empty(&dev->rx_fifo))
        mask |= EPOLLIN | EPOLLRDNORM;

    /* Always writable (SPI is synchronous) */
    mask |= EPOLLOUT | EPOLLWRNORM;

    return mask;
}

/* File operations structure */
static const struct file_operations apex_bridge_fops = {
    .owner          = THIS_MODULE,
    .open           = apex_bridge_open,
    .release        = apex_bridge_release,
    .read           = apex_bridge_read,
    .write          = apex_bridge_write,
    .unlocked_ioctl = apex_bridge_ioctl,
    .poll           = apex_bridge_poll,
};

/* ========================================================================
 * Sysfs Attributes for Telemetry and Status
 * ======================================================================== */

/*
 * These sysfs attributes expose telemetry and device status information
 * through /sys/class/apex/apex_bridge0/ for userspace monitoring tools
 * and shell scripts. This complements the ioctl interface which is
 * better suited for high-frequency programmatic access.
 *
 * Available attributes:
 *   rssi_dbm_x10    - SDR RSSI in dBm × 10
 *   temp_c_x10      - MCU die temperature in °C × 10
 *   vbat_mv         - Battery voltage in mV
 *   cc1101_rssi_x10 - CC1101 sub-GHz RSSI in dBm × 10
 *   nfc_field_mv    - NFC field strength in mV
 *   mcu_flags       - MCU status flags (hex)
 *   uptime_ms       - MCU uptime in milliseconds
 *   driver_status   - Driver status flags (hex)
 *   spi_errors      - Cumulative SPI error count
 */

static ssize_t rssi_dbm_x10_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint16_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.rssi_dbm_x10;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%d\n", (int16_t)le16_to_cpu(val));
}
static DEVICE_ATTR_RO(rssi_dbm_x10);

static ssize_t temp_c_x10_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint16_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.temp_c_x10;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%d\n", (int16_t)le16_to_cpu(val));
}
static DEVICE_ATTR_RO(temp_c_x10);

static ssize_t vbat_mv_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint16_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.vbat_mv;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%u\n", le16_to_cpu(val));
}
static DEVICE_ATTR_RO(vbat_mv);

static ssize_t cc1101_rssi_x10_show(struct device *dev,
                                      struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint16_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.cc1101_rssi_x10;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%d\n", (int16_t)le16_to_cpu(val));
}
static DEVICE_ATTR_RO(cc1101_rssi_x10);

static ssize_t nfc_field_mv_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint16_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.nfc_field_mv;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%u\n", le16_to_cpu(val));
}
static DEVICE_ATTR_RO(nfc_field_mv);

static ssize_t mcu_flags_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint16_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.flags;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "0x%04x\n", le16_to_cpu(val));
}
static DEVICE_ATTR_RO(mcu_flags);

static ssize_t uptime_ms_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint32_t val;

    spin_lock(&adev->rx_lock);
    val = adev->last_telem.uptime_ms;
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%u\n", le32_to_cpu(val));
}
static DEVICE_ATTR_RO(uptime_ms);

static ssize_t driver_status_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint32_t status = 0;

    if (test_bit(APEX_FLAG_MCU_READY, &adev->flags))
        status |= BIT(0);
    if (test_bit(APEX_FLAG_MCU_RESET, &adev->flags))
        status |= BIT(1);
    if (test_bit(APEX_FLAG_SPI_ERROR, &adev->flags))
        status |= BIT(2);

    return sprintf(buf, "0x%08x\n", status);
}
static DEVICE_ATTR_RO(driver_status);

static ssize_t spi_errors_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);

    return sprintf(buf, "%lu\n",
                   test_bit(APEX_FLAG_SPI_ERROR, &adev->flags) ? 1UL : 0UL);
}
static DEVICE_ATTR_RO(spi_errors);

static struct attribute *apex_bridge_attrs[] = {
    &dev_attr_rssi_dbm_x10.attr,
    &dev_attr_temp_c_x10.attr,
    &dev_attr_vbat_mv.attr,
    &dev_attr_cc1101_rssi_x10.attr,
    &dev_attr_nfc_field_mv.attr,
    &dev_attr_mcu_flags.attr,
    &dev_attr_uptime_ms.attr,
    &dev_attr_driver_status.attr,
    &dev_attr_spi_errors.attr,
    NULL,
};
ATTRIBUTE_GROUPS(apex_bridge);

/* ========================================================================
 * SPI Driver Probe & Remove
 * ======================================================================== */

static int apex_bridge_probe(struct spi_device *spi)
{
    struct apex_bridge_dev *dev;
    int ret;

    dev_info(&spi->dev, "GhostBlade SPI Bridge driver probing\n");

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->spi = spi;
    spi_set_drvdata(spi, dev);

    atomic_set(&dev->open_count, 0);
    mutex_init(&dev->lock);
    spin_lock_init(&dev->rx_lock);
    init_waitqueue_head(&dev->rx_waitq);
    init_waitqueue_head(&dev->tx_waitq);
    init_completion(&dev->xfer_done);
    INIT_WORK(&dev->rx_work, apex_rx_work_handler);

    /* Initialize FIFOs */
    ret = kfifo_alloc(&dev->rx_fifo, APEX_RX_FIFO_SIZE, GFP_KERNEL);
    if (ret) {
        dev_err(&spi->dev, "Failed to allocate RX FIFO: %d\n", ret);
        goto err_free_dev;
    }

    ret = kfifo_alloc(&dev->tx_fifo, APEX_TX_FIFO_SIZE, GFP_KERNEL);
    if (ret) {
        dev_err(&spi->dev, "Failed to allocate TX FIFO: %d\n", ret);
        goto err_free_rx_fifo;
    }

    /* Configure SPI */
    spi->mode = SPI_MODE_0;
    spi->max_speed_hz = spi_speed_hz;
    spi->bits_per_word = 8;
    ret = spi_setup(spi);
    if (ret) {
        dev_err(&spi->dev, "Failed to setup SPI: %d\n", ret);
        goto err_free_tx_fifo;
    }

    /* Initialize GPIOs */
    ret = apex_gpio_init(dev);
    if (ret) {
        dev_err(&spi->dev, "Failed to initialize GPIOs: %d\n", ret);
        goto err_free_tx_fifo;
    }

    /* Register interrupt handler */
    dev->irq = gpio_to_irq(dev->gpio_int_req);
    if (dev->irq < 0) {
        dev_err(&spi->dev, "Failed to get IRQ for INT_REQ GPIO: %d\n",
                dev->irq);
        ret = dev->irq;
        goto err_free_tx_fifo;
    }

    ret = devm_request_irq(&spi->dev, dev->irq, apex_irq_handler,
                           IRQF_TRIGGER_FALLING, DRIVER_NAME, dev);
    if (ret) {
        dev_err(&spi->dev, "Failed to request IRQ %d: %d\n",
                dev->irq, ret);
        goto err_free_tx_fifo;
    }

    /* Allocate character device region */
    ret = alloc_chrdev_region(&dev->devt, 0, APEX_BRIDGE_DEVS, DEVICE_NAME);
    if (ret < 0) {
        dev_err(&spi->dev, "Failed to allocate chrdev region: %d\n", ret);
        goto err_free_irq;
    }

    /* Initialize character device */
    cdev_init(&dev->cdev, &apex_bridge_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &apex_bridge_fops;

    ret = cdev_add(&dev->cdev, dev->devt, 1);
    if (ret) {
        dev_err(&spi->dev, "Failed to add cdev: %d\n", ret);
        goto err_unreg_chrdev;
    }

    /* Create device class */
    dev->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        dev_err(&spi->dev, "Failed to create class: %d\n", ret);
        goto err_del_cdev;
    }

    /* Create device node (with sysfs telemetry attributes) */
    dev->dev = device_create_with_groups(dev->class, &spi->dev, dev->devt,
                                          dev, apex_bridge_groups,
                                          DEVICE_NAME "0");
    if (IS_ERR(dev->dev)) {
        ret = PTR_ERR(dev->dev);
        dev_err(&spi->dev, "Failed to create device: %d\n", ret);
        goto err_destroy_class;
    }

    /* Release MCU from reset */
    apex_mcu_reset_release(dev);

    apex_dev = dev;
    dev_info(&spi->dev, "GhostBlade SPI Bridge driver initialized\n");
    dev_info(&spi->dev, "Device: /dev/%s0, Major: %d, Minor: %d\n",
             DEVICE_NAME, MAJOR(dev->devt), MINOR(dev->devt));

    return 0;

err_destroy_class:
    class_destroy(dev->class);
err_del_cdev:
    cdev_del(&dev->cdev);
err_unreg_chrdev:
    unregister_chrdev_region(dev->devt, APEX_BRIDGE_DEVS);
err_free_irq:
    devm_free_irq(&spi->dev, dev->irq, dev);
err_free_tx_fifo:
    kfifo_free(&dev->tx_fifo);
err_free_rx_fifo:
    kfifo_free(&dev->rx_fifo);
err_free_dev:
    kfree(dev);
    return ret;
}

static void apex_bridge_remove(struct spi_device *spi)
{
    struct apex_bridge_dev *dev = spi_get_drvdata(spi);

    if (!dev)
        return;

    /* Assert MCU reset before shutting down */
    apex_mcu_reset_assert(dev);

    /* Cancel pending work */
    cancel_work_sync(&dev->rx_work);

    /* Destroy device node */
    device_destroy(dev->class, dev->devt);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devt, APEX_BRIDGE_DEVS);

    /* Free IRQ */
    devm_free_irq(&spi->dev, dev->irq, dev);

    /* Free FIFOs */
    kfifo_free(&dev->rx_fifo);
    kfifo_free(&dev->tx_fifo);

    dev_info(&spi->dev, "GhostBlade SPI Bridge driver removed\n");

    kfree(dev);
    apex_dev = NULL;
}

/* ========================================================================
 * Device Tree Match Table
 * ======================================================================== */

static const struct of_device_id apex_bridge_of_match[] = {
    { .compatible = "apex,apex-bridge", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, apex_bridge_of_match);

static const struct spi_device_id apex_bridge_id_table[] = {
    { "apex-bridge", 0 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, apex_bridge_id_table);

static struct spi_driver apex_bridge_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = apex_bridge_of_match,
    },
    .probe = apex_bridge_probe,
    .remove = apex_bridge_remove,
    .id_table = apex_bridge_id_table,
};

module_spi_driver(apex_bridge_driver);

MODULE_AUTHOR("GhostBlade Project");
MODULE_DESCRIPTION("GhostBlade SPI Bridge Driver (RK3576 <-> RP2350B)");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");