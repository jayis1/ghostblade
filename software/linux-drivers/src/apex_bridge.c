// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * apex_bridge.c — Linux Kernel Driver for GhostBlade SPI Bridge
 *
 * Copyright (C) 2026 GhostBlade Project
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
#include <linux/pm_runtime.h>
#include <linux/atomic.h>
#include <linux/pm.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/version.h>

/* Kernel 6.4+ changed class_create() to not require THIS_MODULE */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define apex_class_create(name) class_create(name)
#else
#define apex_class_create(name) class_create(THIS_MODULE, name)
#endif

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
    spinlock_t         rx_lock;       /* Spinlock for RX FIFO and telemetry */
    spinlock_t         tx_lock;       /* Spinlock for TX FIFO */
    wait_queue_head_t  rx_waitq;      /* Wait queue for read() */
    wait_queue_head_t  tx_waitq;      /* Wait queue for write() */
    struct completion   xfer_done;     /* SPI transfer completion */
    struct work_struct  rx_work;       /* Work item for interrupt handling */
    int                irq;            /* INT_REQ GPIO IRQ number */
    int                gpio_int_req;   /* INT_REQ GPIO number */
    int                gpio_host_rdy; /* HOST_RDY GPIO number */
    int                gpio_mcu_reset; /* MCU_RESET GPIO number */
    atomic_t           open_count;     /* Atomic open counter (0=closed, 1=open) */
    struct kfifo       rx_fifo;       /* RX data FIFO */
    struct kfifo       tx_fifo;       /* TX data FIFO */
    struct apex_telemetry last_telem; /* Last telemetry snapshot */
    unsigned long      flags;         /* Driver state flags */
    atomic_t           spi_err_count; /* Cumulative SPI error count */
    atomic_t           brownout_count; /* Cumulative brownout event count */
    atomic_t           brownout_prev_flag; /* Previous LOW_BATTERY flag state for edge detection */
    u8                 saved_spi_mode;       /* Saved SPI mode for PM resume */
    u32                saved_spi_max_speed_hz; /* Saved SPI speed for PM resume */
    struct apex_sg_engine sg_engine;  /* Scatter-gather DMA engine */
};

static struct apex_bridge_dev *apex_dev;

/* Driver state flags */
#define APEX_FLAG_MCU_READY     0
#define APEX_FLAG_MCU_RESET    1
#define APEX_FLAG_SPI_ERROR    2
#define APEX_FLAG_LOW_BATTERY  3

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
        atomic_inc(&dev->spi_err_count);
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

    /* Ensure device is awake before SPI transfer.
     * This is critical because the IRQ handler schedules this work
     * and the device may have entered runtime suspend between the
     * interrupt and the work handler execution. */
    pm_runtime_get_sync(&dev->spi->dev);

    rx_frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!rx_frame)
        goto out_pm_put;

    tx_frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!tx_frame) {
        kfree(rx_frame);
        goto out_pm_put;
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
            uint16_t new_flags;
            spin_lock(&dev->rx_lock);
            memcpy(&dev->last_telem, payload,
                   sizeof(struct apex_telemetry));
            new_flags = le16_to_cpu(dev->last_telem.flags);
            /* Update driver flags from MCU telemetry */
            if (new_flags & APEX_FLAG_LOW_BATTERY) {
                set_bit(APEX_FLAG_LOW_BATTERY, &dev->flags);
                /* Edge detection: count brownout transitions (0→1) */
                if (atomic_xchg(&dev->brownout_prev_flag, 1) == 0)
                    atomic_inc(&dev->brownout_count);
            } else {
                clear_bit(APEX_FLAG_LOW_BATTERY, &dev->flags);
                atomic_set(&dev->brownout_prev_flag, 0);
            }
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
out_pm_put:
    pm_runtime_mark_last_busy(&dev->spi->dev);
    pm_runtime_put_autosuspend(&dev->spi->dev);
}

/* Forward declaration — mmap is defined in the SG engine section below */
static int apex_bridge_mmap(struct file *filp, struct vm_area_struct *vma);

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
    int ret;

    dev = container_of(inode->i_cdev, struct apex_bridge_dev, cdev);
    if (IS_ERR_OR_NULL(dev)) {
        pr_err("apex_bridge: device not found\n");
        return -ENODEV;
    }

    filp->private_data = dev;

    /* Use atomic cmpxchg to ensure only one opener at a time */
    if (atomic_cmpxchg(&dev->open_count, 0, 1) != 0)
        return -EBUSY;  /* Only one user at a time */

    /* Resume device from runtime suspend */
    ret = pm_runtime_get_sync(&dev->spi->dev);
    if (ret < 0) {
        atomic_set(&dev->open_count, 0);
        pm_runtime_put_noidle(&dev->spi->dev);
        return ret;
    }

    return 0;
}

