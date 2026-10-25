#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Compile production SPI helpers with narrow host kernel API stubs.

This checks buffer contracts, not kernel concurrency or hardware SPI behavior.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def production_function(source, name):
    start = source.index("static int " + name + "(")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class KernelSpiBuffers(unittest.TestCase):
    def test_production_helpers(self):
        source = (ROOT / "software/linux-drivers/src/apex_bridge.c").read_text()
        functions = "\n".join(production_function(source, name) for name in (
            "apex_spi_xfer", "apex_build_frame"))
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define APEX_SPI_FRAME_SIZE_MAX 4112
#define APEX_SPI_HDR_SIZE 16
#define APEX_SPI_CRC32_SIZE 4
#define APEX_SPI_MAX_PAYLOAD 4092
#define APEX_SPI_SYNC_BYTE 0xAA
#define APEX_STATE_SPI_ERROR 2
#define READ_ONCE(x) (x)
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define cpu_to_le16(x) (x)
#define dev_err(...) ((void)0)
struct apex_spi_header {
    uint8_t sync, cmd;
    uint16_t len;
    uint32_t reserved;
    uint64_t crc;
} __attribute__((packed));
struct spi_transfer {
    const void *tx_buf;
    void *rx_buf;
    size_t len;
    int speed_hz, bits_per_word, cs_change;
};
struct spi_message { struct spi_transfer *xfer; };
struct apex_bridge_dev { void *spi; int lock, spi_err_count; unsigned long flags; };
static int spi_speed_hz = 50000000;
static int sync_calls, sync_error;
static void mutex_lock(int *lock) { (void)lock; }
static void mutex_unlock(int *lock) { (void)lock; }
static void set_bit(int bit, unsigned long *flags) { *flags |= 1UL << bit; }
static void atomic_inc(int *count) { ++*count; }
static void spi_message_init_with_transfers(struct spi_message *msg,
                                          struct spi_transfer *xfer, int n) {
    assert(n == 1); msg->xfer = xfer;
}
static int spi_sync(void *spi, struct spi_message *msg) {
    (void)spi; ++sync_calls;
    if (sync_error) return sync_error;
    memset(msg->xfer->rx_buf, 0x5A, msg->xfer->len);
    return 0;
}
/* CRC stubs isolate buffer ownership; CRC correctness has separate tests. */
static uint64_t apex_crc64(const uint8_t *p, size_t n) { (void)p; (void)n; return 0; }
static uint32_t apex_crc32(const uint8_t *p, size_t n) { (void)p; (void)n; return 0; }
static void put_unaligned_le64(uint64_t value, void *p) { memcpy(p, &value, 8); }
static void put_unaligned_le32(uint32_t value, void *p) { memcpy(p, &value, 4); }
'''
        main = r'''
int main(void) {
    uint8_t frame[APEX_SPI_FRAME_SIZE_MAX], rx[APEX_SPI_FRAME_SIZE_MAX];
    uint8_t payload[] = { 0x13, 0x37, 0xAA };
    struct apex_bridge_dev dev = {0};
    for (size_t n = 0; n <= sizeof(payload); ++n) {
        memset(frame, 0xA5, sizeof(frame));
        int len = apex_build_frame(0xFF, payload, n, frame, sizeof(frame));
        assert(len == (int)(20 + n));
        assert(frame[0] == 0xAA && frame[1] == 0xFF);
        assert(frame[2] == n && frame[3] == 0);
        assert(memcmp(frame + 16, payload, n) == 0);
        for (size_t i = len; i < sizeof(frame); ++i) assert(frame[i] == 0);
    }
    memset(frame, 0xA5, sizeof(frame));
    assert(apex_build_frame(0xFF, NULL, 1, frame, sizeof(frame)) == -EINVAL);
    assert(apex_build_frame(0xFF, NULL, 0, NULL, sizeof(frame)) == -EINVAL);
    assert(apex_build_frame(0xFF, payload, 3, frame, 22) == -EMSGSIZE);
    assert(apex_build_frame(0xFF, payload, 4093, frame, sizeof(frame)) == -EMSGSIZE);
    for (size_t i = 0; i < sizeof(frame); ++i) assert(frame[i] == 0xA5);
    uint8_t maximum[APEX_SPI_MAX_PAYLOAD];
    uint8_t guarded[APEX_SPI_FRAME_SIZE_MAX + 2];
    memset(maximum, 0xAA, sizeof(maximum));
    memset(guarded, 0xA5, sizeof(guarded));
    assert(apex_build_frame(0xFF, maximum, sizeof(maximum), guarded + 1,
                            sizeof(guarded) - 2) == APEX_SPI_FRAME_SIZE_MAX);
    assert(guarded[0] == 0xA5 && guarded[sizeof(guarded) - 1] == 0xA5);
    assert(memcmp(guarded + 17, maximum, sizeof(maximum)) == 0);
    assert(apex_build_frame(0xFF, NULL, 0, frame, sizeof(frame)) == 20);
    assert(apex_spi_xfer(&dev, frame, sizeof(frame), rx, sizeof(rx)) == sizeof(rx));
    assert(sync_calls == 1);
    assert(apex_spi_xfer(&dev, frame, 21, rx, 20) == -EMSGSIZE);
    assert(apex_spi_xfer(&dev, frame, sizeof(frame) + 1, rx, sizeof(rx)) == -EMSGSIZE);
    assert(apex_spi_xfer(&dev, NULL, 20, rx, sizeof(rx)) == -EINVAL);
    assert(apex_spi_xfer(&dev, frame, 20, NULL, sizeof(rx)) == -EINVAL);
    assert(sync_calls == 1);
    sync_error = -EIO;
    assert(apex_spi_xfer(&dev, frame, 20, rx, sizeof(rx)) == -EIO);
    assert(dev.spi_err_count == 1 && dev.flags == (1UL << APEX_STATE_SPI_ERROR));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-spi-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + functions + main)
            command = [os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                       "-Werror", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
                       str(path / "test.c"), "-o", str(path / "test")]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
