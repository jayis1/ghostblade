#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Exercise the production ISR parser; no MMIO or interrupt timing is emulated."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Spi0Rejection(unittest.TestCase):
    def test_rejection_cleanup_and_recovery(self):
        source = (ROOT / "firmware/rp2350b/src/spi0_isr.c").read_text()
        # The only host substitution is the ARM memory barrier. MMIO paths are
        # compiled but never executed; this does not test interrupt ordering.
        source = re.sub(r'__asm__ volatile\s*\("dmb" ::: "memory"\);',
                        '(void)0;', source)
        harness = r'''
#include <assert.h>
/* Independent bitwise CRC reference, using the existing wire polynomials. */
uint64_t crc64_compute(const uint8_t *data, uint32_t len) {
    uint64_t crc = UINT64_MAX;
    while (len--) {
        crc ^= *data++;
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT64_C(0x42F0E1EBA9EA3693) : 0);
    }
    return crc ^ UINT64_MAX;
}
uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    uint32_t crc = UINT32_MAX;
    while (len--) {
        crc ^= *data++;
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return crc ^ UINT32_MAX;
}
static uint8_t frame[SPI_FRAME_BUF_SIZE];
static unsigned build(unsigned len, unsigned reserved, bool bad_crc) {
    memset(frame, 0, sizeof(frame));
    frame[0] = SPI_SYNC_BYTE;
    frame[2] = len & 255;
    frame[3] = len >> 8;
    if (reserved) frame[reserved] = 1;
    uint64_t hdr_crc = crc64_compute(frame, 8);
    for (unsigned i = 0; i < 8; ++i) frame[8 + i] = hdr_crc >> (8 * i);
    memset(frame + SPI_HDR_SIZE, 0x5a, len);
    uint32_t crc = crc32_compute(frame + SPI_HDR_SIZE, len) ^ bad_crc;
    for (unsigned i = 0; i < 4; ++i) frame[SPI_HDR_SIZE + len + i] = crc >> (8 * i);
    return SPI_HDR_SIZE + len + SPI_CRC32_SIZE;
}
static void feed(unsigned start, unsigned end) {
    for (unsigned i = start; i < end; ++i) spi0_process_byte(frame[i]);
}
static void assert_cleared(void) {
    for (unsigned i = 0; i < sizeof(spi0_rx.buf); ++i)
        assert(spi0_rx.buf[i] == 0);
    assert(!spi0_rx.frame_ready);
    assert(spi0_rx.pos == 0 && spi0_rx.payload_len == 0);
}
static void recover(void) {
    unsigned len = build(3, 0, false);
    feed(0, len);
    const uint8_t *buf;
    uint16_t size;
    assert(spi0_rx_get_frame(&buf, &size));
    assert(size == len && memcmp(buf, frame, size) == 0);
    spi0_rx_release_frame();
    assert_cleared();
}
int main(void) {
    const unsigned lengths[] = { 0, 1, 256, SPI_MAX_PAYLOAD };
    for (unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
        spi0_rx_reset();
        unsigned len = build(lengths[n], 0, true);
        uint32_t rejects = spi0_stats.frames_rejected_pay_crc;
        feed(0, len - 1);
        assert(spi0_rx.state == FRAME_STATE_PAYLOAD);
        assert(!spi0_rx.frame_ready);
        for (unsigned i = 0; i < lengths[n]; ++i)
            assert(spi0_rx.buf[SPI_HDR_SIZE + i] == 0x5a);
        feed(len - 1, len);
        assert(spi0_stats.frames_rejected_pay_crc == rejects + 1);
        assert_cleared();
        recover();
    }
    for (unsigned reserved = 4; reserved < 8; ++reserved) {
        spi0_rx_reset();
        build(0, reserved, false);
        feed(0, SPI_HDR_SIZE);
        assert(spi0_rx.state != FRAME_STATE_PAYLOAD);
        assert_cleared();
        recover();
    }
    /* Header CRC and oversized-length failures must retain immediate resync. */
    spi0_rx_reset();
    build(0, 0, false);
    frame[8] ^= 1;
    feed(0, SPI_HDR_SIZE);
    assert_cleared();
    recover();
    build(0, 0, false);
    frame[2] = 0xff;
    frame[3] = 0xff;
    uint64_t crc = crc64_compute(frame, 8);
    for (unsigned i = 0; i < 8; ++i) frame[8 + i] = crc >> (8 * i);
    feed(0, SPI_HDR_SIZE);
    assert_cleared();
    recover();
    /* Defensive overflow paths must clamp the wipe and recover safely. */
    spi0_rx_reset();
    memset(spi0_rx.buf, 0x5a, sizeof(spi0_rx.buf));
    spi0_rx.state = FRAME_STATE_PAYLOAD;
    spi0_rx.pos = UINT16_MAX;
    spi0_process_byte(0);
    assert_cleared();
    recover();
    spi0_rx.state = FRAME_STATE_PAYLOAD;
    spi0_rx.payload_len = 0;
    spi0_rx.pos = SPI_HDR_SIZE + SPI_CRC32_SIZE;
    memset(spi0_rx.buf, 0x5a, spi0_rx.pos);
    spi0_process_byte(0);
    assert_cleared();
    recover();
    /* Pending valid frames remain owned by the main loop, not rejection. */
    unsigned len = build(1, 0, false);
    feed(0, len);
    spi0_process_byte(0);
    assert(spi0_rx.frame_ready && spi0_rx.state == FRAME_STATE_COMPLETE);
    assert(memcmp(spi0_rx.buf, frame, len) == 0);
    spi0_rx_release_frame();
    assert_cleared();
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-spi0-reject-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(source + harness)
            result = subprocess.run([
                os.environ.get("CC", "gcc"), "-std=c11", "-O2", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=undefined,bounds",
                "-fno-sanitize-recover=all", "-I" + str(ROOT / "firmware/rp2350b/include"),
                str(path / "test.c"), "-o", str(path / "test")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
