#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Exercise the production response builder's stack cleanup, without MMIO.

CRC and interrupt stubs isolate buffer lifetime; this is not a firmware build
or a concurrency, wire-integrity, or hardware test.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_kernel_spi_buffers import production_function

ROOT = Path(__file__).resolve().parents[1]


class ResponseCleanup(unittest.TestCase):
    def test_full_ring_cleanup_and_recovery(self):
        source = (ROOT / "firmware/rp2350b/src/spi_protocol.c").read_text()
        wipe = production_function(source, "secure_wipe", "void").replace(
            "secure_wipe(", "production_wipe(", 1)
        helpers = "\n".join(production_function(source, name, result) for name, result in (
            ("write_le16", "void"), ("write_le32", "void"),
            ("write_le64", "void"), ("build_response_frame", "int")))
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "spi_protocol.h"
#define TX_RING_SIZE 4096
static uint8_t tx_ring[TX_RING_SIZE];
static volatile uint32_t tx_head, tx_tail;
static struct { unsigned tx_overruns; } proto_stats;
static unsigned wipes, interrupts;
static void dmb(void) { }
static void int_req_assert(void) { ++interrupts; }
static uint64_t crc64_compute(const uint8_t *data, uint32_t len) {
    (void)data; (void)len; return 0;
}
static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    (void)data; (void)len; return 0;
}
'''
        observe = r'''
static void secure_wipe(void *ptr, size_t len) {
    assert(len == SPI_FRAME_SIZE_MAX);
    production_wipe(ptr, len);
    for (size_t i = 0; i < len; ++i)
        assert(((uint8_t *)ptr)[i] == 0);
    ++wipes;
}
'''
        main = r'''
int main(void) {
    uint8_t payload[SPI_MAX_PAYLOAD];
    const unsigned lengths[] = { 0, 1, 256, SPI_MAX_PAYLOAD };
    memset(payload, 0x5a, sizeof(payload));
    for (unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
        const unsigned len = lengths[n];
        memset(tx_ring, 0xa5, sizeof(tx_ring));
        tx_head = 37;
        tx_tail = 38; /* Full ring, including wrap-around arithmetic. */
        wipes = interrupts = proto_stats.tx_overruns = 0;
        assert(build_response_frame(0x83, len ? payload : NULL, len) == -1);
        assert(wipes == 1 && interrupts == 0 && proto_stats.tx_overruns == 1);
        assert(tx_head == 37 && tx_tail == 38);
        for (unsigned i = 0; i < sizeof(tx_ring); ++i)
            assert(tx_ring[i] == 0xa5);
    }
    /* Exactly enough room, wrapping the frame across the end of the ring. */
    tx_head = TX_RING_SIZE - 8;
    tx_tail = 14; /* 21 available bytes, enough for header + one byte + CRC. */
    wipes = interrupts = 0;
    assert(build_response_frame(0x83, payload, 1) == 0);
    assert(wipes == 1 && interrupts == 1);
    assert(tx_head == 13 && tx_tail == 14);
    assert(tx_ring[TX_RING_SIZE - 8] == SPI_SYNC_BYTE);
    assert(tx_ring[TX_RING_SIZE - 7] == 0x83);
    assert(tx_ring[8] == 0x5a);
    /* Invalid arguments must not publish or copy any payload. */
    wipes = interrupts = 0;
    assert(build_response_frame(0x83, NULL, 1) == -1);
    assert(build_response_frame(0x83, payload, SPI_MAX_PAYLOAD + 1) == -1);
    assert(wipes == 0 && interrupts == 0 && tx_head == 13);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-response-cleanup-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + wipe + observe + helpers + main)
            result = subprocess.run([
                os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
                "-I", str(ROOT / "firmware/rp2350b/include"),
                str(path / "test.c"), "-o", str(path / "test")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
