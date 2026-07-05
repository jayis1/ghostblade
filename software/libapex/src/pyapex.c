/*
 * pyapex — Python Bindings for libapex
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Python C extension module providing access to the GhostBlade hardware
 * through the libapex C library. This module wraps all libapex functions
 * into Python classes and methods.
 *
 * Usage:
 *   import pyapex
 *   dev = pyapex.ApexDevice()
 *   dev.sdr_tune(868e6, 20000, 30.0)
 *   telem = dev.get_telemetry()
 *   print(f"Battery: {telem.vbat_mv} mV")
 *   dev.close()
 *
 * Build:
 *   python3 setup.py build_ext --inplace
 */

#include <Python.h>
#include "libapex.h"

/* ========================================================================
 * ApexDevice Python Type
 * ======================================================================== */

typedef struct {
    PyObject_HEAD
    apex_handle_t handle;
} PyApexDeviceObject;

/* ========================================================================
 * ApexTelemetry Python Type
 * ======================================================================== */

typedef struct {
    PyObject_HEAD
    int16_t  rssi_dbm_x10;
    int16_t  temp_c_x10;
    uint16_t vbat_mv;
    int16_t  cc1101_rssi_x10;
    uint16_t nfc_field_mv;
    uint16_t flags;
    uint32_t uptime_ms;
} PyApexTelemetryObject;

/* ========================================================================
 * ApexDevice Methods
 * ======================================================================== */

static PyObject *
pyapex_device_close(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    if (self->handle) {
        apex_close(self->handle);
        self->handle = NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_sdr_tune(PyApexDeviceObject *self, PyObject *args)
{
    uint32_t freq_hz;
    uint16_t bw_khz;
    double gain_db;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "IHd", &freq_hz, &bw_khz, &gain_db))
        return NULL;

    ret = apex_sdr_tune(self->handle, freq_hz, bw_khz, (float)gain_db);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_sdr_stream_start(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    int ret;
    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    ret = apex_sdr_stream_start(self->handle);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_sdr_stream_stop(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    int ret;
    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    ret = apex_sdr_stream_stop(self->handle);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_ant_select(PyApexDeviceObject *self, PyObject *args)
{
    int ant;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "i", &ant))
        return NULL;

    ret = apex_ant_select(self->handle, (apex_antenna_t)ant);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_cc1101_write(PyApexDeviceObject *self, PyObject *args)
{
    uint8_t reg_addr;
    Py_buffer data_buf;
    apex_cc1101_config_t cfg;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "By*", &reg_addr, &data_buf))
        return NULL;

    if (data_buf.len == 0 || data_buf.len > 64) {
        PyBuffer_Release(&data_buf);
        PyErr_SetString(PyExc_ValueError, "Data length must be 1-64");
        return NULL;
    }

    cfg.reg_addr = reg_addr;
    cfg.reg_len = (uint8_t)data_buf.len;
    memcpy(cfg.data, data_buf.buf, data_buf.len);
    PyBuffer_Release(&data_buf);

    ret = apex_cc1101_write_regs(self->handle, &cfg);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_cc1101_set_channel(PyApexDeviceObject *self, PyObject *args)
{
    uint8_t channel;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "B", &channel))
        return NULL;

    ret = apex_cc1101_set_channel(self->handle, channel);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_cc1101_set_power(PyApexDeviceObject *self, PyObject *args)
{
    int power_dbm;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "i", &power_dbm))
        return NULL;

    ret = apex_cc1101_set_power(self->handle, (int8_t)power_dbm);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_nfc_transact(PyApexDeviceObject *self, PyObject *args)
{
    uint8_t cmd;
    uint8_t flags;
    Py_buffer data_buf;
    apex_nfc_transact_t txn;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "BBy*", &cmd, &flags, &data_buf))
        return NULL;

    if (data_buf.len > 256) {
        PyBuffer_Release(&data_buf);
        PyErr_SetString(PyExc_ValueError, "Data length must be <= 256");
        return NULL;
    }

    memset(&txn, 0, sizeof(txn));
    txn.cmd = cmd;
    txn.flags = flags;
    txn.data_len = (uint16_t)data_buf.len;
    if (data_buf.len > 0)
        memcpy(txn.data, data_buf.buf, data_buf.len);
    PyBuffer_Release(&data_buf);

    ret = apex_nfc_transact(self->handle, &txn);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    /* Return response data as bytes */
    if (txn.data_len > 0)
        return PyBytes_FromStringAndSize((const char *)txn.data, txn.data_len);
    else
        Py_RETURN_NONE;
}

