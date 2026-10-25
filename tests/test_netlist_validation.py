#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
# Author: jayis1
"""Run the production checker against synthetic name-coverage fixtures, not CAD."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("validate_netlist", ROOT / "tools/validate_netlist.py")
assert SPEC is not None and SPEC.loader is not None
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class NetlistNameTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ghostblade-net-names-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for relative in ("tools/validate_netlist.py", "Makefile"):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        self.netlist = self.root / "hardware/kicad/ghostblade.net"
        self.netlist.parent.mkdir(parents=True)
        self.manifest = self.root / "GhostBlade.mf"
        self.manifest.write_text('NET_SPI_CLK = "synthetic endpoint"\n')
        self.netlist.write_text('(net (code "1") (name "NET_SPI_CLK"))\n')
        # Minimal synthetic assignment inputs, not a compilable board design.
        self.dts = self.root / "software/dts/ghostblade-rk3576.dts"
        self.dts.parent.mkdir(parents=True)
        self.dts.write_text(
            'apex,int-req-gpio = <&gpio1 8 0>;\n'
            'apex,host-rdy-gpio = <&gpio1 9 0>;\n'
            'apex,mcu-reset-gpio = <&gpio1 10 0>;\n')
        self.pins = self.root / "firmware/rp2350b/include/board_pins.h"
        self.pins.parent.mkdir(parents=True)
        self.pins.write_text(
            '#define PIN_SPI0_RX 16\n#define PIN_SPI0_CSN 17\n'
            '#define PIN_SPI0_SCK 18\n#define PIN_SPI0_TX 19\n'
            '#define PIN_INT_REQ 20\n#define PIN_HOST_RDY 21\n'
            '#define PIN_MCU_RUN 24\n')

    def run_checker(self, *args):
        return subprocess.run([sys.executable, str(self.root / "tools/validate_netlist.py"), *args],
                              cwd=self.root.parent, capture_output=True, text=True, timeout=10)

    def test_exact_match_and_extra_nets_pass_strict(self):
        with self.netlist.open("a") as stream:
            stream.write('(net (code "2") (name "GND"))\n')
        result = self.run_checker("--strict-net-names")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("coverage: 1/1", result.stdout)
        self.assertNotIn("Manifest net absent", result.stdout)

    def test_same_prefix_is_not_a_match(self):
        self.netlist.write_text('(net (code "1") (name "NET_SPI_MOSI"))\n')
        result = self.run_checker("--strict-net-names")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Manifest net absent by exact name from KiCad netlist: NET_SPI_CLK", result.stdout)
        self.assertIn("coverage: 0/1", result.stdout)

    def test_existing_component_error_fails_even_with_exact_names(self):
        with self.netlist.open("a") as stream:
            stream.write('(comp (ref "U1") (value "synthetic wrong component"))\n')
        for args in ((), ("--strict-net-names",)):
            with self.subTest(args=args):
                result = self.run_checker(*args)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("coverage: 1/1", result.stdout)
                self.assertIn("U1: expected 'RK3576', got 'synthetic wrong component'", result.stdout)

    def test_case_difference_is_not_a_match(self):
        self.netlist.write_text('(net (code "1") (name "net_spi_clk"))\n')
        result = self.run_checker("--strict-net-names")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Manifest net absent by exact name from KiCad netlist: NET_SPI_CLK", result.stdout)

    def test_default_warns_without_claiming_all_passed(self):
        self.manifest.write_text('NET_HOST_READY = "synthetic endpoint"\n')
        self.netlist.write_text('(net (code "1") (name "NET_HOST_RDY"))\n')
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("WARNINGS", result.stdout)
        self.assertIn("Manifest net absent by exact name from KiCad netlist: NET_HOST_READY", result.stdout)
        self.assertNotIn("All netlist cross-reference checks passed", result.stdout)

    def test_empty_or_unrecognized_inputs_fail_in_both_modes(self):
        for path in (self.manifest, self.netlist):
            original = path.read_text()
            for text in ("", "unrecognized synthetic content\n"):
                path.write_text(text)
                for args in ((), ("--strict-net-names",)):
                    with self.subTest(path=path.name, text=text, args=args):
                        result = self.run_checker(*args)
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertIn("coverage cannot be checked", result.stdout)
            path.write_text(original)

    def test_missing_inputs_fail_in_both_modes(self):
        for path in (self.manifest, self.netlist):
            original = path.read_text()
            path.unlink()
            for args in ((), ("--strict-net-names",)):
                with self.subTest(path=path.name, args=args):
                    result = self.run_checker(*args)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn("not found", result.stdout)
            path.write_text(original)

    def test_missing_assignment_inputs_fail_in_both_modes(self):
        for path in (self.dts, self.pins):
            original = path.read_text()
            path.unlink()
            for args in ((), ("--strict-net-names",)):
                with self.subTest(path=path.name, args=args):
                    result = self.run_checker(*args)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn(f"not found: {path}", result.stdout)
                    self.assertNotIn("All netlist cross-reference checks passed", result.stdout)
            path.write_text(original)

    def test_empty_assignment_inputs_fail_in_both_modes(self):
        for path in (self.dts, self.pins):
            original = path.read_text()
            path.write_text("")
            for args in ((), ("--strict-net-names",)):
                with self.subTest(path=path.name, args=args):
                    result = self.run_checker(*args)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn("ERRORS", result.stdout)
            path.write_text(original)

    @unittest.skipUnless(shutil.which("make"), "GNU Make unavailable")
    def test_make_targets_reject_missing_assignment_inputs(self):
        for path in (self.dts, self.pins):
            original = path.read_text()
            path.unlink()
            for target in ("validate-netlist", "validate-netlist-strict"):
                with self.subTest(path=path.name, target=target):
                    result = subprocess.run(["make", target], cwd=self.root,
                                            capture_output=True, text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(f"not found: {path}", result.stdout)
            path.write_text(original)

    def test_missing_names_are_sorted_and_not_aliased(self):
        self.manifest.write_text('NET_Z = "fixture"\nNET_A = "fixture"\n')
        result = self.run_checker("--strict-net-names")
        self.assertEqual(result.returncode, 1)
        messages = [line for line in result.stdout.splitlines() if "Manifest net absent" in line]
        self.assertEqual([line.rsplit(": ", 1)[1] for line in messages], ["NET_A", "NET_Z"])

    def test_repeated_main_does_not_retain_diagnostics(self):
        from contextlib import redirect_stdout
        import io
        from unittest.mock import patch
        with patch.object(checker, "ROOT", str(self.root)), redirect_stdout(io.StringIO()):
            self.manifest.write_text('NET_OTHER = "fixture"\n')
            self.assertEqual(checker.main(["--strict-net-names"]), 1)
            self.manifest.write_text('NET_SPI_CLK = "fixture"\n')
            self.assertEqual(checker.main(["--strict-net-names"]), 0)
            self.assertEqual(checker.ERRORS, [])

    @unittest.skipUnless(shutil.which("make"), "GNU Make unavailable")
    def test_strict_make_target_propagates_failure(self):
        self.manifest.write_text('NET_OTHER = "fixture"\n')
        result = subprocess.run(["make", "validate-netlist-strict"], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Manifest net absent by exact name", result.stdout)
        self.manifest.write_text('NET_SPI_CLK = "fixture"\n')
        result = subprocess.run(["make", "validate-netlist-strict"], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
