/*
 * pyapex.c — Python Bindings for GhostBlade libapex
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Python extension module providing access to the GhostBlade hardware
 * through the libapex C library. This module wraps the ioctl interface
 * and provides a Pythonic API for SDR control, sub-GHz radio, NFC,
 * and telemetry.
 *
 * Usage:
 *   import pyapex
 *   dev = pyapex.ApexBridge('/dev/apex_bridge0')
 *   dev.sdr_tune(868e6, 20000, 30.0)
 *   telem = dev.get_telemetry()
 *   print(f"VBAT: {telem['vbat_mv']}mV, Temp: {telem['temp_c_x10']/10.0}°C")
 *   dev.close()
 *
 * Build:
 *   pip install .  (or python3 setup.py build_ext --inplace)
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "libapex.h"

/* ========================================================================
 * Kernel ioctl definitions (must match apex_bridge_regs.h)
 * ======================================================================== */

#include <linux/ioctl.h>

#define APEX_IOC_MAGIC         'A'

/* ioctl command structures (must match kernel) */
struct apex_sdr_tune_cmd {
    uint32_t freq_hz;
    uint16_t bw_khz;
    uint16_t gain_db_x10;
} __attribute__((packed));

struct apex_cc1101_cfg {
    uint8_t  reg_addr;
    uint8_t  reg_len;
    uint8_t  data[64];
} __attribute__((packed));

struct apex_nfc_transact {
    uint8_t  cmd;
    uint8_t  flags;
    uint16_t data_len;
    uint8_t  data[256];
} __attribute__((packed));

struct apex_telemetry {
    uint16_t rssi_dbm_x10;
    uint16_t temp_c_x10;
    uint16_t vbat_mv;
    uint16_t cc1101_rssi_x10;
    uint16_t nfc_field_mv;
    uint16_t flags;
    uint32_t uptime_ms;
} __attribute__((packed));

struct apex_sg_config {
    uint32_t buf_count;
    uint32_t buf_size;
    uint32_t timeout_ms;
    uint32_t spi_speed_hz;
    uint8_t  continuous;
    uint8_t  reserved[3];
} __attribute__((packed));

struct apex_sg_status {
    uint32_t state;
    uint32_t buf_count;
    uint32_t buf_size;
    uint64_t total_transferred;
    uint32_t overruns;
    uint32_t errors;
    uint32_t frames_rx;
    uint32_t frames_crc_err;
} __attribute__((packed));

#define APEX_IOC_SDR_TUNE      _IOW(APEX_IOC_MAGIC, 1, struct apex_sdr_tune_cmd)
#define APEX_IOC_SDR_STREAM    _IOW(APEX_IOC_MAGIC, 2, uint8_t)
#define APEX_IOC_ANT_SELECT    _IOW(APEX_IOC_MAGIC, 3, uint8_t)
#define APEX_IOC_CC1101_CFG    _IOW(APEX_IOC_MAGIC, 4, struct apex_cc1101_cfg)
#define APEX_IOC_NFC_TRANSACT  _IOW(APEX_IOC_MAGIC, 5, struct apex_nfc_transact)
#define APEX_IOC_GET_TELEMETRY _IOR(APEX_IOC_MAGIC, 6, struct apex_telemetry)
#define APEX_IOC_MCU_RESET     _IOW(APEX_IOC_MAGIC, 7, uint8_t)
#define APEX_IOC_GET_STATUS    _IOR(APEX_IOC_MAGIC, 8, uint32_t)
#define APEX_IOC_SG_START      _IOW(APEX_IOC_MAGIC, 9, struct apex_sg_config)
#define APEX_IOC_SG_STOP       _IO(APEX_IOC_MAGIC, 10)
#define APEX_IOC_SG_GET_STATUS _IOR(APEX_IOC_MAGIC, 11, struct apex_sg_status)
#define APEX_IOC_SOFT_RESET    _IOW(APEX_IOC_MAGIC, 12, uint32_t)

#define APEX_RESET_MAGIC       0x52534554UL

/* ========================================================================
 * ApexBridge Python Object
 * ======================================================================== */

typedef struct {
    PyObject_HEAD
    int fd;             /* File descriptor for /dev/apex_bridge0 */
    char *device_path;  /* Device path string */
} ApexBridgeObject;

