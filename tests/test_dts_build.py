#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Exercise the real DTS Makefile in a disposable copy, without hardware."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "software" / "dts"


@unittest.skipUnless(
    all(shutil.which(tool) for tool in ("make", "cc", "dtc")),
    "DTS build tests require make, cc (preprocessor), and dtc",
)
class DtsBuildTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="ghostblade-dts-")
        self.addCleanup(self.scratch.cleanup)
        self.tree = Path(self.scratch.name) / "dts"
        shutil.copytree(
            SOURCE, self.tree,
            ignore=shutil.ignore_patterns("*.pp", "*.dtb", "*.dtbo", "rk3576.dtsi"),
        )
        self.overlays = sorted(
            p.stem for p in self.tree.glob("ghostblade-*.dts")
            if p.name != "ghostblade-rk3576.dts"
        )

    def make(self, target):
        result = subprocess.run(
            ["make", "--no-print-directory", target, "RK3576_DTSI=",
             "DTS_INCLUDE_PATHS=", "CPP=cc -E", "DTC=dtc"],
            cwd=self.tree, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_all_builds_every_overlay_including_audio(self):
        self.assertIn("ghostblade-audio-overlay", self.overlays)
        output = self.make("all")
        self.assertIn("base DTB skipped", output)
        for stem in self.overlays:
            with self.subTest(overlay=stem):
                artifact = self.tree / (stem + ".dtbo")
                self.assertTrue(artifact.is_file(), str(artifact))
                self.assertEqual(artifact.read_bytes()[:4], b"\xd0\x0d\xfe\xed")
        self.assertFalse((self.tree / "ghostblade-rk3576.dtb").exists())

    def test_validate_checks_every_overlay(self):
        output = self.make("validate")
        for stem in self.overlays:
            with self.subTest(overlay=stem):
                self.assertIn("Checking " + stem + ".pp...", output)

    def test_new_overlay_is_discovered_and_cleaned(self):
        extra = self.tree / "ghostblade-regression-overlay.dts"
        extra.write_text(
            '/dts-v1/;\n/plugin/;\n/ { compatible = "ghostblade,nullspectre"; };\n',
            encoding="utf-8",
        )
        self.make("all")
        self.assertTrue(extra.with_suffix(".dtbo").is_file())
        output = self.make("validate")
        self.assertIn("Checking ghostblade-regression-overlay.pp...", output)
        self.make("clean")
        for extension in ("*.dtbo", "*.dtb", "*.pp"):
            self.assertEqual(list(self.tree.glob(extension)), [])


if __name__ == "__main__":
    unittest.main()
