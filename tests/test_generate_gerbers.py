#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
# Author: jayis1
"""Export failure-handling regressions; fixtures do not validate KiCad output."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("generate_gerbers", ROOT / "tools/generate_gerbers.py")
assert SPEC is not None and SPEC.loader is not None
exporter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(exporter)


class GerberExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ghostblade-export-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.pcb = self.root / "fixture board.kicad_pcb"
        # Only a path fixture: subprocess is mocked, never parsed as a real PCB.
        self.pcb.write_text("fixture, not a KiCad board\n")
        self.output = self.root / "export output"
        self.args = ["--kicad-pcb", str(self.pcb), "--output", str(self.output)]
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        self.enterContext(contextlib.redirect_stderr(io.StringIO()))

    def test_missing_board_prevents_notes_and_archive(self):
        self.pcb.unlink()
        with patch.object(exporter.subprocess, "run") as run:
            self.assertEqual(exporter.main(self.args + ["--fab-note", "--zip"]), 1)
        run.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_directory_is_not_a_board(self):
        self.pcb.unlink()
        self.pcb.mkdir()
        self.assertEqual(exporter.main(self.args), 1)
        self.assertFalse(self.output.exists())

    def test_each_export_failure_prevents_packaging(self):
        stages = len(exporter.GERBER_LAYERS) + 2  # layers, drill, position
        for stage in range(stages):
            for failure in (subprocess.CompletedProcess([], 7, "", "fixture failure"),
                            FileNotFoundError("fixture missing executable"),
                            subprocess.TimeoutExpired("fixture", 60)):
                with self.subTest(stage=stage, failure=type(failure).__name__):
                    results = [subprocess.CompletedProcess([], 0, "", "")] * stage + [failure]
                    with patch.object(exporter.subprocess, "run", side_effect=results) as run:
                        self.assertEqual(exporter.main(self.args + ["--fab-note", "--zip"]), 1)
                    self.assertEqual(run.call_count, stage + 1)
                    self.assertFalse((self.output / "ghostblade-fab-notes.json").exists())
                    self.assertFalse((self.output / "ghostblade-gerbers.zip").exists())

    def test_successful_command_sequence(self):
        with patch.object(exporter.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as run:
            self.assertEqual(exporter.main(self.args), 0)
        self.assertEqual(run.call_count, len(exporter.GERBER_LAYERS) + 2)
        self.assertEqual(run.call_args.args[0][-1], str(self.pcb))
        self.assertFalse((self.output / "ghostblade-fab-notes.json").exists())

    def test_successful_export_packages_notes_and_fixture_outputs(self):
        def fixture_export(command, **kwargs):
            # Synthetic export artifacts test orchestration, not KiCad geometry.
            kind = command[3]
            suffix = {"gerbers": ".gbr", "drill": ".drl", "pos": ".pos"}[kind]
            name = command[command.index("--layers") + 1] if kind == "gerbers" else kind
            (self.output / (name + suffix)).write_text("synthetic export fixture")
            return subprocess.CompletedProcess(command, 0, "", "")

        with patch.object(exporter.subprocess, "run", side_effect=fixture_export):
            self.assertEqual(exporter.main(self.args + ["--fab-note", "--zip"]), 0)
        expected = {layer + ".gbr" for layer in exporter.GERBER_LAYERS}
        expected.update({"drill.drl", "pos.pos", "ghostblade-fab-notes.json"})
        with zipfile.ZipFile(self.output / "ghostblade-gerbers.zip") as archive:
            self.assertEqual(set(archive.namelist()), expected)
            self.assertEqual(len(archive.namelist()), len(expected))
            notes = json.loads(archive.read("ghostblade-fab-notes.json"))
            self.assertEqual(notes["author"], "jayis1")
            self.assertIn("draft", notes["status"])
            self.assertIsNone(archive.testzip())

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        stale = self.output / "old.gbr"
        stale.write_text("existing data")
        with patch.object(exporter.subprocess, "run") as run:
            self.assertEqual(exporter.main(self.args + ["--zip"]), 1)
        run.assert_not_called()
        self.assertEqual(stale.read_text(), "existing data")
        self.assertFalse((self.output / "ghostblade-gerbers.zip").exists())

    def test_output_file_is_rejected(self):
        self.output.write_text("preserve")
        self.assertEqual(exporter.main(self.args), 1)
        self.assertEqual(self.output.read_text(), "preserve")

    def test_notes_only_needs_no_board_or_tool(self):
        self.pcb.unlink()
        with patch.object(exporter.subprocess, "run") as run:
            self.assertEqual(exporter.main(self.args + ["--notes-only"]), 0)
        run.assert_not_called()
        self.assertTrue((self.output / "ghostblade-fab-notes.json").is_file())
        notes = json.loads((self.output / "ghostblade-fab-notes.json").read_text())
        self.assertEqual(notes["author"], "jayis1")
        self.assertIn("draft", notes["status"])
        self.assertFalse((self.output / "ghostblade-gerbers.zip").exists())

    def test_notes_only_cannot_package(self):
        with self.assertRaises(SystemExit) as error:
            exporter.main(self.args + ["--notes-only", "--zip"])
        self.assertEqual(error.exception.code, 2)
        self.assertFalse(self.output.exists())

    def test_archive_contains_each_file_once(self):
        self.output.mkdir()
        for name in ("b.gbr", "a.gbr", "board.drl", "board.pos", "notes.json"):
            (self.output / name).write_text("test fixture")
        exporter.generate_zip(str(self.output))
        with zipfile.ZipFile(self.output / "ghostblade-gerbers.zip") as archive:
            self.assertEqual(archive.namelist(), ["a.gbr", "b.gbr", "board.drl", "board.pos", "notes.json"])
            self.assertIsNone(archive.testzip())


if __name__ == "__main__":
    unittest.main()
