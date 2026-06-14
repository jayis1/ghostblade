/*
 * libapex.h — Userspace API for Apex One SPI Bridge Device
 *
 * Copyright (C) 2026 Apex One Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This library provides a C API for interacting with the Apex One
 * pentesting device through the kernel SPI bridge driver. It wraps
 * the ioctl interface and provides higher-level functions for SDR
 * control, sub-GHz radio, NFC, and telemetry.
 *
 * Usage:
 *   #include <libapex.h>
 *   apex_handle_t handle = apex_open();
 *   apex_sdr_tune(handle, 868e6, 20000, 30);
 *   ...
 *   apex_close(handle);
 */

#ifndef LIBAPEX_H
#define LIBAPEX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Library Version
 * ======================================================================== */

#define LIBAPEX_VERSION_MAJOR   0
#define LIBAPEX_VERSION_MINOR   1
#define LIBAPEX_VERSION_PATCH   0
#define LIBAPEX_VERSION_STRING  "0.1.0"

/* ========================================================================
 * Error Codes
 * ======================================================================== */

#define APEX_OK                  0
#define APEX_ERR_INVALID_ARG   -1
#define APEX_ERR_NO_DEVICE     -2
#define APEX_ERR_OPEN_FAILED   -3
#define APEX_ERR_IOCTL_FAILED  -4
#define APEX_ERR_TIMEOUT       -5
#define APEX_ERR_COMM          -6
#define APEX_ERR_NOT_READY     -7
#define APEX_ERR_NOMEM         -8

/* ========================================================================
 * Types
 * ======================================================================== */

/* Opaque handle returned by apex_open() */
typedef struct apex_device *apex_handle_t;

/* Antenna selection */
typedef enum {
    APEX_ANT_MIMO_TX    = 0,   /* PE42422 RF1 → SMA_ANT0 */
    APEX_ANT_MIMO_RX    = 1,   /* PE42422 RF2 → SMA_ANT1 */
    APEX_ANT_SUBGHZ     = 2,   /* PE42422 RF3 → CC1101/u.FL */
    APEX_ANT_TERMINATED = 3,   /* PE42422 RF4 → 50Ω load */
} apex_antenna_t;

/* SDR stream direction */
typedef enum {
    APEX_STREAM_STOP  = 0,
    APEX_STREAM_START = 1,
} apex_stream_state_t;

/* Telemetry data structure (mirrors kernel struct apex_telemetry) */
typedef struct {
    int16_t  rssi_dbm_x10;      /* SDR RSSI in dBm × 10 */
    int16_t  temp_c_x10;        /* MCU die temperature in °C × 10 */
    uint16_t vbat_mv;           /* Battery voltage in mV */
    int16_t  cc1101_rssi_x10;   /* CC1101 sub-GHz RSSI in dBm × 10 */
    uint16_t nfc_field_mv;      /* NFC field strength in mV */
    uint16_t flags;             /* Status flags bitmap */
    uint32_t uptime_ms;         /* MCU uptime in ms */
} apex_telemetry_t;

/* Telemetry flag bits */
#define APEX_TELEM_SDR_RX_ACTIVE    (1 << 0)
#define APEX_TELEM_SDR_TX_ACTIVE    (1 << 1)
#define APEX_TELEM_CC1101_RX        (1 << 2)
#define APEX_TELEM_CC1101_TX        (1 << 3)
#define APEX_TELEM_NFC_ACTIVE       (1 << 4)
#define APEX_TELEM_NFC_TAG_PRESENT  (1 << 5)
#define APEX_TELEM_OVERTEMP         (1 << 6)
#define APEX_TELEM_LOW_BATTERY      (1 << 7)

/* SDR tune parameters */
typedef struct {
    uint32_t freq_hz;           /* Center frequency in Hz (100 kHz – 3.8 GHz) */
    uint16_t bw_khz;            /* Bandwidth in kHz (e.g., 20000 = 20 MHz) */
    uint16_t gain_db_x10;       /* LNA gain in dB × 10 (e.g., 300 = 30.0 dB) */
} apex_sdr_tune_t;

