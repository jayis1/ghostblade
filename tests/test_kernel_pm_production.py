#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Test production PM callbacks with fault-injected SPI setup.

Host stubs verify rollback and IRQ ordering, not kernel PM or hardware.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_kernel_spi_buffers import production_function

ROOT = Path(__file__).resolve().parents[1]


class KernelPowerManagement(unittest.TestCase):
    def test_setup_failures_and_recovery(self):
        source = (ROOT / "software/linux-drivers/src/apex_bridge.c").read_text()
        functions = "\n".join(production_function(source, name, "int") for name in (
            "apex_bridge_runtime_suspend", "apex_bridge_runtime_resume"))
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
typedef uint32_t u32;
struct device { int unused; };
struct apex_bridge_dev {
    uint8_t saved_spi_mode;
    uint32_t saved_spi_max_speed_hz;
    int irq;
};
struct spi_device {
    struct device dev;
    uint32_t mode, max_speed_hz;
    struct apex_bridge_dev *data;
};
static struct spi_device spi;
static int setup_result, setup_calls, disables, enables;
static uint32_t expected_mode, expected_speed;
static int setup_completed;
static struct spi_device *to_spi_device(struct device *dev) {
    assert(dev == &spi.dev);
    return &spi;
}
#define spi_get_drvdata(s) ((s)->data)
#define dev_dbg(...) ((void)0)
#define dev_warn(...) ((void)0)
static int spi_setup(struct spi_device *s) {
    assert(s == &spi);
    assert(s->mode == expected_mode && s->max_speed_hz == expected_speed);
    ++setup_calls;
    setup_completed = (setup_result == 0);
    return setup_result;
}
static void disable_irq(int irq) {
    assert(irq == 7 && setup_completed);
    ++disables;
}
static void enable_irq(int irq) {
    assert(irq == 7 && setup_completed);
    ++enables;
}
'''
        main = r'''
int main(void) {
    struct apex_bridge_dev state = { .irq = 7 };
    spi.data = &state;
    spi.mode = 3;
    spi.max_speed_hz = 50000000;
    expected_mode = 3;
    expected_speed = 1000000;
    setup_result = -EIO;
    setup_completed = 0;
    assert(apex_bridge_runtime_suspend(&spi.dev) == -EIO);
    assert(spi.mode == 3 && spi.max_speed_hz == 50000000);
    assert(disables == 0 && enables == 0 && setup_calls == 1);
    setup_result = 0;
    setup_completed = 0;
    assert(apex_bridge_runtime_suspend(&spi.dev) == 0);
    assert(spi.max_speed_hz == 1000000 && disables == 1);
    assert(state.saved_spi_mode == 3 && state.saved_spi_max_speed_hz == 50000000);
    /* Failed resume must preserve the suspended configuration and IRQ. */
    spi.mode = 0;
    expected_speed = 50000000;
    setup_result = -ETIMEDOUT;
    setup_completed = 0;
    assert(apex_bridge_runtime_resume(&spi.dev) == -ETIMEDOUT);
    assert(spi.mode == 0 && spi.max_speed_hz == 1000000);
    assert(enables == 0 && disables == 1);
    setup_result = 0;
    setup_completed = 0;
    assert(apex_bridge_runtime_resume(&spi.dev) == 0);
    assert(spi.mode == 3 && spi.max_speed_hz == 50000000 && enables == 1);
    /* Devices without an IRQ still perform configuration and rollback. */
    state.irq = -1;
    expected_speed = 1000000;
    setup_result = -EIO;
    assert(apex_bridge_runtime_suspend(&spi.dev) == -EIO);
    assert(spi.max_speed_hz == 50000000);
    setup_result = 0;
    assert(apex_bridge_runtime_suspend(&spi.dev) == 0);
    expected_speed = 50000000;
    setup_result = -ETIMEDOUT;
    assert(apex_bridge_runtime_resume(&spi.dev) == -ETIMEDOUT);
    assert(spi.max_speed_hz == 1000000);
    setup_result = 0;
    assert(apex_bridge_runtime_resume(&spi.dev) == 0);
    assert(enables == 1 && disables == 1);
    assert(setup_calls == 8);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-pm-") as tmp:
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
