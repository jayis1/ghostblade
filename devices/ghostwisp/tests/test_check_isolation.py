"""Regression tests for GhostWisp's binary isolation gate.

Author: jayis1
Tool fixtures exercise failure handling, not hardware or target-build validity.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


CHECK = Path(__file__).resolve().parents[1] / "check.sh"


class TestIsolationGate(unittest.TestCase):
    def run_gate(self, nm_body="printf '00000000 T ghostwisp_boot\\n'",
                 strings_body="printf 'ghostwisp\\n'", binary=True,
                 grep_body=None, missing_tool=None):
        with tempfile.TemporaryDirectory(prefix="ghostwisp-isolation-") as directory:
            root = Path(directory)
            tools = root / "tools"
            tools.mkdir()
            for name, body in (("nm", nm_body), ("strings", strings_body)):
                tool = tools / name
                tool.write_text("#!/bin/sh\n" + body + "\n")
                tool.chmod(0o755)
            if grep_body is None:
                (tools / "grep").symlink_to("/usr/bin/grep")
            else:
                grep = tools / "grep"
                grep.write_text("#!/bin/sh\n" + grep_body + "\n")
                grep.chmod(0o755)
            if missing_tool:
                (tools / missing_tool).unlink()
            if binary:
                (root / "test_ghostwisp_boot").write_bytes(b"fixture, not an ELF")
            return subprocess.run(
                ["/bin/bash", str(CHECK), "isolation", str(root)],
                env={**os.environ, "PATH": str(tools)},
                capture_output=True, text=True, timeout=10,
            )

    def test_clean_inspection_passes(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all 6 check(s) passed", result.stdout)

    def test_failed_inspectors_cannot_pass(self):
        for tool in ("nm", "strings"):
            with self.subTest(tool=tool):
                result = (self.run_gate(nm_body="exit 7") if tool == "nm"
                          else self.run_gate(strings_body="exit 7"))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(tool, result.stderr)
                self.assertNotIn("all 6 check(s) passed", result.stdout)

    def test_empty_symbol_table_cannot_establish_isolation(self):
        result = self.run_gate(nm_body="exit 0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symbol", result.stderr)

    def test_forbidden_symbols_rejected(self):
        for symbol in ("rk3576_init", "rk35_init", "blade_boot", "blade_main",
                       "rp2350b_memmap"):
            with self.subTest(symbol=symbol):
                result = self.run_gate(nm_body=f"printf '00000000 T {symbol}\\n'")
                self.assertNotEqual(result.returncode, 0)

    def test_test_symbols_remain_exempt(self):
        result = self.run_gate(nm_body="printf '00000000 t test_rk3576\\n'")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_forbidden_linker_reference_rejected(self):
        result = self.run_gate(strings_body="printf 'rp2350b_memmap.ld\\n'")
        self.assertNotEqual(result.returncode, 0)

    def test_missing_tools_rejected(self):
        for tool in ("nm", "strings", "grep"):
            with self.subTest(tool=tool):
                result = self.run_gate(missing_tool=tool)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("not found: " + tool, result.stderr)

    def test_grep_errors_rejected_at_each_stage(self):
        for arguments in ("-v", "-i rk3576", "-i rp2350b_memmap.ld"):
            with self.subTest(arguments=arguments):
                body = (f'if [ "$*" = "{arguments}" ]; then exit 2; fi\n'
                        'exec /usr/bin/grep "$@"')
                # The filter also has a pattern argument, so match its first flag.
                if arguments == "-v":
                    body = 'if [ "$1" = "-v" ]; then exit 2; fi\nexec /usr/bin/grep "$@"'
                result = self.run_gate(grep_body=body)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("grep could not", result.stderr)

    def test_partial_inspector_output_does_not_hide_failure(self):
        result = self.run_gate(nm_body="printf '00000000 T ghostwisp_boot\\n'; exit 7")
        self.assertNotEqual(result.returncode, 0)
        result = self.run_gate(strings_body="printf 'ghostwisp\\n'; exit 7")
        self.assertNotEqual(result.returncode, 0)

    def test_large_symbol_output_with_early_match_rejected(self):
        body = ("printf '00000000 T blade_boot\\n'\n"
                "i=0; while [ $i -lt 10000 ]; do "
                "printf '00000000 T ghostwisp_boot\\n'; i=$((i + 1)); done")
        result = self.run_gate(nm_body=body)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("forbidden symbol pattern 'blade_boot'", result.stderr)

    def test_missing_binary_rejected(self):
        result = self.run_gate(binary=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not found", result.stderr)
