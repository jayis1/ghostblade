#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Author: jayis1
"""Check the production read path with fault-injected host kernel stubs.

This verifies lock/allocation ordering and retry logic, not kernel scheduling,
device removal lifetimes, or real hardware behavior.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_kernel_spi_buffers import production_function

ROOT = Path(__file__).resolve().parents[1]


class KernelReadProduction(unittest.TestCase):
    def test_read_faults_and_retry(self):
        source = (ROOT / "software/linux-drivers/src/apex_bridge.c").read_text()
        function = production_function(source, "apex_bridge_read", "ssize_t")
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
typedef long long loff_t;
#define __user
#define GFP_KERNEL 0
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
struct fifo { unsigned int len; };
struct apex_bridge_dev { void *spi; int rx_lock, rx_waitq; struct fifo rx_fifo; };
struct file { struct apex_bridge_dev *private_data; int f_flags; };
static bool locked, fail_alloc, fail_copy, drain_on_alloc, drain_after_wait;
static int waits, wait_error, allocations, frees, copies, fifo_reads;
static unsigned int partial_on_alloc;
static struct apex_bridge_dev *active;
static void spin_lock(int *lock) {
    (void)lock; assert(!locked);
    if (drain_after_wait && waits == 1) {
        active->rx_fifo.len = 0; drain_after_wait = false;
    }
    locked = true;
}
static void spin_unlock(int *lock) { (void)lock; assert(locked); locked = false; }
static bool kfifo_is_empty(struct fifo *fifo) { return fifo->len == 0; }
static unsigned int kfifo_len(struct fifo *fifo) { assert(locked); return fifo->len; }
static void *kmalloc(size_t n, int flags) {
    (void)flags; assert(!locked); assert(n > 0); ++allocations;
    if (drain_on_alloc) { active->rx_fifo.len = 0; drain_on_alloc = false; }
    if (partial_on_alloc) { active->rx_fifo.len = partial_on_alloc; partial_on_alloc = 0; }
    return fail_alloc ? NULL : malloc(n);
}
static void kfree_sensitive(void *p) { assert(!locked); ++frees; free(p); }
static unsigned int kfifo_out(struct fifo *fifo, void *buf, size_t n) {
    assert(locked); ++fifo_reads;
    if (n > fifo->len) n = fifo->len;
    memset(buf, 0x5a, n); fifo->len -= n; return n;
}
static int copy_to_user(void *dst, const void *src, size_t n) {
    assert(!locked); ++copies;
    if (fail_copy) return 1;
    memcpy(dst, src, n); return 0;
}
static int wait_for_data(int *queue) {
    (void)queue; assert(!locked); assert(++waits < 4);
    if (wait_error) return wait_error;
    active->rx_fifo.len = 3; return 0;
}
#define wait_event_interruptible(queue, predicate) \
    ((predicate) ? 0 : wait_for_data(&(queue)))
'''
        main = r'''
int main(void) {
    struct apex_bridge_dev dev = { .spi = (void *)1 };
    struct file file = { .private_data = &dev };
    char output[16];
    active = &dev;
    dev.rx_fifo.len = 8; fail_alloc = true;
    assert(apex_bridge_read(&file, output, 4, NULL) == -ENOMEM);
    assert(!locked && dev.rx_fifo.len == 8);
    dev.rx_fifo.len = 0; fail_alloc = false; allocations = 0;
    assert(apex_bridge_read(&file, NULL, 0, NULL) == 0);
    file.f_flags = O_NONBLOCK;
    assert(apex_bridge_read(&file, NULL, 0, NULL) == 0);
    assert(allocations == 0 && waits == 0 && fifo_reads == 0 && copies == 0);
    assert(apex_bridge_read(&file, output, sizeof(output), NULL) == -EAGAIN);
    assert(allocations == 0 && !locked);
    dev.rx_fifo.len = 8; fail_alloc = true;
    assert(apex_bridge_read(&file, output, 4, NULL) == -ENOMEM);
    assert(dev.rx_fifo.len == 8 && !locked && fifo_reads == 0);
    fail_alloc = false;
    assert(apex_bridge_read(&file, output, 4, NULL) == 4);
    assert(dev.rx_fifo.len == 4 && output[0] == 0x5a && !locked);
    fail_copy = true;
    assert(apex_bridge_read(&file, output, 4, NULL) == -EFAULT);
    assert(!locked); fail_copy = false;
    file.f_flags = 0; wait_error = -EINTR;
    assert(apex_bridge_read(&file, output, 4, NULL) == -EINTR);
    assert(!locked); wait_error = 0; waits = 0;
    assert(apex_bridge_read(&file, output, sizeof(output), NULL) == 3);
    assert(waits == 1 && !locked);
    waits = 0; drain_after_wait = true;
    assert(apex_bridge_read(&file, output, sizeof(output), NULL) == 3);
    assert(waits == 2 && !locked);
    dev.rx_fifo.len = 8; partial_on_alloc = 2;
    memset(output, 0, sizeof(output));
    assert(apex_bridge_read(&file, output, sizeof(output), NULL) == 2);
    assert(output[0] == 0x5a && output[1] == 0x5a && output[2] == 0);
    assert(dev.rx_fifo.len == 0 && !locked);
    /* Another reader consumes the FIFO while this reader allocates. */
    dev.rx_fifo.len = 8; drain_on_alloc = true; waits = 0;
    assert(apex_bridge_read(&file, output, sizeof(output), NULL) == 3);
    assert(waits == 1 && !locked);
    dev.rx_fifo.len = 8; drain_on_alloc = true; file.f_flags = O_NONBLOCK;
    assert(apex_bridge_read(&file, output, sizeof(output), NULL) == -EAGAIN);
    assert(!locked && allocations == frees + 1); /* failed allocation */
    file.private_data = NULL;
    assert(apex_bridge_read(&file, output, 4, NULL) == -ENODEV);
    dev.spi = NULL; file.private_data = &dev;
    assert(apex_bridge_read(&file, output, 4, NULL) == -ENODEV);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ghostblade-read-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(harness + function + main)
            result = subprocess.run([
                os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wno-unused-parameter", "-fsanitize=undefined,bounds",
                "-fno-sanitize-recover=all", str(path / "test.c"),
                "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
