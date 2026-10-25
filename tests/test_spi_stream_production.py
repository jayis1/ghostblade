#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Exercise the production stream handler with injected DMA failures.

Host stubs check state and cleanup, not DMA hardware or multicore ordering.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_kernel_spi_buffers import production_function

ROOT = Path(__file__).resolve().parents[1]


class SpiStreamProduction(unittest.TestCase):
    def test_dma_failure_and_retry(self):
        source = (ROOT / "firmware/rp2350b/src/spi_protocol.c").read_text()
        function = production_function(source, "handle_cmd_sdr_stream", "void")
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static struct { unsigned cmd_sdr_stream_rx; } proto_stats;
static struct { bool brownout_active, sdr_streaming, sdr_rx_enabled; } device_state;
static bool rx_enabled, lna_enabled;
static int start_result, starts, stops;
static void apex_sdr_rx_enable(bool enabled) { rx_enabled = enabled; }
static void apex_sdr_lna_enable(bool enabled) { lna_enabled = enabled; }
static int sdr_dma_start(void) { ++starts; return start_result; }
static void sdr_dma_stop(void) { ++stops; }
'''
        main = r'''
int main(void) {
    uint8_t enable = 1, disable = 0;
    handle_cmd_sdr_stream(&enable, 0);
    assert(starts == 0);
    device_state.brownout_active = true;
    handle_cmd_sdr_stream(&enable, 1);
    assert(starts == 0 && !rx_enabled && !lna_enabled);
    device_state.brownout_active = false;
    start_result = -1;
    handle_cmd_sdr_stream(&enable, 1);
    assert(starts == 1);
    assert(!device_state.sdr_streaming && !device_state.sdr_rx_enabled);
    assert(!rx_enabled && !lna_enabled);
    start_result = 0;
    handle_cmd_sdr_stream(&enable, 1);
    assert(starts == 2 && device_state.sdr_streaming && device_state.sdr_rx_enabled);
    assert(rx_enabled && lna_enabled);
    handle_cmd_sdr_stream(&enable, 1);
    assert(starts == 2);
    handle_cmd_sdr_stream(&disable, 1);
    assert(stops == 1 && !device_state.sdr_streaming && !device_state.sdr_rx_enabled);
    assert(!rx_enabled && !lna_enabled);
    handle_cmd_sdr_stream(&disable, 1);
    assert(stops == 1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-stream-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + function + main)
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
