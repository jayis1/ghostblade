"""Exercise the production GhostWisp Makefile with real host compilation.

Author: jayis1
Traces to roadmap Phase 3 reproducible firmware-toolchain instructions.
No target compilation or physical hardware is exercised.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


GHOSTWISP = Path(__file__).resolve().parents[1]


class TestHostBuild(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ghostwisp-build-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.device = self.root / "devices" / "ghostwisp"
        shutil.copytree(GHOSTWISP, self.device,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))
        self.binary = self.device / "build" / "test_ghostwisp_boot"
        self.include = self.device / "firmware" / "rp2350b" / "include"

    def make(self, *goals, success=True):
        result = subprocess.run(
            ["make", "--no-print-directory", "-C", str(self.device),
             f"REPO_ROOT={self.root}", *goals],
            capture_output=True, text=True, timeout=30,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def change_after_build(self, path, text):
        path.write_text(text)
        # Avoid filesystem timestamp-resolution sleeps. The prerequisite is
        # explicitly newer than the binary; keep both timestamps in the past.
        for source in self.device.rglob("*"):
            if source.is_file():
                os.utime(source, (1, 1))
        os.utime(self.binary, (2, 2))
        os.utime(path, (3, 3))

    def test_transitive_header_edit_rebuilds_default_goal_and_runs_new_code(self):
        nested = self.include / "host_build_probe.h"
        nested.write_text("/* initial transitive dependency */\n")
        boot_header = self.include / "ghostwisp_boot.h"
        with boot_header.open("a") as stream:
            stream.write('\n#include "host_build_probe.h"\n')
        self.make("test")
        self.change_after_build(nested,
                                '#error "transitive edit must trigger compilation"\n')
        failed = self.make(success=False)
        self.assertIn("transitive edit must trigger compilation", failed.stderr)
        nested.write_text("/* repaired transitive dependency */\n")
        rebuilt = self.make()
        self.assertIn("[CC]", rebuilt.stdout)
        self.make("test")
        unchanged = self.make()
        self.assertNotIn("[CC]", unchanged.stdout)
        self.assertTrue(self.binary.is_file())

    def test_direct_header_edit_is_rebuilt(self):
        self.make()
        header = self.include / "ghostwisp_pins.h"
        self.change_after_build(header, header.read_text() + "\n/* edited pins */\n")
        self.assertIn("[CC]", self.make().stdout)
        self.make("test")

    def test_clean_removes_dependency_file_and_default_rebuilds(self):
        self.make()
        dependency = self.binary.with_suffix(".d")
        self.assertTrue(dependency.is_file())
        self.make("clean")
        self.assertFalse(self.binary.exists())
        self.assertFalse(dependency.exists())
        self.assertIn("[CC]", self.make().stdout)
        self.make("test")


if __name__ == "__main__":
    unittest.main(verbosity=2)