static int apex_bridge_release(struct inode *inode, struct file *filp)
{
    struct apex_bridge_dev *dev = filp->private_data;

    /* Allow device to runtime suspend */
    pm_runtime_put_autosuspend(&dev->spi->dev);
    atomic_set(&dev->open_count, 0);

    /* Securely wipe cached telemetry data on close to prevent
     * information leakage between user sessions. Use memzero_explicit()
     * to prevent the compiler from optimizing away the wipe. */
    memzero_explicit(&dev->last_telem, sizeof(dev->last_telem));

    filp->private_data = NULL;

    return 0;
}

static ssize_t apex_bridge_read(struct file *filp, char __user *buf,
                                 size_t count, loff_t *f_pos)
{
    struct apex_bridge_dev *dev = filp->private_data;
    unsigned int copied;
    int ret;
    unsigned int avail;
    size_t to_copy;
    void *kbuf;

    if (kfifo_is_empty(&dev->rx_fifo)) {
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /* Wait for data */
        ret = wait_event_interruptible(dev->rx_waitq,
                                        !kfifo_is_empty(&dev->rx_fifo));
        if (ret)
            return ret;
    }

    /* Determine how much data is available, then copy to a kernel buffer
     * under the lock, and copy to userspace outside the lock.
     * kfifo_to_user() can sleep (copy_to_user), so we must NOT hold
     * a spinlock while calling it. */
    spin_lock(&dev->rx_lock);
    avail = kfifo_len(&dev->rx_fifo);
    to_copy = min_t(size_t, count, avail);
    if (to_copy == 0) {
        spin_unlock(&dev->rx_lock);
        return 0;
    }
    kbuf = kmalloc(to_copy, GFP_ATOMIC);
    if (!kbuf) {
        spin_unlock(&dev->rx_lock);
        return -ENOMEM;
    }
    copied = kfifo_out(&dev->rx_fifo, kbuf, to_copy);
    spin_unlock(&dev->rx_lock);

    if (copy_to_user(buf, kbuf, copied)) {
        /* Zero kernel buffer before freeing to avoid leaking protocol data */
        kfree_sensitive(kbuf);
        return -EFAULT;
    }

    /* Wipe kernel buffer that may contain protocol/sensor data */
    kfree_sensitive(kbuf);
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

    /* Reject zero-length and oversized writes */
    if (count < 1)
        return -EINVAL;

    /* Limit write size to max payload */
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
        kfree_sensitive(kbuf);
        return -ENOMEM;
    }

    rx_buf = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
    if (!rx_buf) {
        kfree_sensitive(kbuf);
        kfree_sensitive(frame);
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

    /* Validate ioctl command: must use our magic number and valid direction */
    if (_IOC_TYPE(cmd) != APEX_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) < 1 || _IOC_NR(cmd) > 11)
        return -ENOTTY;

    /* Validate direction: write ioctls require write access,
     * read ioctls require read access to the user buffer */
    if (_IOC_DIR(cmd) & _IOC_WRITE) {
        if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
            return -EFAULT;
    }
    if (_IOC_DIR(cmd) & _IOC_READ) {
        if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
            return -EFAULT;
    }

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

        /* Validate reg_len to prevent integer overflow and buffer overread */
        if (cfg.reg_len == 0 || cfg.reg_len > APEX_CC1101_MAX_REG_LEN) {
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
        uint16_t data_len;

        if (copy_from_user(&nfc, (struct apex_nfc_transact __user *)arg,
                           sizeof(nfc))) {
            ret = -EFAULT;
            break;
        }

        data_len = le16_to_cpu(nfc.data_len);

        /* Validate data_len to prevent overflow and excessive allocation */
        if (data_len > APEX_NFC_MAX_DATA_LEN) {
            ret = -EINVAL;
            break;
        }

        nfc_total = sizeof(nfc.cmd) + sizeof(nfc.flags) +
                     sizeof(nfc.data_len) + data_len;
        nfc_buf = kmalloc(nfc_total, GFP_KERNEL);
        if (!nfc_buf) {
            ret = -ENOMEM;
            break;
        }

        nfc_buf[0] = nfc.cmd;
        nfc_buf[1] = nfc.flags;
        put_unaligned_le16(nfc.data_len, &nfc_buf[2]);
        if (data_len > 0 && copy_from_user(&nfc_buf[4],
                           (uint8_t __user *)(arg + offsetof(struct apex_nfc_transact, data)),
                           data_len)) {
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

    case APEX_IOC_SG_START: {
        struct apex_sg_config config;

        if (copy_from_user(&config, (struct apex_sg_config __user *)arg,
                           sizeof(config))) {
            ret = -EFAULT;
            break;
        }

        ret = apex_sg_engine_start(dev, &config);
        break;
    }

    case APEX_IOC_SG_STOP: {
        ret = apex_sg_engine_stop(dev);
        break;
    }

    case APEX_IOC_SG_GET_STATUS: {
        struct apex_sg_status status;

        memset(&status, 0, sizeof(status));

        mutex_lock(&dev->sg_engine.sg_lock);
        status.state = dev->sg_engine.state;
        status.buf_count = dev->sg_engine.buf_count;
        status.buf_size = dev->sg_engine.buf_size;
        status.total_transferred = dev->sg_engine.total_transferred;
        status.overruns = dev->sg_engine.overruns;
        status.errors = dev->sg_engine.errors;
        status.frames_rx = dev->sg_engine.frames_rx;
        status.frames_crc_err = dev->sg_engine.frames_crc_err;
        mutex_unlock(&dev->sg_engine.sg_lock);

        if (copy_to_user((struct apex_sg_status __user *)arg,
                         &status, sizeof(status))) {
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

    kfree_sensitive(frame);
    kfree_sensitive(rx_buf);
    return ret;
}

static __poll_t apex_bridge_poll(struct file *filp,
                                 struct poll_table_struct *wait)
{
    struct apex_bridge_dev *dev = filp->private_data;
    __poll_t mask = 0;

    if (!dev)
        return EPOLLERR;

    poll_wait(filp, &dev->rx_waitq, wait);
    poll_wait(filp, &dev->tx_waitq, wait);

    if (!kfifo_is_empty(&dev->rx_fifo))
        mask |= EPOLLIN | EPOLLRDNORM;

    /* Report SPI error condition */
    if (test_bit(APEX_FLAG_SPI_ERROR, &dev->flags))
        mask |= EPOLLERR;

    /* Report MCU reset as hangup */
    if (test_bit(APEX_FLAG_MCU_RESET, &dev->flags))
        mask |= EPOLLHUP;

    /* Report brownout/low battery as priority event */
    if (test_bit(APEX_FLAG_LOW_BATTERY, &dev->flags))
        mask |= EPOLLPRI;

    /* Writable when TX FIFO has space (or always if FIFO is large enough) */
    if (kfifo_avail(&dev->tx_fifo) > 0)
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
    .mmap           = apex_bridge_mmap,
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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

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

    if (!adev)
        return -ENODEV;

    return sprintf(buf, "%u\n", atomic_read(&adev->spi_err_count));
}
static DEVICE_ATTR_RO(spi_errors);

static ssize_t rx_fifo_count_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    unsigned int count;

    if (!adev)
        return -ENODEV;

    spin_lock(&adev->rx_lock);
    count = kfifo_len(&adev->rx_fifo);
    spin_unlock(&adev->rx_lock);

    return sprintf(buf, "%u\n", count);
}
static DEVICE_ATTR_RO(rx_fifo_count);

static ssize_t tx_fifo_count_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    unsigned int count;

    if (!adev)
        return -ENODEV;

    spin_lock(&adev->tx_lock);
    count = kfifo_len(&adev->tx_fifo);
    spin_unlock(&adev->tx_lock);

    return sprintf(buf, "%u\n", count);
}
static DEVICE_ATTR_RO(tx_fifo_count);

static ssize_t brownout_count_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);

    if (!adev)
        return -ENODEV;

    /* Return cumulative brownout event count (rising-edge transitions
     * of the LOW_BATTERY flag). Each time the flag transitions from
     * cleared to set, the counter increments. This gives a persistent
     * count of how many brownout events have occurred since boot.
     * Use atomic_read for lockless access to the atomic counter. */
    return sprintf(buf, "%u\n", atomic_read(&adev->brownout_count));
}
static DEVICE_ATTR_RO(brownout_count);

