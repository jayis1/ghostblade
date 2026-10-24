#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
# Author: jayis1
"""Exercise the real prerequisite checker and Makefile with isolated fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ToolchainCheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="toolchain-check-", dir=os.environ.get("TMPDIR"))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / "SDK with spaces"
        self.kernel = self.root / "kernel with spaces"
        self.sdk.mkdir()
        self.kernel.mkdir()
        (self.sdk / "pico_sdk_init.cmake").touch()
        (self.kernel / "Makefile").touch()
        self.aarch64 = self.tool("aarch64-linux-gnu-gcc", "aarch64-linux-gnu")
        self.arm = self.tool("arm gcc", "arm-none-eabi")
        self.dtc = self.tool("dtc", "Version: DTC fixture")
        self.cmake = self.tool("cmake", "cmake version fixture")

    def tool(self, name, output, status=0):
        path = self.root / name
        path.write_text(
            f"#!{sys.executable}\nimport sys\nprint({output!r})\nsys.exit({status})\n"
        )
        path.chmod(0o755)
        return path

    def check(self, **overrides):
        options = {
            "aarch64-cc": self.aarch64, "arm-cc": self.arm,
            "dtc": self.dtc, "cmake": self.cmake,
            "pico-sdk": self.sdk, "kernel-dir": self.kernel,
        }
        options.update(overrides)
        command = [sys.executable, str(ROOT / "tools/check_toolchain.py")]
        for name, value in options.items():
            command.extend([f"--{name}", str(value)])
        return subprocess.run(command, capture_output=True, text=True, timeout=30)

    def test_complete_fixture_and_paths_with_spaces(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("OK   AArch64 compiler: aarch64-linux-gnu", result.stdout)

    def test_native_compiler_is_not_aarch64(self):
        native = self.tool("native-gcc", "x86_64-linux-gnu")
        result = self.check(**{"aarch64-cc": native})
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAIL AArch64 compiler: target", result.stdout)

    def test_wrong_bare_metal_target(self):
        wrong = self.tool("wrong-arm", "arm-linux-gnueabihf")
        self.assertEqual(self.check(**{"arm-cc": wrong}).returncode, 1)

    def test_missing_executable_reports_remaining_probes(self):
        result = self.check(**{"aarch64-cc": self.root / "absent"})
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAIL AArch64 compiler", result.stdout)
        self.assertIn("OK   kernel build tree", result.stdout)

    def test_nonzero_and_empty_probes_fail(self):
        for output, status in [("plausible version", 2), ("", 0)]:
            with self.subTest(output=output, status=status):
                broken = self.tool("broken", output, status)
                self.assertEqual(self.check(cmake=broken).returncode, 1)

    def test_empty_directories_are_not_sdk_or_kernel(self):
        (self.sdk / "pico_sdk_init.cmake").unlink()
        (self.kernel / "Makefile").unlink()
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAIL Pico SDK", result.stdout)
        self.assertIn("FAIL kernel build tree", result.stdout)

    @unittest.skipUnless(shutil.which("make"), "GNU Make required")
    def test_make_defaults_to_cross_compiler_and_propagates_failure(self):
        env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ.get("PATH", ""))
        command = [
            "make", "--no-print-directory", "-C", str(ROOT), "check",
            "CROSS_COMPILE=", f"CHECK_ARM_CC={self.arm}", f"CHECK_DTC={self.dtc}",
            f"CHECK_CMAKE={self.cmake}", f"PICO_SDK_PATH={self.sdk}", f"KDIR={self.kernel}",
        ]
        # Pin only the target under test; do not inherit a caller's check override.
        env.pop("CHECK_AARCH64_CC", None)
        env.pop("MAKEFLAGS", None)
        env.pop("MAKEOVERRIDES", None)
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("OK   AArch64 compiler: aarch64-linux-gnu", result.stdout)
        self.aarch64 = self.tool("aarch64-linux-gnu-gcc", "x86_64-linux-gnu")
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAIL AArch64 compiler", result.stdout)
        custom = self.tool("custom-gcc", "aarch64-linux-gnu")
        result = subprocess.run(
            command + [f"CROSS_COMPILE={str(custom)[:-3]}"], env=env,
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
