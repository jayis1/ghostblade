#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Exercise the real DTS Makefile in a disposable copy, without hardware."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from typing import Optional
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

    def make(self, target: Optional[str] = "all", expected=0):
        result = subprocess.run(
            ["make", "--no-print-directory"] + ([target] if target else []) +
            ["RK3576_DTSI=", "DTS_INCLUDE_PATHS=", "CPP=cc -E", "DTC=dtc"],
            cwd=self.tree, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout

    def test_default_goal_still_builds_all_with_dependency_files(self):
        self.make("all")
        artifact = self.tree / (self.overlays[-1] + ".dtbo")
        artifact.unlink()
        self.make(None)
        self.assertTrue(artifact.exists())

    def test_failed_preprocessing_is_removed_and_can_be_retried(self):
        source = self.tree / "ghostblade-regression-overlay.dts"
        source.write_text('#error intentional regression fixture\n', encoding="utf-8")
        self.make("all", expected=2)
        self.assertFalse(source.with_suffix(".pp").exists())
        source.write_text('/dts-v1/;\n/plugin/;\n/ {};\n', encoding="utf-8")
        self.make("all")
        self.assertTrue(source.with_suffix(".dtbo").exists())

    def test_included_file_change_rebuilds_overlay(self):
        # Author: jayis1. Exercise actual preprocessing and binary generation.
        include = self.tree / "regression.dtsi"
        source = self.tree / "ghostblade-regression-overlay.dts"
        wrapper = self.tree / "wrapper.dtsi"
        wrapper.write_text('#include "regression.dtsi"\n', encoding="utf-8")
        source.write_text(
            '/dts-v1/;\n/plugin/;\n#include "wrapper.dtsi"\n',
            encoding="utf-8",
        )
        include.write_text('/ { regression = <1>; };\n', encoding="utf-8")
        self.make("all")
        artifact = source.with_suffix(".dtbo")
        original = artifact.read_bytes()
        timestamp = artifact.stat().st_mtime_ns
        self.make("all")
        self.assertEqual(artifact.stat().st_mtime_ns, timestamp,
                         "unchanged inputs must not rebuild")
        time.sleep(1.1)  # Also work on filesystems with one-second timestamps.
        include.write_text('/ { regression = <2>; };\n', encoding="utf-8")
        self.make("all")
        self.assertNotEqual(artifact.read_bytes(), original,
                            "included-file edits must invalidate the binary")

    def test_clean_removes_base_outputs_without_base_include(self):
        # Artifacts may outlive the external kernel tree that produced them.
        for suffix in (".dtb", ".pp", ".pp.d"):
            (self.tree / ("ghostblade-rk3576" + suffix)).write_text(
                "stale output", encoding="utf-8")
        self.make("clean")
        for suffix in (".dtb", ".pp", ".pp.d"):
            self.assertFalse((self.tree / ("ghostblade-rk3576" + suffix)).exists())

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