static ssize_t low_battery_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);

    if (!adev)
        return -ENODEV;

    return sprintf(buf, "%u\n",
                   test_bit(APEX_FLAG_LOW_BATTERY, &adev->flags) ? 1 : 0);
}
static DEVICE_ATTR_RO(low_battery);

/* ── Scatter-Gather DMA sysfs attributes ──────────────────────────────────── */

static ssize_t sg_state_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    const char *state_str;
    enum apex_sg_state state;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    state = adev->sg_engine.state;
    mutex_unlock(&adev->sg_engine.sg_lock);

    switch (state) {
    case APEX_SG_STATE_IDLE:
        state_str = "idle";
        break;
    case APEX_SG_STATE_RUNNING:
        state_str = "running";
        break;
    case APEX_SG_STATE_ERROR:
        state_str = "error";
        break;
    default:
        state_str = "unknown";
        break;
    }

    return sprintf(buf, "%s\n", state_str);
}
static DEVICE_ATTR_RO(sg_state);

static ssize_t sg_total_bytes_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    u64 total_transferred;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    total_transferred = adev->sg_engine.total_transferred;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%llu\n", total_transferred);
}
static DEVICE_ATTR_RO(sg_total_bytes);

static ssize_t sg_overruns_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    unsigned int overruns;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    overruns = adev->sg_engine.overruns;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%u\n", overruns);
}
static DEVICE_ATTR_RO(sg_overruns);

