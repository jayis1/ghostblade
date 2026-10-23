/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 jayis1
 * Regression tests linked against the production libapex implementation.
 * Only ioctl is wrapped; /dev/null supplies a real, hardware-free handle.
 */
#include "libapex.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int ioctl_calls;
static uint16_t encoded_gain;

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    const uint8_t *payload;

    (void)fd;
    (void)request;
    va_start(args, request);
    payload = va_arg(args, const uint8_t *);
    /* The tune ioctl payload is frequency, bandwidth, then gain. */
    memcpy(&encoded_gain, payload + 6, sizeof(encoded_gain));
    va_end(args);
    ioctl_calls++;
    return 0;
}

static void test_error_extremes(void)
{
    assert(strcmp(apex_strerror(INT_MIN), "Unknown error") == 0);
    assert(strcmp(apex_strerror(INT_MAX), "Unknown error") == 0);
    assert(strcmp(apex_strerror(1), "Unknown error") == 0);
    assert(strcmp(apex_strerror(-9), "Unknown error") == 0);
    assert(strcmp(apex_strerror(APEX_OK), "Success") == 0);
    assert(strcmp(apex_strerror(APEX_ERR_NOMEM), "Out of memory") == 0);
}

static void test_gain_validation(apex_handle_t handle)
{
    const float invalid[] = { NAN, INFINITY, -INFINITY, -0.1f, 6554.0f };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        assert(apex_sdr_tune(handle, 868000000, 10000, invalid[i]) ==
               APEX_ERR_INVALID_ARG);
        assert(apex_last_error(handle) == APEX_ERR_INVALID_ARG);
        assert(ioctl_calls == 0);
    }

    assert(apex_sdr_tune(handle, 868000000, 10000, 0.0f) == APEX_OK);
    assert(encoded_gain == 0);
    assert(apex_sdr_tune(handle, 868000000, 10000, 30.5f) == APEX_OK);
    assert(encoded_gain == 305);
    assert(apex_sdr_tune(handle, 868000000, 10000, 6553.5f) == APEX_OK);
    assert(encoded_gain == UINT16_MAX);
    assert(ioctl_calls == 3);
    assert(apex_last_error(handle) == APEX_OK);
}

static void test_version_capacity(apex_handle_t handle)
{
    static const char expected[] = "GhostBlade v1.0.0";
    char buf[sizeof(expected) + 1];

    for (size_t capacity = 1; capacity < sizeof(expected); capacity++) {
        memset(buf, 'X', sizeof(buf));
        assert(apex_get_firmware_version(handle, buf, capacity) ==
               APEX_ERR_INVALID_ARG);
        assert(buf[0] == '\0');
        assert(buf[capacity] == 'X');
        assert(apex_last_error(handle) == APEX_ERR_INVALID_ARG);
    }

    memset(buf, 'X', sizeof(buf));
    assert(apex_get_firmware_version(handle, buf, sizeof(expected)) == APEX_OK);
    assert(strcmp(buf, expected) == 0);
    assert(buf[sizeof(expected)] == 'X');
    assert(apex_last_error(handle) == APEX_OK);
    assert(apex_get_firmware_version(handle, buf, 0) == APEX_ERR_INVALID_ARG);
    assert(apex_get_firmware_version(handle, NULL, sizeof(buf)) ==
           APEX_ERR_INVALID_ARG);
}

int main(void)
{
    apex_handle_t handle = apex_open("/dev/null");

    assert(handle != NULL);
    test_error_extremes();
    test_gain_validation(handle);
    test_version_capacity(handle);
    apex_close(handle);
    puts("libapex production API regression tests passed");
    return 0;
}