static PyObject *
pyapex_device_get_telemetry(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    apex_telemetry_t telem;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    ret = apex_get_telemetry(self->handle, &telem);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:I}",
        "rssi_dbm_x10",    telem.rssi_dbm_x10,
        "temp_c_x10",      telem.temp_c_x10,
        "vbat_mv",         telem.vbat_mv,
        "cc1101_rssi_x10", telem.cc1101_rssi_x10,
        "nfc_field_mv",    telem.nfc_field_mv,
        "flags",           telem.flags,
        "uptime_ms",       telem.uptime_ms);
}

static PyObject *
pyapex_device_get_status(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    apex_status_t status;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    ret = apex_get_status(self->handle, &status);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    return Py_BuildValue("{s:I,s:N,s:N,s:N}",
        "driver_flags", status.driver_flags,
        "mcu_ready",    PyBool_FromLong(status.mcu_ready),
        "mcu_in_reset", PyBool_FromLong(status.mcu_in_reset),
        "spi_error",    PyBool_FromLong(status.spi_error));
}

static PyObject *
pyapex_device_mcu_reset(PyApexDeviceObject *self, PyObject *args)
{
    int assert_val;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "p", &assert_val))
        return NULL;

    ret = apex_mcu_reset(self->handle, assert_val ? true : false);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_soft_reset(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    ret = apex_soft_reset(self->handle);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_battery_percent(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    apex_telemetry_t telem;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (apex_get_telemetry(self->handle, &telem) != APEX_OK) {
        PyErr_SetString(PyExc_IOError, "Failed to read telemetry");
        return NULL;
    }

    return PyLong_FromUnsignedLong(apex_battery_percent(telem.vbat_mv));
}

static PyObject *
pyapex_device_cc1101_read(PyApexDeviceObject *self, PyObject *args)
{
    uint8_t reg_addr;
    uint8_t reg_len;
    apex_cc1101_config_t cfg;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "BB", &reg_addr, &reg_len))
        return NULL;

    if (reg_len == 0 || reg_len > 64) {
        PyErr_SetString(PyExc_ValueError, "Register length must be 1-64");
        return NULL;
    }

    cfg.reg_addr = reg_addr;
    cfg.reg_len = reg_len;
    ret = apex_cc1101_read_regs(self->handle, &cfg);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    return PyBytes_FromStringAndSize((const char *)cfg.data, cfg.reg_len);
}

static PyObject *
pyapex_device_cc1101_set_band(PyApexDeviceObject *self, PyObject *args)
{
    int band;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "i", &band))
        return NULL;

    ret = apex_cc1101_set_band(self->handle, band);
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pyapex_device_sdr_read_iq(PyApexDeviceObject *self, PyObject *args)
{
    Py_ssize_t buf_len;
    size_t samples_read = 0;
    uint8_t *buf;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "n", &buf_len))
        return NULL;

    if (buf_len <= 0 || buf_len > 65536) {
        PyErr_SetString(PyExc_ValueError, "Buffer size must be 1-65536");
        return NULL;
    }

    buf = (uint8_t *)malloc(buf_len);
    if (!buf) {
        PyErr_NoMemory();
        return NULL;
    }

    ret = apex_sdr_read_iq(self->handle, buf, (size_t)buf_len, &samples_read);
    if (ret != APEX_OK) {
        free(buf);
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    PyObject *result = PyBytes_FromStringAndSize((const char *)buf, samples_read);
    free(buf);
    return result;
}

static PyObject *
pyapex_device_nfc_poll(PyApexDeviceObject *self, PyObject *args)
{
    uint32_t timeout_ms = 0;
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "|I", &timeout_ms))
        return NULL;

    ret = apex_nfc_poll(self->handle, timeout_ms);
    if (ret == APEX_OK) {
        Py_RETURN_TRUE;
    } else if (ret == APEX_ERR_TIMEOUT) {
        Py_RETURN_FALSE;
    } else {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }
}