/* Forward declarations */
static PyTypeObject ApexBridgeType;

/* ========================================================================
 * ApexBridge.__init__
 * ======================================================================== */

static int ApexBridge_init(ApexBridgeObject *self, PyObject *args, PyObject *kwds) {
    const char *device_path = "/dev/apex_bridge0";
    static char *kwlist[] = {"device", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &device_path))
        return -1;

    self->device_path = strdup(device_path);
    if (!self->device_path) {
        PyErr_NoMemory();
        return -1;
    }

    self->fd = open(device_path, O_RDWR);
    if (self->fd < 0) {
        PyErr_Format(PyExc_OSError,
                     "Failed to open %s: %s", device_path, strerror(errno));
        free(self->device_path);
        self->device_path = NULL;
        return -1;
    }

    return 0;
}

/* ========================================================================
 * ApexBridge.__del__
 * ======================================================================== */

static void ApexBridge_dealloc(ApexBridgeObject *self) {
    if (self->fd >= 0) {
        close(self->fd);
        self->fd = -1;
    }
    free(self->device_path);
    self->device_path = NULL;
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* ========================================================================
 * ApexBridge.sdr_tune(freq_hz, bw_khz, gain_db)
 * ======================================================================== */

static PyObject *ApexBridge_sdr_tune(ApexBridgeObject *self, PyObject *args) {
    double freq_hz, gain_db;
    int bw_khz;

    if (!PyArg_ParseTuple(args, "did", &freq_hz, &bw_khz, &gain_db))
        return NULL;

    if (freq_hz < 100e3 || freq_hz > 3.8e9) {
        PyErr_SetString(PyExc_ValueError,
                        "freq_hz must be between 100 kHz and 3.8 GHz");
        return NULL;
    }

    struct apex_sdr_tune_cmd cmd = {
        .freq_hz = (uint32_t)freq_hz,
        .bw_khz = (uint16_t)bw_khz,
        .gain_db_x10 = (uint16_t)(gain_db * 10),
    };

    if (ioctl(self->fd, APEX_IOC_SDR_TUNE, &cmd) < 0) {
        PyErr_Format(PyExc_OSError, "SDR tune failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.sdr_stream_start()
 * ======================================================================== */

static PyObject *ApexBridge_sdr_stream_start(ApexBridgeObject *self,
                                                PyObject *Py_UNUSED(ignored)) {
    uint8_t enable = 1;
    if (ioctl(self->fd, APEX_IOC_SDR_STREAM, &enable) < 0) {
        PyErr_Format(PyExc_OSError, "SDR stream start failed: %s", strerror(errno));
        return NULL;
    }
    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.sdr_stream_stop()
 * ======================================================================== */

static PyObject *ApexBridge_sdr_stream_stop(ApexBridgeObject *self,
                                               PyObject *Py_UNUSED(ignored)) {
    uint8_t enable = 0;
    if (ioctl(self->fd, APEX_IOC_SDR_STREAM, &enable) < 0) {
        PyErr_Format(PyExc_OSError, "SDR stream stop failed: %s", strerror(errno));
        return NULL;
    }
    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.ant_select(antenna)
 * ======================================================================== */

static PyObject *ApexBridge_ant_select(ApexBridgeObject *self, PyObject *args) {
    uint8_t ant;
    if (!PyArg_ParseTuple(args, "B", &ant))
        return NULL;

    if (ant > 3) {
        PyErr_SetString(PyExc_ValueError,
                        "antenna must be 0=MIMO_TX, 1=MIMO_RX, 2=SUBGHZ, 3=TERMINATED");
        return NULL;
    }

    if (ioctl(self->fd, APEX_IOC_ANT_SELECT, &ant) < 0) {
        PyErr_Format(PyExc_OSError, "Antenna select failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.get_telemetry()
 * ======================================================================== */

static PyObject *ApexBridge_get_telemetry(ApexBridgeObject *self,
                                            PyObject *Py_UNUSED(ignored)) {
    struct apex_telemetry telem;

    if (ioctl(self->fd, APEX_IOC_GET_TELEMETRY, &telem) < 0) {
        PyErr_Format(PyExc_OSError, "Get telemetry failed: %s", strerror(errno));
        return NULL;
    }

    return Py_BuildValue("{s:i,s:i,s:I,s:i,s:I,s:I,s:I}",
        "rssi_dbm_x10",    (int)(int16_t)telem.rssi_dbm_x10,
        "temp_c_x10",      (int)(int16_t)telem.temp_c_x10,
        "vbat_mv",         (unsigned int)telem.vbat_mv,
        "cc1101_rssi_x10", (int)(int16_t)telem.cc1101_rssi_x10,
        "nfc_field_mv",    (unsigned int)telem.nfc_field_mv,
        "flags",           (unsigned int)telem.flags,
        "uptime_ms",       (unsigned int)telem.uptime_ms
    );
}

/* ========================================================================
 * ApexBridge.cc1101_write(reg_addr, data_bytes)
 * ======================================================================== */

static PyObject *ApexBridge_cc1101_write(ApexBridgeObject *self, PyObject *args) {
    uint8_t reg_addr;
    Py_buffer data_buf;

    if (!PyArg_ParseTuple(args, "By*", &reg_addr, &data_buf))
        return NULL;

    if (data_buf.len > 64) {
        PyBuffer_Release(&data_buf);
        PyErr_SetString(PyExc_ValueError, "Data length must be <= 64 bytes");
        return NULL;
    }

    struct apex_cc1101_cfg cfg = {
        .reg_addr = reg_addr,
        .reg_len = (uint8_t)data_buf.len,
    };
    memcpy(cfg.data, data_buf.buf, data_buf.len);
    PyBuffer_Release(&data_buf);

    if (ioctl(self->fd, APEX_IOC_CC1101_CFG, &cfg) < 0) {
        PyErr_Format(PyExc_OSError, "CC1101 config failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.nfc_transact(cmd, flags, data)
 * ======================================================================== */

static PyObject *ApexBridge_nfc_transact(ApexBridgeObject *self, PyObject *args) {
    uint8_t cmd, flags;
    Py_buffer tx_buf;
    uint16_t rx_len = 256;

    if (!PyArg_ParseTuple(args, "BBy*", &cmd, &flags, &tx_buf))
        return NULL;

    if (tx_buf.len > 256) {
        PyBuffer_Release(&tx_buf);
        PyErr_SetString(PyExc_ValueError, "Data length must be <= 256 bytes");
        return NULL;
    }

    struct apex_nfc_transact txn = {
        .cmd = cmd,
        .flags = flags,
        .data_len = (uint16_t)tx_buf.len,
    };
    memcpy(txn.data, tx_buf.buf, tx_buf.len);
    PyBuffer_Release(&tx_buf);

    if (ioctl(self->fd, APEX_IOC_NFC_TRANSACT, &txn) < 0) {
        PyErr_Format(PyExc_OSError, "NFC transaction failed: %s", strerror(errno));
        return NULL;
    }

    /* Return response data */
    uint16_t response_len = txn.data_len;
    if (response_len > 256)
        response_len = 256;

    return Py_BuildValue("y#", txn.data, (Py_ssize_t)response_len);
}

/* ========================================================================
 * ApexBridge.mcu_reset(assert)
 * ======================================================================== */

static PyObject *ApexBridge_mcu_reset(ApexBridgeObject *self, PyObject *args) {
    int assert_reset;
    if (!PyArg_ParseTuple(args, "p", &assert_reset))
        return NULL;

    uint8_t val = assert_reset ? 1 : 0;
    if (ioctl(self->fd, APEX_IOC_MCU_RESET, &val) < 0) {
        PyErr_Format(PyExc_OSError, "MCU reset failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.soft_reset()
 * ======================================================================== */

static PyObject *ApexBridge_soft_reset(ApexBridgeObject *self,
                                         PyObject *Py_UNUSED(ignored)) {
    uint32_t magic = APEX_RESET_MAGIC;
    if (ioctl(self->fd, APEX_IOC_SOFT_RESET, &magic) < 0) {
        PyErr_Format(PyExc_OSError, "Soft reset failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.get_status()
 * ======================================================================== */

static PyObject *ApexBridge_get_status(ApexBridgeObject *self,
                                          PyObject *Py_UNUSED(ignored)) {
    uint32_t status = 0;

    if (ioctl(self->fd, APEX_IOC_GET_STATUS, &status) < 0) {
        PyErr_Format(PyExc_OSError, "Get status failed: %s", strerror(errno));
        return NULL;
    }

    return Py_BuildValue("{s:I,s:b,s:b,s:b}",
        "raw_flags",    status,
        "mcu_ready",    (status & 0x01) ? 1 : 0,
        "mcu_reset",    (status & 0x02) ? 1 : 0,
        "spi_error",    (status & 0x04) ? 1 : 0
    );
}

/* ========================================================================
 * ApexBridge.sg_start(buf_count, buf_size, timeout_ms, continuous)
 * ======================================================================== */

static PyObject *ApexBridge_sg_start(ApexBridgeObject *self, PyObject *args) {
    uint32_t buf_count, buf_size, timeout_ms;
    int continuous;

    if (!PyArg_ParseTuple(args, "IIIp", &buf_count, &buf_size, &timeout_ms, &continuous))
        return NULL;

    if (buf_count < 2 || buf_count > 64) {
        PyErr_SetString(PyExc_ValueError, "buf_count must be 2-64");
        return NULL;
    }
    if (buf_size < 4096 || buf_size > 262144 || (buf_size % 4) != 0) {
        PyErr_SetString(PyExc_ValueError, "buf_size must be 4096-262144 and 4-byte aligned");
        return NULL;
    }

    struct apex_sg_config config = {
        .buf_count = buf_count,
        .buf_size = buf_size,
        .timeout_ms = timeout_ms,
        .spi_speed_hz = 0,  /* Use default */
        .continuous = continuous ? 1 : 0,
        .reserved = {0},
    };

    if (ioctl(self->fd, APEX_IOC_SG_START, &config) < 0) {
        PyErr_Format(PyExc_OSError, "SG start failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.sg_stop()
 * ======================================================================== */

static PyObject *ApexBridge_sg_stop(ApexBridgeObject *self,
                                      PyObject *Py_UNUSED(ignored)) {
    if (ioctl(self->fd, APEX_IOC_SG_STOP) < 0) {
        PyErr_Format(PyExc_OSError, "SG stop failed: %s", strerror(errno));
        return NULL;
    }

    Py_RETURN_NONE;
}

/* ========================================================================
 * ApexBridge.sg_get_status()
 * ======================================================================== */

static PyObject *ApexBridge_sg_get_status(ApexBridgeObject *self,
                                             PyObject *Py_UNUSED(ignored)) {
    struct apex_sg_status status;

    if (ioctl(self->fd, APEX_IOC_SG_GET_STATUS, &status) < 0) {
        PyErr_Format(PyExc_OSError, "SG get status failed: %s", strerror(errno));
        return NULL;
    }

    return Py_BuildValue("{s:I,s:I,s:I,s:K,s:I,s:I,s:I,s:I}",
        "state",             status.state,
        "buf_count",         status.buf_count,
        "buf_size",          status.buf_size,
        "total_transferred", (unsigned long long)status.total_transferred,
        "overruns",          status.overruns,
        "errors",            status.errors,
        "frames_rx",         status.frames_rx,
        "frames_crc_err",    status.frames_crc_err
    );
}

/* ========================================================================
 * ApexBridge.fileno() — for poll/select compatibility
 * ======================================================================== */

static PyObject *ApexBridge_fileno(ApexBridgeObject *self,
                                     PyObject *Py_UNUSED(ignored)) {
    return PyLong_FromLong(self->fd);
}

/* ========================================================================
 * ApexBridge.read_iq(size) — Read raw IQ data
 * ======================================================================== */

static PyObject *ApexBridge_read_iq(ApexBridgeObject *self, PyObject *args) {
    Py_ssize_t size = 4096;

    if (!PyArg_ParseTuple(args, "|n", &size))
        return NULL;

    if (size <= 0 || size > 1048576) {
        PyErr_SetString(PyExc_ValueError, "size must be 1-1048576 bytes");
        return NULL;
    }

    PyObject *buf = PyBytes_FromStringAndSize(NULL, size);
    if (!buf)
        return NULL;

    char *data = PyBytes_AS_STRING(buf);
    Py_ssize_t n_read = read(self->fd, data, size);

    if (n_read < 0) {
        Py_DECREF(buf);
        PyErr_Format(PyExc_OSError, "Read failed: %s", strerror(errno));
        return NULL;
    }

    /* Resize the bytes object to the actual amount read */
    if (n_read < size) {
        _PyBytes_Resize(&buf, n_read);
    }

    return buf;
}

/* ========================================================================
 * Method Table
 * ======================================================================== */

static PyMethodDef ApexBridge_methods[] = {
    {"sdr_tune",        (PyCFunction)ApexBridge_sdr_tune,        METH_VARARGS,
     "Tune SDR to a frequency.\n\n"
     "Args:\n"
     "    freq_hz (float): Center frequency in Hz (100 kHz - 3.8 GHz)\n"
     "    bw_khz (int): Bandwidth in kHz\n"
     "    gain_db (float): LNA gain in dB\n"},

    {"sdr_stream_start",(PyCFunction)ApexBridge_sdr_stream_start, METH_NOARGS,
     "Start SDR IQ data streaming.\n"},

    {"sdr_stream_stop", (PyCFunction)ApexBridge_sdr_stream_stop,  METH_NOARGS,
     "Stop SDR IQ data streaming.\n"},

    {"ant_select",      (PyCFunction)ApexBridge_ant_select,      METH_VARARGS,
     "Select antenna path.\n\n"
     "Args:\n"
     "    antenna (int): 0=MIMO_TX, 1=MIMO_RX, 2=SUBGHZ, 3=TERMINATED\n"},

    {"get_telemetry",   (PyCFunction)ApexBridge_get_telemetry,    METH_NOARGS,
     "Read telemetry data from MCU.\n\n"
     "Returns: dict with keys: rssi_dbm_x10, temp_c_x10, vbat_mv,\n"
     "         cc1101_rssi_x10, nfc_field_mv, flags, uptime_ms\n"},

    {"cc1101_write",    (PyCFunction)ApexBridge_cc1101_write,     METH_VARARGS,
     "Write consecutive CC1101 registers.\n\n"
     "Args:\n"
     "    reg_addr (int): Starting register address\n"
     "    data (bytes): Register values to write\n"},

    {"nfc_transact",    (PyCFunction)ApexBridge_nfc_transact,     METH_VARARGS,
     "Perform an NFC transaction.\n\n"
     "Args:\n"
     "    cmd (int): NFC command opcode\n"
     "    flags (int): Transaction flags\n"
     "    data (bytes): TX data\n\n"
     "Returns: bytes - RX data from NFC tag\n"},

    {"mcu_reset",       (PyCFunction)ApexBridge_mcu_reset,        METH_VARARGS,
     "Assert or deassert MCU reset line.\n\n"
     "Args:\n"
     "    assert_reset (bool): True to hold MCU in reset, False to release\n"},

    {"soft_reset",      (PyCFunction)ApexBridge_soft_reset,       METH_NOARGS,
     "Trigger a soft reset of the MCU coprocessor.\n"},

    {"get_status",      (PyCFunction)ApexBridge_get_status,       METH_NOARGS,
     "Get driver and device status.\n\n"
     "Returns: dict with keys: raw_flags, mcu_ready, mcu_reset, spi_error\n"},

    {"sg_start",        (PyCFunction)ApexBridge_sg_start,          METH_VARARGS,
     "Start DMA scatter-gather streaming.\n\n"
     "Args:\n"
     "    buf_count (int): Number of buffers (2-64)\n"
     "    buf_size (int): Size of each buffer (4096-262144, 4-byte aligned)\n"
     "    timeout_ms (int): DMA timeout in ms\n"
     "    continuous (bool): True for ring-buffer mode\n"},

    {"sg_stop",         (PyCFunction)ApexBridge_sg_stop,           METH_NOARGS,
     "Stop DMA scatter-gather streaming.\n"},

    {"sg_get_status",   (PyCFunction)ApexBridge_sg_get_status,    METH_NOARGS,
     "Get SG engine status.\n\n"
     "Returns: dict with keys: state, buf_count, buf_size, total_transferred,\n"
     "         overruns, errors, frames_rx, frames_crc_err\n"},

    {"fileno",          (PyCFunction)ApexBridge_fileno,           METH_NOARGS,
     "Return the file descriptor for poll/select.\n"},

    {"read_iq",         (PyCFunction)ApexBridge_read_iq,          METH_VARARGS,
     "Read raw IQ data from the SDR stream.\n\n"
     "Args:\n"
     "    size (int): Maximum bytes to read (default 4096)\n\n"
     "Returns: bytes - raw IQ data (I16Q16 format)\n"},

    {NULL}  /* Sentinel */
};

/* ========================================================================
 * Type Definition
 * ======================================================================== */

static PyTypeObject ApexBridgeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pyapex.ApexBridge",
    .tp_doc = "GhostBlade SPI bridge device interface.\n\n"
              "Provides SDR control, sub-GHz radio, NFC, and telemetry access.\n\n"
              "Usage:\n"
              "    dev = ApexBridge('/dev/apex_bridge0')\n"
              "    dev.sdr_tune(868e6, 20000, 30.0)\n"
              "    telem = dev.get_telemetry()\n"
              "    dev.close()\n",
    .tp_basicsize = sizeof(ApexBridgeObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)ApexBridge_init,
    .tp_dealloc = (destructor)ApexBridge_dealloc,
    .tp_methods = ApexBridge_methods,
};

/* ========================================================================
 * Module Definition
 * ======================================================================== */

static PyModuleDef pyapex_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pyapex",
    .m_doc = "Python bindings for GhostBlade hardware (libapex).\n\n"
             "Provides the ApexBridge class for controlling the GhostBlade\n"
             "dual-processor pentesting device via the SPI bridge driver.\n",
    .m_size = -1,
};

/* ========================================================================
 * Module Init
 * ======================================================================== */

PyMODINIT_FUNC PyInit_pyapex(void) {
    PyObject *m;

    if (PyType_Ready(&ApexBridgeType) < 0)
        return NULL;

    m = PyModule_Create(&pyapex_module);
    if (m == NULL)
        return NULL;

    Py_INCREF(&ApexBridgeType);
    if (PyModule_AddObject(m, "ApexBridge", (PyObject *)&ApexBridgeType) < 0) {
        Py_DECREF(&ApexBridgeType);
        Py_DECREF(m);
        return NULL;
    }

    /* Add module-level constants */
    PyModule_AddIntMacro(m, APEX_ANT_MIMO_TX);
    PyModule_AddIntMacro(m, APEX_ANT_MIMO_RX);
    PyModule_AddIntMacro(m, APEX_ANT_SUBGHZ);
    PyModule_AddIntMacro(m, APEX_ANT_TERMINATED);
    PyModule_AddIntMacro(m, APEX_RESET_MAGIC);

    /* Telemetry flag bits */
    PyModule_AddIntConstant(m, "TELEM_SDR_RX_ACTIVE",  (1 << 0));
    PyModule_AddIntConstant(m, "TELEM_SDR_TX_ACTIVE",   (1 << 1));
    PyModule_AddIntConstant(m, "TELEM_CC1101_RX",       (1 << 2));
    PyModule_AddIntConstant(m, "TELEM_CC1101_TX",       (1 << 3));
    PyModule_AddIntConstant(m, "TELEM_NFC_ACTIVE",      (1 << 4));
    PyModule_AddIntConstant(m, "TELEM_NFC_TAG_PRESENT", (1 << 5));
    PyModule_AddIntConstant(m, "TELEM_OVERTEMP",         (1 << 6));
    PyModule_AddIntConstant(m, "TELEM_LOW_BATTERY",      (1 << 7));

    /* NFC command opcodes */
    PyModule_AddIntConstant(m, "NFC_CMD_REQA",    0x26);
    PyModule_AddIntConstant(m, "NFC_CMD_WUPA",    0x52);
    PyModule_AddIntConstant(m, "NFC_CMD_ANTICOL", 0x93);
    PyModule_AddIntConstant(m, "NFC_CMD_SELECT",  0x93);
    PyModule_AddIntConstant(m, "NFC_CMD_HALT",    0x50);
    PyModule_AddIntConstant(m, "NFC_CMD_RATS",    0xE0);
    PyModule_AddIntConstant(m, "NFC_CMD_REQB",    0x05);
    PyModule_AddIntConstant(m, "NFC_CMD_ATTRIB",  0x1D);

    /* SG engine states */
    PyModule_AddIntConstant(m, "SG_STATE_IDLE",    0);
    PyModule_AddIntConstant(m, "SG_STATE_RUNNING", 1);
    PyModule_AddIntConstant(m, "SG_STATE_ERROR",   2);

    return m;
}