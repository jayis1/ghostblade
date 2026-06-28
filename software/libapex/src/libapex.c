/*
 * libapex.c — Userspace Library for GhostBlade SPI Bridge Device
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implementation of the libapex API. This library wraps the kernel
 * driver's ioctl interface and provides a clean, typed C API for
 * controlling the GhostBlade pentesting hardware.
 */

#include "libapex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* ========================================================================
 * Secure memory wipe — prevent compiler from optimizing away memset
 * ======================================================================== */

static void secure_wipe(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--)
        *p++ = 0;
}

/* ========================================================================
 * Kernel ioctl definitions (must match apex_bridge_regs.h)
 * ======================================================================== */

#define APEX_IOC_MAGIC         'A'

struct kernel_sdr_tune_cmd {
    uint32_t freq_hz;
    uint16_t bw_khz;
    uint16_t gain_db_x10;
} __attribute__((packed));

struct kernel_telemetry {
    uint16_t rssi_dbm_x10;
    uint16_t temp_c_x10;
    uint16_t vbat_mv;
    uint16_t cc1101_rssi_x10;
    uint16_t nfc_field_mv;
    uint16_t flags;
    uint32_t uptime_ms;
} __attribute__((packed));

struct kernel_cc1101_cfg {
    uint8_t  reg_addr;
    uint8_t  reg_len;
    uint8_t  data[64];
} __attribute__((packed));

struct kernel_nfc_transact {
    uint8_t  cmd;
    uint8_t  flags;
    uint16_t data_len;
    uint8_t  data[256];
} __attribute__((packed));

#define IOC_SDR_TUNE      _IOW(APEX_IOC_MAGIC, 1, struct kernel_sdr_tune_cmd)
#define IOC_SDR_STREAM    _IOW(APEX_IOC_MAGIC, 2, uint8_t)
#define IOC_ANT_SELECT    _IOW(APEX_IOC_MAGIC, 3, uint8_t)
#define IOC_CC1101_CFG    _IOW(APEX_IOC_MAGIC, 4, struct kernel_cc1101_cfg)
#define IOC_NFC_TRANSACT  _IOW(APEX_IOC_MAGIC, 5, struct kernel_nfc_transact)
#define IOC_GET_TELEMETRY _IOR(APEX_IOC_MAGIC, 6, struct kernel_telemetry)
#define IOC_MCU_RESET     _IOW(APEX_IOC_MAGIC, 7, uint8_t)
#define IOC_GET_STATUS    _IOR(APEX_IOC_MAGIC, 8, uint32_t)

/* ========================================================================
 * Device Handle Structure
 * ======================================================================== */

struct apex_device {
    int fd;              /* File descriptor for /dev/apex_bridge0 */
    int last_error;      /* Last error code */
    char device_path[64]; /* Device path for diagnostics */
};

/* Default device path */
#define APEX_DEFAULT_DEVICE  "/dev/apex_bridge0"

/* ========================================================================
 * Error String Table
 * ======================================================================== */

static const char *error_strings[] = {
    "Success",                        /* APEX_OK */
    "Invalid argument",               /* APEX_ERR_INVALID_ARG */
    "Device not found",               /* APEX_ERR_NO_DEVICE */
    "Failed to open device",          /* APEX_ERR_OPEN_FAILED */
    "ioctl failed",                   /* APEX_ERR_IOCTL_FAILED */
    "Operation timed out",            /* APEX_ERR_TIMEOUT */
    "Communication error",            /* APEX_ERR_COMM */
    "Device not ready",              /* APEX_ERR_NOT_READY */
    "Out of memory",                 /* APEX_ERR_NOMEM */
};

/* ========================================================================
 * Device Management
 * ======================================================================== */

apex_handle_t apex_open(const char *device_path) {
    struct apex_device *dev;
    const char *path = device_path ? device_path : APEX_DEFAULT_DEVICE;

    dev = (struct apex_device *)calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;

    dev->fd = open(path, O_RDWR);
    if (dev->fd < 0) {
        free(dev);
        return NULL;
    }

    strncpy(dev->device_path, path, sizeof(dev->device_path) - 1);
    dev->device_path[sizeof(dev->device_path) - 1] = '\0';
    dev->last_error = APEX_OK;

    return dev;
}

void apex_close(apex_handle_t handle) {
    if (!handle)
        return;

    if (handle->fd >= 0)
        close(handle->fd);

    free(handle);
}

int apex_get_fd(apex_handle_t handle) {
    if (!handle)
        return -1;
    return handle->fd;
}

int apex_last_error(apex_handle_t handle) {
    if (!handle)
        return APEX_ERR_INVALID_ARG;
    return handle->last_error;
}

const char *apex_strerror(int error) {
    int idx = -error;
    if (idx < 0 || idx >= (int)(sizeof(error_strings) / sizeof(error_strings[0])))
        return "Unknown error";
    return error_strings[idx];
}

