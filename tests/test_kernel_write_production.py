#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Test production write() boundaries and cleanup with host kernel API stubs.

This exercises the shipped write function, not a module or SPI hardware.
Framing/transport stubs check buffer contracts and inject failures.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class KernelWriteProduction(unittest.TestCase):
    def test_write_boundaries_and_failures(self):
        source = (ROOT / "software/linux-drivers/src/apex_bridge.c").read_text()
        start = source.index("static ssize_t apex_bridge_write(")
        end = source.index("static long apex_bridge_ioctl(", start)
        function = source[start:end]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
typedef long long loff_t;
typedef uint8_t u8;
typedef uint16_t u16;
#define __user
#define GFP_KERNEL 0
#define APEX_SPI_MAX_PAYLOAD 4092
#define APEX_SPI_FRAME_SIZE_MAX 4112
struct apex_bridge_dev { void *spi; };
struct file { void *private_data; };
static unsigned int allocations, live, fail_allocation, builds, transfers;
static unsigned int validations, pushes;
static bool copy_error;
static int build_error, transfer_error;
static const uint8_t *expected;
static size_t expected_count;
static void *kmalloc(size_t n, int flags)
{
    (void)flags;
    if (++allocations == fail_allocation) return NULL;
    void *p = malloc(n);
    assert(p);
    ++live;
    return p;
}
static void kfree_sensitive(void *p)
{
    if (p) { assert(live); --live; free(p); }
}
static int copy_from_user(void *dst, const void *src, size_t n)
{
    assert(n == expected_count);
    if (copy_error) return 1;
    memcpy(dst, src, n);
    return 0;
}
static int apex_build_frame(uint8_t cmd, const uint8_t *payload,
                            uint16_t n, uint8_t *frame, size_t capacity)
{
    ++builds;
    assert(cmd == expected[0]);
    assert(n == expected_count - 1 && n <= APEX_SPI_MAX_PAYLOAD);
    assert(capacity == APEX_SPI_FRAME_SIZE_MAX);
    assert(memcmp(payload, expected + 1, n) == 0);
    memset(frame, 0, capacity);
    return build_error ? build_error : n + 20;
}
static int apex_spi_xfer(struct apex_bridge_dev *dev, const uint8_t *tx,
                         size_t n, uint8_t *rx, size_t capacity)
{
    ++transfers;
    assert(dev && tx && rx);
    assert(n == expected_count - 1 + 20 && n <= capacity);
    assert(capacity == APEX_SPI_FRAME_SIZE_MAX);
    memset(rx, 0, capacity);
    return transfer_error ? transfer_error : (int)n;
}
static int apex_validate_frame(const uint8_t *frame, size_t n,
                               uint8_t *cmd, uint16_t *len,
                               const uint8_t **payload)
{
    ++validations;
    assert(frame && n > 0);
    *cmd = 0;
    *len = 1;
    *payload = frame;
    return 0;
}
static bool apex_rx_fifo_push(struct apex_bridge_dev *dev,
                              const uint8_t *payload, uint16_t n)
{
    ++pushes;
    assert(dev && payload && n == 1);
    return true;
}
'''
        main = r'''
static ssize_t run(struct file *file, const uint8_t *input, size_t n)
{
    expected = input;
    expected_count = n;
    allocations = builds = transfers = validations = pushes = 0;
    assert(live == 0);
    ssize_t ret = apex_bridge_write(file, (const char *)input, n, NULL);
    assert(live == 0);
    return ret;
}
int main(void)
{
    uint8_t input[APEX_SPI_MAX_PAYLOAD + 2];
    for (size_t i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)i;
    struct apex_bridge_dev dev = { .spi = input };
    struct file file = { .private_data = &dev };
    const size_t sizes[] = { 1, 2, APEX_SPI_MAX_PAYLOAD,
                             APEX_SPI_MAX_PAYLOAD + 1 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        assert(run(&file, input, sizes[i]) == (ssize_t)sizes[i]);
        assert(allocations == 3 && builds == 1 && transfers == 1);
        assert(validations == 1 && pushes == 1);
    }
    assert(run(&file, input, 0) == -EINVAL);
    assert(!allocations && !builds && !transfers);
    assert(run(&file, input, sizeof(input)) == -EMSGSIZE);
    assert(!allocations && !builds && !transfers);
    assert(run(&file, input, SIZE_MAX) == -EMSGSIZE);
    assert(!allocations && !builds && !transfers);
    for (fail_allocation = 1; fail_allocation <= 3; ++fail_allocation) {
        assert(run(&file, input, 2) == -ENOMEM);
        assert(allocations == fail_allocation && !builds && !transfers);
    }
    fail_allocation = 0;
    copy_error = true;
    assert(run(&file, input, 2) == -EFAULT);
    assert(allocations == 1 && !builds && !transfers);
    copy_error = false;
    build_error = -EMSGSIZE;
    assert(run(&file, input, 2) == -EMSGSIZE);
    assert(builds == 1 && !transfers);
    build_error = 0;
    const int errors[] = { -EIO, -ETIMEDOUT, -ESHUTDOWN };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        transfer_error = errors[i];
        assert(run(&file, input, 2) == errors[i]);
        assert(transfers == 1 && !validations && !pushes);
    }
    transfer_error = 0;
    assert(run(&file, input, sizeof(input) - 1) == sizeof(input) - 1);
    file.private_data = NULL;
    assert(run(&file, input, 2) == -ENODEV);
    assert(!allocations);
    file.private_data = &dev;
    dev.spi = NULL;
    assert(run(&file, input, 2) == -ENODEV);
    assert(!allocations);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-write-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + function + main)
            compiled = subprocess.run(
                [os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                 "-Werror", "-Wno-unused-parameter", "-fsanitize=undefined,bounds",
                 "-fno-sanitize-recover=all", str(path / "test.c"),
                 "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