static ssize_t sg_errors_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    unsigned int errors;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    errors = adev->sg_engine.errors;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%u\n", errors);
}
static DEVICE_ATTR_RO(sg_errors);

static ssize_t sg_frames_rx_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    unsigned int frames_rx;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    frames_rx = adev->sg_engine.frames_rx;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%u\n", frames_rx);
}
static DEVICE_ATTR_RO(sg_frames_rx);

static ssize_t sg_buf_count_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint32_t buf_count;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    buf_count = adev->sg_engine.buf_count;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%u\n", buf_count);
}
static DEVICE_ATTR_RO(sg_buf_count);

static ssize_t sg_buf_size_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    uint32_t buf_size;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    buf_size = adev->sg_engine.buf_size;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%u\n", buf_size);
}
static DEVICE_ATTR_RO(sg_buf_size);

static ssize_t sg_frames_crc_err_show(struct device *dev,
                                         struct device_attribute *attr, char *buf)
{
    struct apex_bridge_dev *adev = dev_get_drvdata(dev);
    unsigned int frames_crc_err;

    if (!adev)
        return -ENODEV;

    mutex_lock(&adev->sg_engine.sg_lock);
    frames_crc_err = adev->sg_engine.frames_crc_err;
    mutex_unlock(&adev->sg_engine.sg_lock);

    return sprintf(buf, "%u\n", frames_crc_err);
}
static DEVICE_ATTR_RO(sg_frames_crc_err);

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
    &dev_attr_rx_fifo_count.attr,
    &dev_attr_tx_fifo_count.attr,
    &dev_attr_brownout_count.attr,
    &dev_attr_low_battery.attr,
    /* Scatter-gather DMA attributes */
    &dev_attr_sg_state.attr,
    &dev_attr_sg_total_bytes.attr,
    &dev_attr_sg_overruns.attr,
    &dev_attr_sg_errors.attr,
    &dev_attr_sg_frames_rx.attr,
    &dev_attr_sg_buf_count.attr,
    &dev_attr_sg_buf_size.attr,
    &dev_attr_sg_frames_crc_err.attr,
    NULL,
};
ATTRIBUTE_GROUPS(apex_bridge);

/* ========================================================================
 * DMA Scatter-Gather Engine for High-Throughput SDR IQ Streaming
 * ========================================================================
 *
 * The SG engine provides zero-copy IQ data streaming from the RP2350B to
 * userspace. It allocates DMA-coherent buffers and uses the SPI controller
 * in DMA mode with scatter-gather to fill them directly, avoiding the
 * intermediate kfifo copy path used by the character device read().
 *
 * Architecture:
 *   1. User calls APEX_IOC_SG_START with buffer count/size config
 *   2. Driver allocates DMA-coherent buffers and sets up scatterlist
 *   3. SPI transfers fill buffers in round-robin order
 *   4. Each completed buffer transitions: FREE -> DMA_ACTIVE -> READY
 *   5. Userspace mmaps buffers and reads data directly
 *   6. APEX_IOC_SG_STOP tears down the engine and frees buffers
 *
 * The engine runs as a workqueue that continuously schedules SPI DMA
 * transfers. When a buffer is filled, it's marked READY and userspace
 * is notified via the poll()/epoll interface.
 */

#define APEX_SG_DESC_SIZE      sizeof(struct apex_sg_buf_desc)

struct apex_sg_buf {
    void               *dma_virt;       /* DMA-coherent virtual address */
    dma_addr_t          dma_phys;        /* DMA bus address */
    struct apex_sg_buf_desc desc;        /* Buffer descriptor */
};

struct apex_sg_engine {
    struct apex_sg_buf  *bufs;           /* Array of SG buffers */
    struct scatterlist  *sgl;            /* Scatterlist for DMA mapping */
    uint32_t             buf_count;      /* Number of buffers */
    uint32_t             buf_size;       /* Size of each buffer */
    uint32_t             current_idx;    /* Next buffer to fill */
    uint32_t             state;          /* APEX_SG_STATE_* */
    uint64_t             total_transferred;
    uint32_t             overruns;
    uint32_t             errors;
    uint32_t             frames_rx;
    uint32_t             frames_crc_err;
    uint32_t             sequence;       /* Monotonic sequence counter */
    uint32_t             timeout_ms;    /* DMA completion timeout */
    struct work_struct   sg_work;        /* Work item for SG processing */
    struct completion    sg_buf_complete; /* DMA completion signal */
    wait_queue_head_t   sg_waitq;        /* Userspace wait queue for READY bufs */
    struct mutex         sg_lock;        /* Protect SG engine state */
    bool                 continuous;     /* Continuous streaming mode */
    struct device        *dma_dev;       /* Device for DMA mapping */
};