/* CC1101 register configuration */
typedef struct {
    uint8_t  reg_addr;          /* Starting register address */
    uint8_t  reg_len;           /* Number of consecutive registers */
    uint8_t  data[64];          /* Register data (max 64 consecutive regs) */
} apex_cc1101_config_t;

/* NFC transaction parameters */
typedef struct {
    uint8_t  cmd;               /* NFC command (ISO 14443 A/B type) */
    uint8_t  flags;             /* Transaction flags */
    uint16_t data_len;          /* TX data length */
    uint8_t  data[256];         /* TX/RX data buffer */
} apex_nfc_transact_t;

/* Device status */
typedef struct {
    uint32_t driver_flags;      /* Driver status flags */
    bool     mcu_ready;         /* MCU has reported ready */
    bool     mcu_in_reset;      /* MCU is held in reset */
    bool     spi_error;         /* SPI communication error detected */
} apex_status_t;

/* ========================================================================
 * Device Management
 * ======================================================================== */

/**
 * apex_open — Open a connection to the Apex One device
 *
 * @device_path: Path to device node (e.g., "/dev/apex_bridge0").
 *               If NULL, uses the default "/dev/apex_bridge0".
 *
 * Returns: Handle on success, NULL on failure.
 *          Use apex_last_error() to get error details.
 */
apex_handle_t apex_open(const char *device_path);

/**
 * apex_close — Close the device connection and free resources
 *
 * @handle: Device handle (may be NULL, no-op)
 */
void apex_close(apex_handle_t handle);

/**
 * apex_get_fd — Get the underlying file descriptor for poll/select
 *
 * @handle: Device handle
 * Returns: File descriptor, or -1 on error
 */
int apex_get_fd(apex_handle_t handle);

/**
 * apex_last_error — Get the last error code from an operation
 *
 * @handle: Device handle
 * Returns: APEX error code (negative), or APEX_OK
 */
int apex_last_error(apex_handle_t handle);

/**
 * apex_strerror — Get a human-readable error string
 *
 * @error: APEX error code
 * Returns: Static string describing the error
 */
const char *apex_strerror(int error);

/* ========================================================================
 * SDR Control
 * ======================================================================== */

/**
 * apex_sdr_tune — Tune the LMS7002M SDR to a frequency and bandwidth
 *
 * @handle:   Device handle
 * @freq_hz:  Center frequency in Hz (100 kHz – 3.8 GHz)
 * @bw_khz:   Bandwidth in kHz (e.g., 20000 for 20 MHz)
 * @gain_db:  LNA gain in dB (0.0 – 70.0, passed as ×10 internally)
 *
 * Returns: APEX_OK on success, negative error code on failure
 */
int apex_sdr_tune(apex_handle_t handle, uint32_t freq_hz,
                  uint16_t bw_khz, float gain_db);

/**
 * apex_sdr_stream_start — Start SDR IQ data streaming
 *
 * @handle: Device handle
 *
 * Returns: APEX_OK on success
 */
int apex_sdr_stream_start(apex_handle_t handle);

/**
 * apex_sdr_stream_stop — Stop SDR IQ data streaming
 *
 * @handle: Device handle
 *
 * Returns: APEX_OK on success
 */
int apex_sdr_stream_stop(apex_handle_t handle);

/* ========================================================================
 * Antenna Control
 * ======================================================================== */

/**
 * apex_ant_select — Select the active antenna path
 *
 * @handle: Device handle
 * @ant:    Antenna selection (APEX_ANT_MIMO_TX, _RX, _SUBGHZ, _TERMINATED)
 *
 * Returns: APEX_OK on success
 */
int apex_ant_select(apex_handle_t handle, apex_antenna_t ant);

