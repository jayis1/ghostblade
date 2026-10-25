"""Host-only regressions for the GhostWisp header isolation gate.

Author: jayis1
Fixtures inspect source text, not preprocessed dependencies or hardware.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


CHECK = Path(__file__).resolve().parents[1] / "check.sh"


class TestHeaderGate(unittest.TestCase):
    def run_gate(self, headers=None, grep_body=None, missing_directory=False,
                 missing_grep=False):
        with tempfile.TemporaryDirectory(prefix="ghostwisp-headers-") as directory:
            root = Path(directory)
            include = root / "include with spaces"
            if not missing_directory:
                include.mkdir()
            for name, content in (headers or {}).items():
                path = include / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            tools = root / "tools"
            tools.mkdir()
            # Retain basename for the old implementation while isolating grep.
            (tools / "basename").symlink_to("/usr/bin/basename")
            if not missing_grep:
                tool = tools / "grep"
                if grep_body is None:
                    tool.symlink_to("/usr/bin/grep")
                else:
                    tool.write_text("#!/bin/sh\n" + grep_body + "\n")
                    tool.chmod(0o755)
            return subprocess.run(
                ["/bin/bash", str(CHECK), "headers", str(include)],
                env={**os.environ, "PATH": str(tools)},
                capture_output=True, text=True, timeout=10,
            )

    def assert_rejected(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("HEADERS: all", result.stdout)

    def test_clean_headers_pass(self):
        result = self.run_gate({"safe.h": '#include "ghostwisp_boot.h"\n'})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all 4 check(s) passed", result.stdout)

    def test_missing_and_empty_directories_rejected(self):
        self.assert_rejected(self.run_gate(missing_directory=True))
        self.assert_rejected(self.run_gate())
        self.assert_rejected(self.run_gate({"notes.txt": "not a header"}))

    def test_missing_grep_rejected(self):
        self.assert_rejected(self.run_gate({"safe.h": "/* safe */"},
                                           missing_grep=True))

    def test_grep_errors_rejected(self):
        for body in ("exit 2", "printf 'partial output\\n'; exit 7"):
            with self.subTest(body=body):
                result = self.run_gate({"safe.h": "/* safe */"}, grep_body=body)
                self.assert_rejected(result)
                self.assertIn("grep could not inspect", result.stderr)

    def test_forbidden_includes_rejected_with_directive_whitespace(self):
        for forbidden in ("rk3576", "blade_boot", "blade_spi", "rp2350b_memmap"):
            for directive in ("#include", "  #include", "\t# include", " #\tinclude"):
                with self.subTest(forbidden=forbidden, directive=directive):
                    self.assert_rejected(self.run_gate({
                        "unsafe.h": f'{directive} "{forbidden}.h"\n',
                    }))

    def test_nested_board_headers_inspected(self):
        self.assert_rejected(self.run_gate({
            "safe.h": "/* safe */",
            "boards/unsafe board.h": '#include "blade_boot.h"\n',
        }))
        result = self.run_gate({"boards/safe board.h": '#include "pico.h"\n'})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_case_insensitive_rule_preserved(self):
        self.assert_rejected(self.run_gate({"unsafe.h": '#include "BLADE_SPI.h"\n'}))

    def test_comments_and_nonheader_files_are_not_directives(self):
        result = self.run_gate({
            "safe.h": '// #include "blade_boot.h"\n/* rk3576 */\n',
            "notes.txt": '#include "blade_spi.h"\n',
        })
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