/* ── SG Engine Helper Functions ────────────────────────────────────────── */

static void apex_sg_buf_set_state(struct apex_sg_engine *eng,
                                   uint32_t idx, uint32_t state)
{
    if (idx < eng->buf_count) {
        eng->bufs[idx].desc.state = state;
        if (state == APEX_SG_BUF_READY)
            wake_up_interruptible(&eng->sg_waitq);
    }
}

/*
 * apex_sg_dma_callback — DMA transfer completion callback
 *
 * Called from interrupt context when a SPI DMA transfer completes.
 * Marks the buffer as READY and schedules the next transfer.
 */
static void apex_sg_dma_callback(void *context)
{
    struct apex_sg_engine *eng = context;
    uint32_t idx = eng->current_idx;
    uint32_t buf_size = eng->buf_size;

    /* Mark current buffer as ready */
    eng->bufs[idx].desc.data_len = buf_size;
    eng->bufs[idx].desc.timestamp_ns = ktime_get_real_ns();
    eng->bufs[idx].desc.sequence = eng->sequence++;
    eng->bufs[idx].desc.state = APEX_SG_BUF_READY;

    eng->total_transferred += buf_size;
    eng->frames_rx++;

    /* Advance to next buffer (round-robin) */
    eng->current_idx = (eng->current_idx + 1) % eng->buf_count;

    /* Signal completion so the work function schedules the next transfer */
    complete(&eng->sg_buf_complete);
}

/*
 * apex_sg_work_handler — Workqueue handler for SG DMA processing
 *
 * Continuously schedules SPI DMA transfers in round-robin order
 * through the SG buffers. In continuous mode, it keeps cycling
 * until explicitly stopped. In single-batch mode, it stops when
 * all buffers have been filled once.
 */
static void apex_sg_work_handler(struct work_struct *work)
{
    struct apex_sg_engine *eng =
        container_of(work, struct apex_sg_engine, sg_work);
    struct apex_bridge_dev *adev =
        container_of(eng, struct apex_bridge_dev, sg_engine);

    while (eng->state == APEX_SG_STATE_RUNNING) {
        uint32_t idx = eng->current_idx;
        struct apex_sg_buf *buf = &eng->bufs[idx];
        struct spi_transfer xfer;
        struct spi_message msg;
        int ret;

        /* Skip buffers still in use by userspace */
        if (buf->desc.state == APEX_SG_BUF_USERSPACE) {
            eng->overruns++;
            /* Try next buffer */
            eng->current_idx = (idx + 1) % eng->buf_count;
            continue;
        }

        /* Mark buffer as DMA active */
        apex_sg_buf_set_state(eng, idx, APEX_SG_BUF_DMA_ACTIVE);

        /* Prepare SPI DMA transfer */
        memset(&xfer, 0, sizeof(xfer));
        xfer.rx_buf = buf->dma_virt;
        xfer.len = eng->buf_size;
        /* Use configured SPI speed when running, fall back to
         * a conservative 10 MHz for error recovery transfers */
        xfer.speed_hz = eng->state == APEX_SG_STATE_RUNNING ?
                         spi_speed_hz : min(spi_speed_hz, 10000000);

        spi_message_init_with_transfers(&msg, &xfer, 1);

        /* Set DMA completion callback */
        msg.complete = apex_sg_dma_callback;
        msg.context = eng;

        /* Submit SPI transfer with DMA */
        mutex_lock(&adev->lock);
        ret = spi_async(adev->spi, &msg);
        mutex_unlock(&adev->lock);

        if (ret < 0) {
            dev_err(&adev->spi->dev,
                    "SG DMA transfer failed: %d\n", ret);
            eng->errors++;
            apex_sg_buf_set_state(eng, idx, APEX_SG_BUF_FREE);

            if (eng->errors > 100) {
                eng->state = APEX_SG_STATE_ERROR;
                break;
            }

            /* Brief delay before retry */
            msleep(1);
            continue;
        }

        /* Wait for DMA completion with timeout */
        if (eng->timeout_ms > 0) {
            ret = wait_for_completion_timeout(&eng->sg_buf_complete,
                    msecs_to_jiffies(eng->timeout_ms));
            if (ret == 0) {
                dev_warn(&adev->spi->dev,
                         "SG DMA timeout on buffer %u\n", idx);
                eng->errors++;
                apex_sg_buf_set_state(eng, idx, APEX_SG_BUF_FREE);
            }
        } else {
            wait_for_completion(&eng->sg_buf_complete);
        }

        /* Re-initialize completion for next buffer */
        reinit_completion(&eng->sg_buf_complete);

        /* In single-batch mode, stop after filling all buffers once */
        if (!eng->continuous && eng->sequence >= eng->buf_count) {
            eng->state = APEX_SG_STATE_IDLE;
            break;
        }
    }

    dev_info(&adev->spi->dev, "SG engine stopped: %u frames, %llu bytes, %u errors\n",
             eng->frames_rx, eng->total_transferred, eng->errors);
}

