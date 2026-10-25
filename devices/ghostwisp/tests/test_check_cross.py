"""Requested cross-build probes must not silently skip. Author: jayis1."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


CHECK = Path(__file__).resolve().parents[1] / "check.sh"


class CrossPrerequisiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ghostwisp-cross-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.build = self.root / "build"
        # Only controlled tools are visible; the host's installed ARM toolchain
        # must not affect missing-prerequisite cases or access real hardware.
        for tool in ("bash", "mkdir", "head", "tail", "grep", "awk"):
            source = shutil.which(tool)
            if source is None:
                self.fail("required host test tool not found: " + tool)
            (self.tools / tool).symlink_to(source)
        self.stub("arm-none-eabi-gcc", "echo 'test compiler'")
        self.stub("ninja", "exit 0")
        self.stub("arm-none-eabi-readelf", "echo '  Entry point address: 0x10000100'")
        self.stub("cmake", 'if [ "$1" = "--build" ]; then : > ghostwisp.elf; fi')

    def stub(self, name, body):
        path = self.tools / name
        path.write_text("#!/bin/bash\n" + body + "\n")
        path.chmod(0o755)

    def probe(self):
        return subprocess.run(
            ["/bin/bash", str(CHECK), "cross", str(self.root / "firmware"),
             str(self.root / "sdk"), str(self.build)],
            env={**os.environ, "PATH": str(self.tools)},
            text=True, capture_output=True, timeout=10,
        )

    def test_each_missing_required_tool_fails_before_build(self):
        for tool in ("arm-none-eabi-gcc", "cmake", "ninja", "arm-none-eabi-readelf"):
            with self.subTest(tool=tool):
                path = self.tools / tool
                saved = path.read_text()
                path.unlink()
                try:
                    result = self.probe()
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("required cross-compile tool not found: " + tool,
                                  result.stderr)
                    self.assertFalse(self.build.exists())
                    self.assertNotIn("CROSS: all", result.stdout)
                finally:
                    path.write_text(saved)
                    path.chmod(0o755)

    def test_make_check_propagates_requested_probe_failure(self):
        make = shutil.which("make")
        if make is None:
            self.fail("required host test tool not found: make")
        self.build.mkdir()
        binary = self.build / "test_ghostwisp_boot"
        binary.write_text("#!/bin/bash\nexit 0\n")
        binary.chmod(0o755)
        self.stub("nm", "echo '00000000 T ghostwisp_fixture'")
        self.stub("strings", "echo 'controlled fixture'")
        (self.tools / "arm-none-eabi-gcc").unlink()
        result = subprocess.run(
            [make, "--no-print-directory", "-C", str(CHECK.parent),
             "-o", str(binary), "check", "BUILD_DIR=" + str(self.build),
             "TEST_BIN=" + str(binary), "PICO_SDK_PATH=" + str(self.root / "sdk")],
            env={**os.environ, "PATH": str(self.tools), "MAKEFLAGS": "", "MFLAGS": ""},
            text=True, capture_output=True, timeout=10,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("required cross-compile tool not found: arm-none-eabi-gcc",
                      result.stderr)
        self.assertNotIn("All automated checks PASSED", result.stdout)
        self.assertNotIn("Step 5", result.stdout)

    def test_controlled_success_path(self):
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("CROSS: all 2 check(s) passed", result.stdout)

    def test_configuration_failure_propagates(self):
        self.stub("cmake", "exit 9")
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cmake configure failed", result.stdout)
        self.assertNotIn("CROSS: all", result.stdout)

    def test_build_failure_propagates_even_with_stale_elf(self):
        self.build.mkdir()
        (self.build / "ghostwisp.elf").write_bytes(b"stale fixture")
        self.stub("cmake", 'if [ "$1" = "--build" ]; then exit 9; fi')
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("CROSS: all", result.stdout)

    def test_missing_output_is_not_success(self):
        self.stub("cmake", "exit 0")
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not produce ghostwisp.elf", result.stderr)

    def test_failed_elf_inspection_is_not_success(self):
        self.stub("arm-none-eabi-readelf", "exit 9")
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("CROSS: all", result.stdout)


if __name__ == "__main__":
    unittest.main()