/* ========================================================================
 * CC1101 Sub-GHz Radio
 * ======================================================================== */

/**
 * apex_cc1101_write_regs — Write consecutive CC1101 registers
 *
 * @handle:   Device handle
 * @cfg:      Register configuration (address, length, data)
 *
 * Returns: APEX_OK on success
 */
int apex_cc1101_write_regs(apex_handle_t handle,
                            const apex_cc1101_config_t *cfg);

/**
 * apex_cc1101_set_channel — Set the CC1101 channel number
 *
 * @handle:  Device handle
 * @channel: Channel number (0-255)
 *
 * This writes the CHANNR register (0x0A) on the CC1101.
 * Channel spacing is defined by MDMCFG1/MDMCFG0.
 *
 * Returns: APEX_OK on success
 */
int apex_cc1101_set_channel(apex_handle_t handle, uint8_t channel);

/**
 * apex_cc1101_set_power — Set the CC1101 TX output power
 *
 * @handle:    Device handle
 * @power_dbm: TX power in dBm (-30 to +10)
 *
 * This writes the FREND0.PA_POWER setting.
 *
 * Returns: APEX_OK on success
 */
int apex_cc1101_set_power(apex_handle_t handle, int8_t power_dbm);

/* ========================================================================
 * NFC Controller
 * ======================================================================== */

/**
 * apex_nfc_transact — Perform an NFC transaction
 *
 * @handle: Device handle
 * @txn:    Transaction parameters (command, flags, data)
 *
 * Returns: APEX_OK on success
 */
int apex_nfc_transact(apex_handle_t handle, const apex_nfc_transact_t *txn);

/**
 * apex_nfc_field_on — Turn on the NFC 13.56 MHz carrier field
 *
 * @handle: Device handle
 *
 * Returns: APEX_OK on success
 */
int apex_nfc_field_on(apex_handle_t handle);

/**
 * apex_nfc_field_off — Turn off the NFC carrier field
 *
 * @handle: Device handle
 *
 * Returns: APEX_OK on success
 */
int apex_nfc_field_off(apex_handle_t handle);

/* ========================================================================
 * Telemetry & Status
 * ======================================================================== */

/**
 * apex_get_telemetry — Read current telemetry data from the MCU
 *
 * @handle: Device handle
 * @telem:  Output: telemetry data structure
 *
 * Returns: APEX_OK on success
 */
int apex_get_telemetry(apex_handle_t handle, apex_telemetry_t *telem);

/**
 * apex_get_status — Get the driver and device status
 *
 * @handle: Device handle
 * @status: Output: device status structure
 *
 * Returns: APEX_OK on success
 */
int apex_get_status(apex_handle_t handle, apex_status_t *status);

/**
 * apex_mcu_reset — Assert or deassert the MCU reset line
 *
 * @handle:  Device handle
 * @assert:  true = assert reset (hold MCU in reset),
 *           false = deassert reset (release MCU)
 *
 * Returns: APEX_OK on success
 */
int apex_mcu_reset(apex_handle_t handle, bool assert);

/* ========================================================================
 * Convenience Functions
 * ======================================================================== */

/**
 * apex_battery_percent — Estimate battery charge percentage
 *
 * @vbat_mv: Battery voltage in mV (from apex_get_telemetry)
 * Returns: Estimated charge percentage (0-100)
 */
uint8_t apex_battery_percent(uint16_t vbat_mv);

/**
 * apex_is_low_battery — Check if battery voltage is below low threshold
 *
 * @telem: Pointer to telemetry data
 * Returns: true if battery is below 3.3V
 */
bool apex_is_low_battery(const apex_telemetry_t *telem);

/**
 * apex_is_overtemp — Check if MCU die temperature exceeds safe limit
 *
 * @telem: Pointer to telemetry data
 * Returns: true if temperature exceeds 85.0°C
 */
bool apex_is_overtemp(const apex_telemetry_t *telem);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPEX_H */