/* ── SG Engine Lifecycle ────────────────────────────────────────────────── */

/*
 * apex_sg_engine_init — Initialize SG engine state (called at probe)
 */
static void apex_sg_engine_init(struct apex_bridge_dev *dev)
{
    struct apex_sg_engine *eng = &dev->sg_engine;

    memset(eng, 0, sizeof(*eng));
    eng->state = APEX_SG_STATE_IDLE;
    mutex_init(&eng->sg_lock);
    init_waitqueue_head(&eng->sg_waitq);
    init_completion(&eng->sg_buf_complete);
}

/*
 * apex_sg_engine_start — Allocate buffers and start SG DMA streaming
 *
 * @dev: Bridge device
 * @config: SG configuration from userspace
 *
 * Returns: 0 on success, negative error code on failure
 */
static int apex_sg_engine_start(struct apex_bridge_dev *dev,
                                 struct apex_sg_config *config)
{
    struct apex_sg_engine *eng = &dev->sg_engine;
    uint32_t i;
    int ret;

    if (config->buf_count < 2 || config->buf_count > APEX_SG_MAX_BUFS)
        return -EINVAL;

    if (config->buf_size < APEX_SG_BUF_SIZE_MIN ||
        config->buf_size > APEX_SG_BUF_SIZE_MAX)
        return -EINVAL;

    if (config->buf_size % APEX_SG_BUF_SIZE_ALIGN != 0)
        return -EINVAL;

    mutex_lock(&eng->sg_lock);

    if (eng->state != APEX_SG_STATE_IDLE) {
        mutex_unlock(&eng->sg_lock);
        return -EBUSY;
    }

    /* Allocate buffer array */
    eng->bufs = kcalloc(config->buf_count, sizeof(*eng->bufs), GFP_KERNEL);
    if (!eng->bufs) {
        mutex_unlock(&eng->sg_lock);
        return -ENOMEM;
    }

    /* Allocate scatterlist */
    eng->sgl = kcalloc(config->buf_count, sizeof(*eng->sgl), GFP_KERNEL);
    if (!eng->sgl) {
        kfree(eng->bufs);
        eng->bufs = NULL;
        mutex_unlock(&eng->sg_lock);
        return -ENOMEM;
    }

    sg_init_table(eng->sgl, config->buf_count);

    /* Allocate DMA-coherent buffers */
    for (i = 0; i < config->buf_count; i++) {
        eng->bufs[i].dma_virt = dma_alloc_coherent(&dev->spi->dev,
                                    config->buf_size,
                                    &eng->bufs[i].dma_phys,
                                    GFP_KERNEL);
        if (!eng->bufs[i].dma_virt) {
            /* Rollback: free all previously allocated buffers */
            while (i > 0) {
                i--;
                dma_free_coherent(&dev->spi->dev, config->buf_size,
                                  eng->bufs[i].dma_virt,
                                  eng->bufs[i].dma_phys);
            }
            kfree(eng->sgl);
            kfree(eng->bufs);
            eng->bufs = NULL;
            eng->sgl = NULL;
            mutex_unlock(&eng->sg_lock);
            return -ENOMEM;
        }

        /* Initialize buffer descriptor */
        eng->bufs[i].desc.buf_index = i;
        eng->bufs[i].desc.data_len = 0;
        eng->bufs[i].desc.timestamp_ns = 0;
        eng->bufs[i].desc.sequence = 0;
        eng->bufs[i].desc.state = APEX_SG_BUF_FREE;

        /* Set up scatterlist entry */
        sg_set_buf(&eng->sgl[i], eng->bufs[i].dma_virt, config->buf_size);
    }

    eng->buf_count = config->buf_count;
    eng->buf_size = config->buf_size;
    eng->timeout_ms = config->timeout_ms;
    eng->continuous = config->continuous ? true : false;
    eng->current_idx = 0;
    eng->total_transferred = 0;
    eng->overruns = 0;
    eng->errors = 0;
    eng->frames_rx = 0;
    eng->frames_crc_err = 0;
    eng->sequence = 0;
    eng->dma_dev = &dev->spi->dev;

    /* Override SPI speed if requested */
    if (config->spi_speed_hz > 0 && config->spi_speed_hz <= 50000000)
        spi_speed_hz = config->spi_speed_hz;

    /* Initialize work item */
    INIT_WORK(&eng->sg_work, apex_sg_work_handler);
    reinit_completion(&eng->sg_buf_complete);

    /* Start the engine */
    eng->state = APEX_SG_STATE_RUNNING;