/* ========================================================================
 * SDR Control
 * ======================================================================== */

int apex_sdr_tune(apex_handle_t handle, uint32_t freq_hz,
                  uint16_t bw_khz, float gain_db) {
    struct kernel_sdr_tune_cmd cmd;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    if (freq_hz < 100000 || freq_hz > 3800000000UL)
        return APEX_ERR_INVALID_ARG;

    cmd.freq_hz = freq_hz;
    cmd.bw_khz = bw_khz;
    cmd.gain_db_x10 = (uint16_t)(gain_db * 10.0f);

    if (ioctl(handle->fd, IOC_SDR_TUNE, &cmd) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_sdr_stream_start(apex_handle_t handle) {
    uint8_t val = 1;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    if (ioctl(handle->fd, IOC_SDR_STREAM, &val) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_sdr_stream_stop(apex_handle_t handle) {
    uint8_t val = 0;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    if (ioctl(handle->fd, IOC_SDR_STREAM, &val) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

/* ========================================================================
 * Antenna Control
 * ======================================================================== */

int apex_ant_select(apex_handle_t handle, apex_antenna_t ant) {
    uint8_t val;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    if (ant > APEX_ANT_TERMINATED)
        return APEX_ERR_INVALID_ARG;

    val = (uint8_t)ant;

    if (ioctl(handle->fd, IOC_ANT_SELECT, &val) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

/* ========================================================================
 * CC1101 Sub-GHz Radio
 * ======================================================================== */

int apex_cc1101_write_regs(apex_handle_t handle,
                            const apex_cc1101_config_t *cfg) {
    struct kernel_cc1101_cfg kcmd;

    if (!handle || handle->fd < 0 || !cfg)
        return APEX_ERR_INVALID_ARG;

    if (cfg->reg_len == 0 || cfg->reg_len > 64)
        return APEX_ERR_INVALID_ARG;

    /* Validate register address range (CC1101 config: 0x00-0x2E, status: 0x30-0x3D) */
    if (cfg->reg_addr > 0x3D)
        return APEX_ERR_INVALID_ARG;

    kcmd.reg_addr = cfg->reg_addr;
    kcmd.reg_len = cfg->reg_len;
    memcpy(kcmd.data, cfg->data, cfg->reg_len);

    if (ioctl(handle->fd, IOC_CC1101_CFG, &kcmd) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_cc1101_set_channel(apex_handle_t handle, uint8_t channel) {
    apex_cc1101_config_t cfg;

    /* CC1101 CHANNR register is at address 0x0A */
    cfg.reg_addr = 0x0A;
    cfg.reg_len = 1;
    cfg.data[0] = channel;

    return apex_cc1101_write_regs(handle, &cfg);
}

int apex_cc1101_set_power(apex_handle_t handle, int8_t power_dbm) {
    apex_cc1101_config_t cfg;
    uint8_t pa_val;

    /* Map dBm to CC1101 PA_POWER setting (simplified) */
    if (power_dbm <= -30)       pa_val = 0x00;
    else if (power_dbm <= -20)  pa_val = 0x01;
    else if (power_dbm <= -15)  pa_val = 0x02;
    else if (power_dbm <= -10)  pa_val = 0x34;
    else if (power_dbm <= 0)    pa_val = 0x60;
    else if (power_dbm <= 5)    pa_val = 0x84;
    else if (power_dbm <= 7)    pa_val = 0xA0;
    else                        pa_val = 0xC0;  /* +10 dBm */

    /* FREND0 register is at address 0x22 */
    cfg.reg_addr = 0x22;
    cfg.reg_len = 1;
    cfg.data[0] = pa_val;

    return apex_cc1101_write_regs(handle, &cfg);
}

/* ========================================================================
 * NFC Controller
 * ======================================================================== */

int apex_nfc_transact(apex_handle_t handle, apex_nfc_transact_t *txn) {
    struct kernel_nfc_transact ktxn;

    if (!handle || handle->fd < 0 || !txn)
        return APEX_ERR_INVALID_ARG;

    if (txn->data_len > 256)
        return APEX_ERR_INVALID_ARG;

    ktxn.cmd = txn->cmd;
    ktxn.flags = txn->flags;
    ktxn.data_len = txn->data_len;
    if (txn->data_len > 0)
        memcpy(ktxn.data, txn->data, txn->data_len);

    if (ioctl(handle->fd, IOC_NFC_TRANSACT, &ktxn) < 0) {
        /* Wipe sensitive NFC data from stack before returning error */
        secure_wipe(&ktxn, sizeof(ktxn));
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    /* Copy back response data to the caller's buffer.
     * The kernel driver writes response data into ktxn.data and
     * updates ktxn.data_len with the response length.
     * Clamp the response length to the buffer size to prevent
     * reporting more bytes than were actually copied.
     */
    if (ktxn.data_len > 0) {
        size_t copy_len = ktxn.data_len;
        if (copy_len > sizeof(ktxn.data))
            copy_len = sizeof(ktxn.data);
        if (copy_len > sizeof(txn->data))
            copy_len = sizeof(txn->data);
        memcpy(txn->data, ktxn.data, copy_len);
        txn->data_len = (uint16_t)copy_len;
    } else {
        txn->data_len = 0;
    }

    /* Wipe kernel transaction struct from stack — NFC data is sensitive */
    secure_wipe(&ktxn, sizeof(ktxn));

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_nfc_field_on(apex_handle_t handle) {
    apex_nfc_transact_t txn;

    memset(&txn, 0, sizeof(txn));
    txn.cmd = 0x01;  /* NFC CMD: field on */
    txn.flags = 0x00;

    return apex_nfc_transact(handle, &txn);
}

int apex_nfc_field_off(apex_handle_t handle) {
    apex_nfc_transact_t txn;

    memset(&txn, 0, sizeof(txn));
    txn.cmd = 0x02;  /* NFC CMD: field off */
    txn.flags = 0x00;

    return apex_nfc_transact(handle, &txn);
}

/* ========================================================================
 * Telemetry & Status
 * ======================================================================== */

int apex_get_telemetry(apex_handle_t handle, apex_telemetry_t *telem) {
    struct kernel_telemetry ktelem;

    if (!handle || handle->fd < 0 || !telem)
        return APEX_ERR_INVALID_ARG;

    if (ioctl(handle->fd, IOC_GET_TELEMETRY, &ktelem) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    /* Convert from kernel wire format to library types */
    telem->rssi_dbm_x10    = (int16_t)ktelem.rssi_dbm_x10;
    telem->temp_c_x10      = (int16_t)ktelem.temp_c_x10;
    telem->vbat_mv         = ktelem.vbat_mv;
    telem->cc1101_rssi_x10 = (int16_t)ktelem.cc1101_rssi_x10;
    telem->nfc_field_mv    = ktelem.nfc_field_mv;
    telem->flags           = ktelem.flags;
    telem->uptime_ms       = ktelem.uptime_ms;

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_get_status(apex_handle_t handle, apex_status_t *status) {
    uint32_t drv_flags = 0;

    if (!handle || handle->fd < 0 || !status)
        return APEX_ERR_INVALID_ARG;

    if (ioctl(handle->fd, IOC_GET_STATUS, &drv_flags) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    status->driver_flags = drv_flags;
    status->mcu_ready    = !!(drv_flags & 0x01);
    status->mcu_in_reset = !!(drv_flags & 0x02);
    status->spi_error    = !!(drv_flags & 0x04);

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_mcu_reset(apex_handle_t handle, bool assert) {
    uint8_t val = assert ? 1 : 0;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    if (ioctl(handle->fd, IOC_MCU_RESET, &val) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

/* ========================================================================
 * CC1101 Read Registers
 * ======================================================================== */

int apex_cc1101_read_regs(apex_handle_t handle, apex_cc1101_config_t *cfg) {
    struct kernel_cc1101_cfg kcmd;

    if (!handle || handle->fd < 0 || !cfg)
        return APEX_ERR_INVALID_ARG;

    if (cfg->reg_len == 0 || cfg->reg_len > 64)
        return APEX_ERR_INVALID_ARG;

    /* Validate register address range */
    if (cfg->reg_addr > 0x3D)
        return APEX_ERR_INVALID_ARG;

    kcmd.reg_addr = cfg->reg_addr;
    kcmd.reg_len = cfg->reg_len;
    memset(kcmd.data, 0, sizeof(kcmd.data));

    /* Use a dedicated ioctl for reading.
     * IOC_CC1101_CFG is bidirectional: on input, reg_addr and reg_len
     * specify what to read; on output, data is filled with values. */
    if (ioctl(handle->fd, IOC_CC1101_CFG, &kcmd) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    /* Copy read data back to caller */
    memcpy(cfg->data, kcmd.data, cfg->reg_len);

    handle->last_error = APEX_OK;
    return APEX_OK;
}

int apex_cc1101_set_band(apex_handle_t handle, int band) {
    struct kernel_cc1101_cfg kcmd;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    /* Validate band: 0=433 MHz, 1=868 MHz, 2=915 MHz */
    if (band < 0 || band > 2)
        return APEX_ERR_INVALID_ARG;

    /* Send a band-switch command via CC1101_CFG ioctl.
     * Use reg_addr = 0xFF as a sentinel to indicate band switch,
     * reg_len = band number. */
    memset(&kcmd, 0, sizeof(kcmd));
    kcmd.reg_addr = 0xFF;  /* Sentinel: band switch command */
    kcmd.reg_len = (uint8_t)band;
    kcmd.data[0] = (uint8_t)band;

    if (ioctl(handle->fd, IOC_CC1101_CFG, &kcmd) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    handle->last_error = APEX_OK;
    return APEX_OK;
}

/* ========================================================================
 * SDR IQ Read
 * ======================================================================== */

int apex_sdr_read_iq(apex_handle_t handle, uint8_t *buf,
                      size_t buf_len, size_t *samples_read) {
    ssize_t nread;

    if (!handle || handle->fd < 0 || !buf || buf_len == 0)
        return APEX_ERR_INVALID_ARG;

    if (samples_read)
        *samples_read = 0;

    /* Read from the device file — the kernel driver provides IQ samples
     * via read() on the character device when streaming is active. */
    nread = read(handle->fd, buf, buf_len);
    if (nread < 0) {
        handle->last_error = APEX_ERR_COMM;
        return APEX_ERR_COMM;
    }

    if (samples_read)
        *samples_read = (size_t)nread;

    handle->last_error = APEX_OK;
    return APEX_OK;
}

/* ========================================================================
 * NFC Polling
 * ======================================================================== */

int apex_nfc_poll(apex_handle_t handle, uint32_t timeout_ms) {
    apex_nfc_transact_t txn;

    if (!handle || handle->fd < 0)
        return APEX_ERR_INVALID_ARG;

    /* Send REQA command (0x26) for ISO 14443A tag detection.
     * NFC_CMD_POLL = 0x03 in the command namespace. */
    memset(&txn, 0, sizeof(txn));
    txn.cmd = 0x03;  /* NFC_CMD_POLL */
    txn.flags = (uint8_t)((timeout_ms > 0) ? 0x01 : 0x00);  /* FLAG_TIMEOUT */
    txn.data_len = 0;

    if (apex_nfc_transact(handle, &txn) != APEX_OK)
        return handle->last_error;

    /* Check if the response indicates a tag was found.
     * The driver returns data_len > 0 if a tag ATQA was received. */
    if (txn.data_len > 0)
        return APEX_OK;

    return APEX_ERR_TIMEOUT;
}

/* ========================================================================
 * Firmware Version
 * ======================================================================== */

int apex_get_firmware_version(apex_handle_t handle, char *version,
                               size_t buf_len) {
    struct kernel_telemetry ktelem;

    if (!handle || handle->fd < 0 || !version || buf_len == 0)
        return APEX_ERR_INVALID_ARG;

    /* The firmware version is embedded in the driver's status response.
     * For now, we query the driver status to get the version string.
     * A dedicated ioctl can be added later. */
    if (ioctl(handle->fd, IOC_GET_STATUS, &(uint32_t){0}) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    /* Format a version string from the telemetry uptime as a placeholder.
     * The real implementation would use a dedicated version ioctl. */
    if (buf_len < 16) {
        version[0] = '\0';
        return APEX_ERR_INVALID_ARG;
    }

    /* Read telemetry to confirm MCU is communicating */
    if (ioctl(handle->fd, IOC_GET_TELEMETRY, &ktelem) < 0) {
        handle->last_error = APEX_ERR_IOCTL_FAILED;
        return APEX_ERR_IOCTL_FAILED;
    }

    /* Return a formatted version string.
     * The kernel driver reports the MCU firmware version via
     * the firmware_version sysfs attribute. Here we format
     * the version from the driver flags field. */
    snprintf(version, buf_len, "GhostBlade v%u.%u.%u",
             (unsigned)((ktelem.flags >> 8) & 0xFF),
             (unsigned)((ktelem.flags >> 4) & 0x0F),
             (unsigned)(ktelem.flags & 0x0F));

    handle->last_error = APEX_OK;
    return APEX_OK;
}

/* ========================================================================
 * Convenience Functions
 * ======================================================================== */

uint8_t apex_battery_percent(uint16_t vbat_mv) {
    if (vbat_mv >= 4200) return 100;
    if (vbat_mv <= 3000) return 0;

    if (vbat_mv >= 3700)
        return (uint8_t)((uint32_t)(vbat_mv - 3700) * 50 / 500 + 50);
    else if (vbat_mv >= 3300)
        return (uint8_t)((uint32_t)(vbat_mv - 3300) * 40 / 400 + 10);
    else
        return (uint8_t)((uint32_t)(vbat_mv - 3000) * 10 / 300);
}

bool apex_is_low_battery(const apex_telemetry_t *telem) {
    if (!telem) return false;
    return telem->vbat_mv < 3300;
}

bool apex_is_overtemp(const apex_telemetry_t *telem) {
    if (!telem) return false;
    return telem->temp_c_x10 > 850;  /* 85.0°C */
}