static PyObject *
pyapex_device_get_firmware_version(PyApexDeviceObject *self, PyObject *Py_UNUSED(ignored))
{
    char version[64];
    int ret;

    if (!self->handle) {
        PyErr_SetString(PyExc_RuntimeError, "Device not open");
        return NULL;
    }

    ret = apex_get_firmware_version(self->handle, version, sizeof(version));
    if (ret != APEX_OK) {
        PyErr_SetString(PyExc_IOError, apex_strerror(ret));
        return NULL;
    }

    return PyUnicode_FromString(version);
}

/* ========================================================================
 * ApexDevice Type Definition
 * ======================================================================== */

static PyMethodDef pyapex_device_methods[] = {
    {"close",              (PyCFunction)pyapex_device_close,              METH_NOARGS,  "Close the device connection"},
    {"sdr_tune",           (PyCFunction)pyapex_device_sdr_tune,          METH_VARARGS, "Tune SDR: sdr_tune(freq_hz, bw_khz, gain_db)"},
    {"sdr_stream_start",   (PyCFunction)pyapex_device_sdr_stream_start,  METH_NOARGS,  "Start SDR IQ streaming"},
    {"sdr_stream_stop",    (PyCFunction)pyapex_device_sdr_stream_stop,   METH_NOARGS,  "Stop SDR IQ streaming"},
    {"sdr_read_iq",       (PyCFunction)pyapex_device_sdr_read_iq,      METH_VARARGS, "Read IQ samples: sdr_read_iq(buf_len)"},
    {"ant_select",         (PyCFunction)pyapex_device_ant_select,        METH_VARARGS, "Select antenna: ant_select(ant_id)"},
    {"cc1101_write",       (PyCFunction)pyapex_device_cc1101_write,      METH_VARARGS, "Write CC1101 regs: cc1101_write(addr, data)"},
    {"cc1101_read",       (PyCFunction)pyapex_device_cc1101_read,       METH_VARARGS, "Read CC1101 regs: cc1101_read(addr, len)"},
    {"cc1101_set_channel", (PyCFunction)pyapex_device_cc1101_set_channel, METH_VARARGS, "Set CC1101 channel: cc1101_set_channel(ch)"},
    {"cc1101_set_power",   (PyCFunction)pyapex_device_cc1101_set_power,  METH_VARARGS, "Set CC1101 TX power: cc1101_set_power(dbm)"},
    {"cc1101_set_band",    (PyCFunction)pyapex_device_cc1101_set_band,   METH_VARARGS, "Set CC1101 band (0=433,1=868,2=915): cc1101_set_band(band)"},
    {"nfc_transact",       (PyCFunction)pyapex_device_nfc_transact,      METH_VARARGS, "NFC transaction: nfc_transact(cmd, flags, data)"},
    {"nfc_poll",           (PyCFunction)pyapex_device_nfc_poll,          METH_VARARGS, "Poll for NFC tag: nfc_poll([timeout_ms])"},
    {"get_telemetry",      (PyCFunction)pyapex_device_get_telemetry,     METH_NOARGS,  "Read telemetry dict"},
    {"get_status",         (PyCFunction)pyapex_device_get_status,        METH_NOARGS,  "Read device status dict"},
    {"get_firmware_version", (PyCFunction)pyapex_device_get_firmware_version, METH_NOARGS, "Read firmware version string"},
    {"mcu_reset",          (PyCFunction)pyapex_device_mcu_reset,       METH_VARARGS, "MCU reset: mcu_reset(assert)"},
    {"soft_reset",         (PyCFunction)pyapex_device_soft_reset,      METH_NOARGS,  "Soft reset MCU via SPI command (requires magic)"},
    {"battery_percent",    (PyCFunction)pyapex_device_battery_percent,  METH_NOARGS,  "Get battery charge estimate (0-100)"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject PyApexDeviceType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "pyapex.ApexDevice",
    .tp_doc       = "GhostBlade hardware device handle",
    .tp_basicsize = sizeof(PyApexDeviceObject),
    .tp_itemsize  = 0,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_methods   = pyapex_device_methods,
};

/* ========================================================================
 * Module-level Functions
 * ======================================================================== */

static PyObject *
pyapex_module_open(PyObject *self, PyObject *args)
{
    const char *path = NULL;
    PyApexDeviceObject *dev_obj;

    if (!PyArg_ParseTuple(args, "|s", &path))
        return NULL;

    dev_obj = PyObject_New(PyApexDeviceObject, &PyApexDeviceType);
    if (!dev_obj)
        return NULL;

    dev_obj->handle = apex_open(path);
    if (!dev_obj->handle) {
        Py_DECREF(dev_obj);
        PyErr_SetString(PyExc_IOError, "Failed to open Apex device");
        return NULL;
    }

    return (PyObject *)dev_obj;
}

static PyObject *
pyapex_module_strerror(PyObject *self, PyObject *args)
{
    int error;
    if (!PyArg_ParseTuple(args, "i", &error))
        return NULL;
    return PyUnicode_FromString(apex_strerror(error));
}

static PyObject *
pyapex_module_battery_percent(PyObject *self, PyObject *args)
{
    uint16_t vbat_mv;
    if (!PyArg_ParseTuple(args, "H", &vbat_mv))
        return NULL;
    return PyLong_FromUnsignedLong(apex_battery_percent(vbat_mv));
}

/* ========================================================================
 * Module Definition
 * ======================================================================== */

/* Antenna constants */
#define PYAPEX_ANT_MIMO_TX     0
#define PYAPEX_ANT_MIMO_RX     1
#define PYAPEX_ANT_SUBGHZ     2
#define PYAPEX_ANT_TERMINATED  3

static PyMethodDef pyapex_module_methods[] = {
    {"open",             pyapex_module_open,             METH_VARARGS, "Open Apex device: open([path])"},
    {"strerror",         pyapex_module_strerror,         METH_VARARGS, "Error code to string: strerror(code)"},
    {"battery_percent",  pyapex_module_battery_percent,  METH_VARARGS, "Estimate battery %: battery_percent(vbat_mv)"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef pyapex_module = {
    PyModuleDef_HEAD_INIT,
    .m_name    = "pyapex",
    .m_doc     = "Python bindings for GhostBlade hardware (libapex)",
    .m_size    = -1,
    .m_methods = pyapex_module_methods,
};

PyMODINIT_FUNC PyInit_pyapex(void)
{
    PyObject *m;

    if (PyType_Ready(&PyApexDeviceType) < 0)
        return NULL;

    m = PyModule_Create(&pyapex_module);
    if (!m)
        return NULL;

    /* Add constants */
    PyModule_AddIntConstant(m, "ANT_MIMO_TX",    PYAPEX_ANT_MIMO_TX);
    PyModule_AddIntConstant(m, "ANT_MIMO_RX",    PYAPEX_ANT_MIMO_RX);
    PyModule_AddIntConstant(m, "ANT_SUBGHZ",     PYAPEX_ANT_SUBGHZ);
    PyModule_AddIntConstant(m, "ANT_TERMINATED", PYAPEX_ANT_TERMINATED);

    PyModule_AddIntConstant(m, "OK",             APEX_OK);
    PyModule_AddIntConstant(m, "ERR_INVALID_ARG", APEX_ERR_INVALID_ARG);
    PyModule_AddIntConstant(m, "ERR_NO_DEVICE",  APEX_ERR_NO_DEVICE);
    PyModule_AddIntConstant(m, "ERR_OPEN_FAILED", APEX_ERR_OPEN_FAILED);
    PyModule_AddIntConstant(m, "ERR_IOCTL_FAILED", APEX_ERR_IOCTL_FAILED);
    PyModule_AddIntConstant(m, "ERR_TIMEOUT",    APEX_ERR_TIMEOUT);
    PyModule_AddIntConstant(m, "ERR_COMM",       APEX_ERR_COMM);
    PyModule_AddIntConstant(m, "ERR_NOT_READY",  APEX_ERR_NOT_READY);
    PyModule_AddIntConstant(m, "ERR_NOMEM",      APEX_ERR_NOMEM);

    /* CC1101 band constants */
    PyModule_AddIntConstant(m, "CC1101_BAND_433", 0);
    PyModule_AddIntConstant(m, "CC1101_BAND_868", 1);
    PyModule_AddIntConstant(m, "CC1101_BAND_915", 2);

    PyModule_AddStringConstant(m, "VERSION", LIBAPEX_VERSION_STRING);

    /* Add ApexDevice type */
    Py_INCREF(&PyApexDeviceType);
    if (PyModule_AddObject(m, "ApexDevice", (PyObject *)&PyApexDeviceType) < 0) {
        Py_DECREF(&PyApexDeviceType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}