    /* Send SDR stream start command to MCU */
    {
        uint8_t enable = 1;
        uint8_t *frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
        uint8_t *rx = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
        int frame_len;

        if (frame && rx) {
            frame_len = apex_build_frame(APEX_CMD_SDR_STREAM, &enable,
                                          sizeof(enable), frame,
                                          APEX_SPI_FRAME_SIZE_MAX);
            if (frame_len > 0)
                apex_spi_xfer(dev, frame, frame_len, rx,
                              APEX_SPI_FRAME_SIZE_MAX);
        }
        kfree_sensitive(frame);
        kfree_sensitive(rx);
    }

    /* Schedule the SG work */
    schedule_work(&eng->sg_work);

    mutex_unlock(&eng->sg_lock);

    dev_info(&dev->spi->dev,
             "SG engine started: %u buffers × %u bytes, continuous=%u\n",
             config->buf_count, config->buf_size, config->continuous);

    return 0;
}

/*
 * apex_sg_engine_stop — Stop SG DMA streaming and free buffers
 *
 * @dev: Bridge device
 *
 * Returns: 0 on success, negative error code on failure
 */
static int apex_sg_engine_stop(struct apex_bridge_dev *dev)
{
    struct apex_sg_engine *eng = &dev->sg_engine;
    uint32_t i;

    mutex_lock(&eng->sg_lock);

    if (eng->state == APEX_SG_STATE_IDLE) {
        mutex_unlock(&eng->sg_lock);
        return 0;  /* Already stopped */
    }

    /* Signal the engine to stop */
    eng->state = APEX_SG_STATE_IDLE;

    /* Cancel pending work */
    cancel_work_sync(&eng->sg_work);

    /* Send SDR stream stop command to MCU */
    {
        uint8_t disable = 0;
        uint8_t *frame = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
        uint8_t *rx = kmalloc(APEX_SPI_FRAME_SIZE_MAX, GFP_KERNEL);
        int frame_len;

        if (frame && rx) {
            frame_len = apex_build_frame(APEX_CMD_SDR_STREAM, &disable,
                                          sizeof(disable), frame,
                                          APEX_SPI_FRAME_SIZE_MAX);
            if (frame_len > 0)
                apex_spi_xfer(dev, frame, frame_len, rx,
                              APEX_SPI_FRAME_SIZE_MAX);
        }
        kfree_sensitive(frame);
        kfree_sensitive(rx);
    }

    /* Free DMA-coherent buffers (wipe sensitive data before freeing to prevent
     * leakage of IQ samples or sensor data). Use memzero_explicit() instead of
     * memset() to prevent the compiler from optimizing away the wipe — the
     * compiler can prove the memory is about to be freed and remove the
     * memset, leaving sensitive data in DMA-coherent memory that may be
     * reallocated to another driver. */
    if (eng->bufs) {
        for (i = 0; i < eng->buf_count; i++) {
            if (eng->bufs[i].dma_virt) {
                memzero_explicit(eng->bufs[i].dma_virt, eng->buf_size);
                dma_free_coherent(&dev->spi->dev, eng->buf_size,
                                  eng->bufs[i].dma_virt,
                                  eng->bufs[i].dma_phys);
            }
        }
        kfree(eng->bufs);
        eng->bufs = NULL;
    }

    kfree(eng->sgl);
    eng->sgl = NULL;

    dev_info(&dev->spi->dev,
             "SG engine stopped: %u frames, %llu bytes, %u overruns, %u errors\n",
             eng->frames_rx, eng->total_transferred, eng->overruns, eng->errors);

    mutex_unlock(&eng->sg_lock);
    return 0;
}

/* ── SG mmap support ───────────────────────────────────────────────────── */

/*
 * apex_sg_mmap — Map SG buffers to userspace
 *
 * Allows userspace to mmap the SG buffer pool as a contiguous region.
 * Each buffer's descriptor is placed at the start of the buffer region,
 * allowing userspace to determine which buffers contain valid data.
 *
 * Uses dma_mmap_coherent() for proper mapping of DMA-coherent memory.
 * This is the correct Linux kernel API for mmap-ing DMA-coherent buffers
 * and works correctly on both cache-coherent and non-cache-coherent
 * architectures (including ARM with writecombine mappings).
 *
 * Previously used remap_pfn_range() with __phys_to_pfn() which is
 * incorrect for DMA-coherent allocations and can cause cache aliasing
 * issues on ARM platforms.
 */
