#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Exercise the production soft-reset ioctl case with host API stubs.

This checks error propagation and framing, not a module build or MCU reset.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class KernelSoftReset(unittest.TestCase):
    def test_production_ioctl_case(self):
        source = (ROOT / "software/linux-drivers/src/apex_bridge.c").read_text()
        start = source.index("    case APEX_IOC_SOFT_RESET: {")
        end = source.index("    case APEX_IOC_GET_STATUS:", start)
        case = source[start:end]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define __user
#define APEX_IOC_SOFT_RESET 12
#define APEX_RESET_MAGIC 0xA5F01234U
#define APEX_CMD_RESET_MCU 0xFE
#define APEX_SPI_FRAME_SIZE_MAX 4112
#define dev_err(...) ((void)0)
#define dev_info(...) (++success_logs)
static int copy_error, build_error, transfer_result;
static int build_calls, transfer_calls, success_logs;
static int copy_from_user(void *dst, const void *src, size_t n)
{
    if (copy_error) return 1;
    memcpy(dst, src, n);
    return 0;
}
static int apex_build_frame(uint8_t cmd, const uint8_t *payload,
                            size_t len, uint8_t *frame, size_t capacity)
{
    ++build_calls;
    assert(cmd == APEX_CMD_RESET_MCU && len == 4);
    assert(capacity == APEX_SPI_FRAME_SIZE_MAX);
    for (size_t i = 0; i < len; ++i)
        assert(payload[i] == ((APEX_RESET_MAGIC >> (8 * i)) & 0xFF));
    (void)frame;
    return build_error ? build_error : 24;
}
static int apex_spi_xfer(void *dev, const uint8_t *tx, size_t len,
                         uint8_t *rx, size_t capacity)
{
    (void)dev; (void)tx; (void)rx;
    assert(len == 24 && capacity == APEX_SPI_FRAME_SIZE_MAX);
    ++transfer_calls;
    return transfer_result;
}
static int run_case(uint32_t magic)
{
    unsigned int cmd = APEX_IOC_SOFT_RESET;
    unsigned long arg = (unsigned long)&magic;
    void *dev = NULL;
    uint8_t frame[APEX_SPI_FRAME_SIZE_MAX], rx_buf[APEX_SPI_FRAME_SIZE_MAX];
    int frame_len, ret = -ENOTTY;
    switch (cmd) {
'''
        main = r'''
    }
    return ret;
}
int main(void)
{
    copy_error = 1;
    assert(run_case(APEX_RESET_MAGIC) == -EFAULT);
    assert(!build_calls && !transfer_calls && !success_logs);
    copy_error = 0;
    assert(run_case(0) == -EINVAL);
    assert(!build_calls && !transfer_calls && !success_logs);
    build_error = -EMSGSIZE;
    assert(run_case(APEX_RESET_MAGIC) < 0);
    assert(build_calls == 1 && !transfer_calls && !success_logs);
    build_error = 0;
    const int errors[] = { -EIO, -ETIMEDOUT, -ESHUTDOWN };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        transfer_result = errors[i];
        assert(run_case(APEX_RESET_MAGIC) == errors[i]);
        assert(!success_logs);
    }
    assert(transfer_calls == 3);
    transfer_result = 24;
    assert(run_case(APEX_RESET_MAGIC) == 0);
    assert(transfer_calls == 4 && success_logs == 1);
    transfer_result = 0;
    assert(run_case(APEX_RESET_MAGIC) == 0);
    assert(transfer_calls == 5 && success_logs == 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-reset-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + case + main)
            compiled = subprocess.run(
                [os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                 "-Werror", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
                 str(path / "test.c"), "-o", str(path / "test")],
                capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
