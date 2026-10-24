# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Exercise the production extension's descriptor ownership without hardware."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="pyapex-lifecycle-")
        cls.addClassCleanup(cls.build.cleanup)
        build = subprocess.run(
            [sys.executable, "setup.py", "build_ext",
             "--build-lib", cls.build.name,
             "--build-temp", str(Path(cls.build.name) / "obj")],
            cwd=ROOT / "software/libapex", text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if build.returncode:
            raise RuntimeError(build.stdout)
        cls.environment = dict(os.environ, PYTHONPATH=cls.build.name)

    def run_isolated(self, code):
        # Constructor failure must not damage the test runner's own stdin.
        result = subprocess.run(
            [sys.executable, "-c", "import os, gc, pyapex\n" + code],
            env=self.environment, stdin=subprocess.PIPE, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_failed_constructor_preserves_stdin(self):
        self.run_isolated("""
original = os.fstat(0)
for kwargs in ({'device': 123}, {'unexpected': True}):
    try:
        pyapex.ApexBridge(**kwargs)
    except TypeError:
        pass
    else:
        raise AssertionError('invalid constructor accepted')
    gc.collect()
    assert os.fstat(0) == original
""")

    def test_uninitialized_object_owns_no_descriptor(self):
        self.run_isolated("""
original = os.fstat(0)
dev = pyapex.ApexBridge.__new__(pyapex.ApexBridge)
assert dev.fileno() == -1
del dev
gc.collect()
assert os.fstat(0) == original
""")

    def test_reinitialization_does_not_leak(self):
        self.run_isolated("""
dev = pyapex.ApexBridge('/dev/null')
before = len(os.listdir('/proc/self/fd'))
for _ in range(100):
    dev.__init__('/dev/null')
    os.fstat(dev.fileno())
assert len(os.listdir('/proc/self/fd')) == before
del dev
gc.collect()
assert len(os.listdir('/proc/self/fd')) == before - 1
""")

    def test_failed_reinitialization_preserves_open_device(self):
        self.run_isolated("""
dev = pyapex.ApexBridge('/dev/null')
fd = dev.fileno()
before = len(os.listdir('/proc/self/fd'))
try:
    dev.__init__('/dev/null/does-not-exist')
except OSError:
    pass
else:
    raise AssertionError('open unexpectedly succeeded')
assert dev.fileno() == fd
os.fstat(fd)
assert len(os.listdir('/proc/self/fd')) == before
del dev
gc.collect()
assert len(os.listdir('/proc/self/fd')) == before - 1
""")

    def test_close_is_idempotent_and_does_not_close_reused_fd(self):
        self.run_isolated("""
dev = pyapex.ApexBridge('/dev/null')
fd = dev.fileno()
assert dev.close() is None
assert dev.fileno() == -1
replacement = os.open('/dev/null', os.O_RDONLY)
assert replacement == fd
assert dev.close() is None
del dev
gc.collect()
os.fstat(replacement)
os.close(replacement)
""")

    def test_zero_is_a_valid_owned_descriptor(self):
        self.run_isolated("""
os.close(0)
dev = pyapex.ApexBridge('/dev/null')
assert dev.fileno() == 0
dev.close()
try:
    os.fstat(0)
except OSError:
    pass
else:
    raise AssertionError('owned fd zero was not closed')
""")

    def test_closed_object_can_be_reinitialized(self):
        self.run_isolated("""
dev = pyapex.ApexBridge('/dev/null')
dev.close()
dev.__init__('/dev/null')
os.fstat(dev.fileno())
dev.close()
""")


if __name__ == "__main__":
    unittest.main()