static int apex_bridge_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct apex_bridge_dev *dev = filp->private_data;
    struct apex_sg_engine *eng = &dev->sg_engine;
    unsigned long vsize = vma->vm_end - vma->vm_start;
    unsigned long psize;
    int ret;

    mutex_lock(&eng->sg_lock);
    if (eng->state == APEX_SG_STATE_IDLE) {
        mutex_unlock(&eng->sg_lock);
        return -ENODEV;
    }

    psize = (unsigned long)eng->buf_count * eng->buf_size;
    mutex_unlock(&eng->sg_lock);

    if (vsize > psize)
        return -EINVAL;

    /* Validate that the VMA offset is page-aligned and within range */
    if (vma->vm_pgoff >= (psize >> PAGE_SHIFT))
        return -EINVAL;

    /* Set VM flags for device/DMA mapping:
     * VM_DONTEXPAND prevents mremap from expanding the mapping,
     * VM_IO marks it as device I/O memory (affects /proc/pid/maps).
     */
    vma->vm_flags |= VM_DONTEXPAND | VM_IO;

    /* Use dma_mmap_coherent() for proper DMA buffer mapping.
     * This handles cache coherency correctly on all architectures,
     * including ARM with writecombine mappings. It replaces the
     * previous per-buffer remap_pfn_range() approach which could
     * cause cache aliasing issues on non-cache-coherent systems. */
    ret = dma_mmap_coherent(&dev->spi->dev, vma, eng->bufs[0].dma_virt,
                             eng->bufs[0].dma_phys, psize);
    if (ret)
        return ret;

    return 0;
}

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
    atomic_set(&dev->spi_err_count, 0);
    atomic_set(&dev->brownout_count, 0);
    atomic_set(&dev->brownout_prev_flag, 0);
    mutex_init(&dev->lock);
    spin_lock_init(&dev->rx_lock);
    spin_lock_init(&dev->tx_lock);
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
    dev->class = apex_class_create(CLASS_NAME);
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

    /* Enable runtime power management */
    pm_runtime_set_autosuspend_delay(&spi->dev, 5000);  /* 5 second autosuspend */
    pm_runtime_use_autosuspend(&spi->dev);
    pm_runtime_enable(&spi->dev);
    pm_runtime_get_sync(&spi->dev);  /* Hold active during probe */

    /* Initialize scatter-gather DMA engine */
    apex_sg_engine_init(dev);

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

    /* Disable runtime power management */
    pm_runtime_put_sync(&spi->dev);
    pm_runtime_disable(&spi->dev);

    /* Cancel pending work */
    cancel_work_sync(&dev->rx_work);

    /* Stop scatter-gather engine if running */
    apex_sg_engine_stop(dev);

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

/* ========================================================================
 * Runtime Power Management
 * ======================================================================== */

static int apex_bridge_runtime_suspend(struct device *dev)
{
    struct spi_device *spi = to_spi_device(dev);
    struct apex_bridge_dev *adev = spi_get_drvdata(spi);

    dev_dbg(dev, "apex_bridge: runtime suspend\n");

    /* Save current SPI configuration for restore on resume */
    adev->saved_spi_mode = spi->mode;
    adev->saved_spi_max_speed_hz = spi->max_speed_hz;

    /* Disable SPI transfers by lowering clock speed to minimum.
     * We keep the SPI bus powered to allow the MCU to still signal
     * interrupts, but the low speed ensures minimal power consumption
     * on the SPI bus lines. */
    spi->max_speed_hz = 1000000;  /* 1 MHz — low power mode */
    spi_setup(spi);

    /* Free the SPI IRQ during suspend to avoid spurious wakeups
     * from the MCU INT_REQ line while the host is idle. The IRQ
     * will be re-requested in runtime_resume. */
    if (adev->irq >= 0) {
        disable_irq(adev->irq);
    }

    return 0;
}

static int apex_bridge_runtime_resume(struct device *dev)
{
    struct spi_device *spi = to_spi_device(dev);
    struct apex_bridge_dev *adev = spi_get_drvdata(spi);

    dev_dbg(dev, "apex_bridge: runtime resume\n");

    /* Restore original SPI configuration */
    spi->mode = adev->saved_spi_mode;
    spi->max_speed_hz = adev->saved_spi_max_speed_hz;
    spi_setup(spi);

    /* Re-enable the SPI IRQ */
    if (adev->irq >= 0) {
        enable_irq(adev->irq);
    }

    return 0;
}

static int apex_bridge_runtime_idle(struct device *dev)
{
    struct spi_device *spi = to_spi_device(dev);
    struct apex_bridge_dev *adev = spi_get_drvdata(spi);

    /* Allow runtime suspend only when no open handles */
    if (atomic_read(&adev->open_count) == 0)
        return pm_runtime_suspend(dev);

    return -EBUSY;
}

static const struct dev_pm_ops apex_bridge_pm_ops = {
    SET_RUNTIME_PM_OPS(apex_bridge_runtime_suspend,
                       apex_bridge_runtime_resume,
                       apex_bridge_runtime_idle)
    SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
                            pm_runtime_force_resume)
};

static struct spi_driver apex_bridge_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = apex_bridge_of_match,
        .pm = &apex_bridge_pm_ops,
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