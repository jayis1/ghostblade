#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Check production parser cleanup with software-only ring/CRC fixtures."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_kernel_spi_buffers import production_function

ROOT = Path(__file__).resolve().parents[1]


class RejectedPayload(unittest.TestCase):
    def test_crc_failure_wipes_payload_and_recovers(self):
        source = (ROOT / "firmware/rp2350b/src/spi_protocol.c").read_text()
        # The shared extractor expects static linkage; only linkage changes here.
        source = source.replace("\nuint32_t crc32_compute(", "\nstatic uint32_t crc32_compute(")
        source = source.replace("\nint spi_protocol_process(", "\nstatic int spi_protocol_process(")
        functions = "\n".join(production_function(source, name, result) for name, result in [
            ("secure_wipe", "void"), ("read_le32", "uint32_t"),
            ("crc32_compute", "uint32_t"), ("validate_frame_payload", "int"),
            ("spi_protocol_process", "int"),
        ])
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define SPI_HDR_SIZE 16
#define SPI_CRC32_SIZE 4
#define SPI_MAX_PAYLOAD 4092
#define SPI_SYNC_BYTE 0xaa
#define SPI_RX_BUF_SIZE 8192
static uint32_t crc32_table[256];
static int crc32_initialized;
enum rx_state { RX_STATE_IDLE, RX_STATE_HEADER, RX_STATE_PAYLOAD,
                RX_STATE_CRC32, RX_STATE_COMPLETE, RX_STATE_ERROR };
static struct {
    enum rx_state state;
    uint8_t hdr_buf[SPI_HDR_SIZE], payload_buf[SPI_MAX_PAYLOAD];
    uint8_t crc32_buf[SPI_CRC32_SIZE];
    uint32_t hdr_idx, payload_idx, crc32_idx;
    uint16_t expected_payload_len;
    uint32_t frames_received, frames_crc_error, frames_sync_error;
    uint32_t frames_dispatched;
} rx_ctx;
static uint8_t spi_rx_buf[SPI_RX_BUF_SIZE];
static uint32_t spi_rx_head, spi_rx_tail;
static unsigned dispatches;
static void dmb(void) { }
static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
/* Header validity is outside this payload-cleanup test's scope. */
static int validate_frame_header(void) { return 0; }
static void dispatch_frame(void) {
    ++dispatches;
    memset(rx_ctx.payload_buf, 0, rx_ctx.expected_payload_len);
}
'''
        main = r'''
static void enqueue(uint8_t byte) {
    spi_rx_buf[spi_rx_head] = byte;
    spi_rx_head = (spi_rx_head + 1) & (SPI_RX_BUF_SIZE - 1);
}
static void frame(unsigned len, bool corrupt) {
    uint8_t payload[SPI_MAX_PAYLOAD];
    memset(payload, 0x5a, sizeof(payload));
    for (unsigned i = 0; i < SPI_HDR_SIZE; ++i)
        enqueue(i == 0 ? SPI_SYNC_BYTE : i == 2 ? len & 255 : i == 3 ? len >> 8 : 0);
    for (unsigned i = 0; i < len; ++i)
        enqueue(payload[i]);
    uint32_t crc = crc32_compute(payload, len) ^ (corrupt ? 1 : 0);
    for (unsigned i = 0; i < 4; ++i)
        enqueue((uint8_t)(crc >> (8 * i)));
}
int main(void) {
    const unsigned lengths[] = { 1, 256, SPI_MAX_PAYLOAD, 0 };
    for (unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
        memset(&rx_ctx, 0, sizeof(rx_ctx));
        spi_rx_head = spi_rx_tail = 0;
        dispatches = 0;
        frame(lengths[n], true);
        /* A trailer split across process calls must not dispatch or wipe early. */
        unsigned end = spi_rx_head;
        spi_rx_head = (end - 1) & (SPI_RX_BUF_SIZE - 1);
        assert(spi_protocol_process() == 0);
        assert(rx_ctx.state == RX_STATE_CRC32 && dispatches == 0);
        for (unsigned i = 0; i < lengths[n]; ++i)
            assert(rx_ctx.payload_buf[i] == 0x5a);
        spi_rx_head = end;
        assert(spi_protocol_process() == 0);
        assert(rx_ctx.frames_crc_error == 1 && dispatches == 0);
        assert(rx_ctx.state == RX_STATE_IDLE);
        for (unsigned i = 0; i < sizeof(rx_ctx.payload_buf); ++i)
            assert(rx_ctx.payload_buf[i] == 0);
        frame(3, false);
        assert(spi_protocol_process() == 1 && dispatches == 1);
        assert(rx_ctx.frames_crc_error == 1);
    }
    /* Back-to-back rejection and recovery within one parser invocation. */
    memset(&rx_ctx, 0, sizeof(rx_ctx));
    spi_rx_head = spi_rx_tail = 0;
    dispatches = 0;
    frame(32, true);
    frame(1, false);
    assert(spi_protocol_process() == 1 && dispatches == 1);
    for (unsigned i = 0; i < sizeof(rx_ctx.payload_buf); ++i)
        assert(rx_ctx.payload_buf[i] == 0);
    secure_wipe(&rx_ctx, sizeof(rx_ctx));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-rejected-payload-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + functions + main)
            result = subprocess.run([
                os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
                str(path / "test.c"), "-o", str(path / "test")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
