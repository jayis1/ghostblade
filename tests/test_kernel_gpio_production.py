#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Fault-inject GPIO lookup/request errors in the production init helper.

Host stubs verify error propagation and ordering, not GPIO or kernel lifetimes.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_kernel_spi_buffers import production_function

ROOT = Path(__file__).resolve().parents[1]


class KernelGpioInitialization(unittest.TestCase):
    def test_lookup_errors_requests_and_retry(self):
        source = (ROOT / "software/linux-drivers/src/apex_bridge.c").read_text()
        function = production_function(source, "apex_gpio_init")
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#define EPROBE_DEFER 517
#define GPIOF_IN 0
#define GPIOF_OUT_INIT_HIGH 1
#define GPIOF_OUT_INIT_LOW 2
#define APEX_STATE_MCU_RESET 1
#define dev_err(...) ((void)0)
struct device_node { int unused; };
struct device { struct device_node *of_node; };
struct spi_device { struct device dev; };
struct apex_bridge_dev {
    struct spi_device *spi;
    int gpio_int_req, gpio_host_rdy, gpio_mcu_reset;
    unsigned long flags;
};
static const char *properties[] = {
    "apex,int-req-gpio", "apex,host-rdy-gpio", "apex,mcu-reset-gpio"
};
static const char *labels[] = {
    "apex_int_req", "apex_host_rdy", "apex_mcu_reset"
};
static int values[3], lookups, requests, failed_request, request_error;
static struct device_node node;
static struct spi_device spi = { .dev = { .of_node = &node } };
static int of_get_named_gpio(struct device_node *np, const char *name, int index)
{
    assert(np == &node && index == 0);
    assert(lookups < 3 && !strcmp(name, properties[lookups]));
    return values[lookups++];
}
static int gpio_is_valid(int gpio) { return gpio >= 0 && gpio < 100; }
static int devm_gpio_request_one(struct device *dev, int gpio,
                                unsigned long flags, const char *label)
{
    int i = requests++;
    assert(dev == &spi.dev && i < 3);
    assert(gpio == values[i] && !strcmp(label, labels[i]));
    assert(flags == (unsigned long)i);
    return i == failed_request ? request_error : 0;
}
static void set_bit(int bit, unsigned long *flags) { *flags |= 1UL << bit; }
static struct apex_bridge_dev reset(void)
{
    values[0] = 0; /* GPIO zero is valid. */
    values[1] = 21;
    values[2] = 22;
    lookups = requests = 0;
    failed_request = -1;
    request_error = 0;
    return (struct apex_bridge_dev){ .spi = &spi };
}
'''
        main = r'''
int main(void)
{
    const int errors[] = { -EPROBE_DEFER, -ENOENT, -EINVAL, -EIO };
    struct apex_bridge_dev dev;
    for (int pin = 0; pin < 3; ++pin) {
        for (size_t e = 0; e < sizeof(errors) / sizeof(errors[0]); ++e) {
            dev = reset();
            values[pin] = errors[e];
            assert(apex_gpio_init(&dev) == errors[e]);
            assert(lookups == pin + 1 && requests == 0 && dev.flags == 0);
            /* The next probe can succeed after the provider becomes ready. */
            dev = reset();
            assert(apex_gpio_init(&dev) == 0);
            assert(lookups == 3 && requests == 3 && dev.flags == 2);
        }
        dev = reset();
        values[pin] = 100; /* Positive but invalid GPIO number. */
        assert(apex_gpio_init(&dev) == -EINVAL);
        assert(requests == 0 && dev.flags == 0);
        for (size_t e = 0; e < sizeof(errors) / sizeof(errors[0]); ++e) {
            dev = reset();
            failed_request = pin;
            request_error = errors[e];
            assert(apex_gpio_init(&dev) == errors[e]);
            assert(lookups == 3 && requests == pin + 1 && dev.flags == 0);
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-gpio-") as tmp:
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
