#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
# Author: jayis1
"""Regress selected handshake text checks, not DTS bindings or board wiring."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('validate_dts', ROOT / 'tools/validate_dts.py')
assert SPEC is not None and SPEC.loader is not None
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)
VALID = '''apex,int-req-gpio = <&gpio1 8 GPIO_ACTIVE_LOW>;
apex,host-rdy-gpio = <&gpio1 9 GPIO_ACTIVE_LOW>;
apex,mcu-reset-gpio = <&gpio1 10 GPIO_ACTIVE_LOW>;
'''


class BridgeGPIOTest(unittest.TestCase):
    def check_content(self, content, fails):
        with tempfile.TemporaryDirectory(prefix='ghostblade-dts-check-') as directory:
            root = Path(directory)
            path = root / 'software/dts/ghostblade-rk3576.dts'
            path.parent.mkdir(parents=True)
            path.write_text(content, encoding='utf-8')
            original = VALIDATOR.ROOT
            VALIDATOR.ROOT = directory
            VALIDATOR.ERRORS.clear()
            try:
                VALIDATOR.check_bridge_gpios()
                self.assertEqual(bool(VALIDATOR.ERRORS), fails, VALIDATOR.ERRORS)
            finally:
                VALIDATOR.ROOT = original
                VALIDATOR.ERRORS.clear()

    def test_current_assignments(self):
        self.check_content(VALID, False)

    def test_swapped_signal_assignments(self):
        self.check_content(VALID.replace('gpio1 8', 'gpio1 SWAP').replace(
            'gpio1 9', 'gpio1 8').replace('gpio1 SWAP', 'gpio1 9'), True)

    def test_each_missing_signal(self):
        for line in VALID.splitlines(keepends=True):
            with self.subTest(line=line):
                self.check_content(VALID.replace(line, ''), True)

    def test_incorrect_duplicate_not_hidden_by_correct_copy(self):
        for bad in ('<&gpio1 9 GPIO_ACTIVE_LOW>', '<&gpio2 8 GPIO_ACTIVE_LOW>',
                    '<&gpio1 8 GPIO_ACTIVE_HIGH>', '<&gpio1 8>', '<0>'):
            for content in (VALID + f'apex,int-req-gpio = {bad};\n',
                            f'apex,int-req-gpio = {bad};\n' + VALID):
                with self.subTest(content=content):
                    self.check_content(content, True)

    def test_comment_only_assignments_do_not_count(self):
        self.check_content('/*\n' + VALID + '*/\n', True)
        self.check_content(''.join('// ' + line + '\n' for line in VALID.splitlines()), True)

    def test_comments_cannot_hide_missing_property(self):
        line = VALID.splitlines(keepends=True)[0]
        self.check_content(VALID.replace(line, '/* ' + line.strip() + ' */\n'), True)

    def test_repeated_consistent_nodes_and_whitespace(self):
        self.check_content(VALID + VALID.replace(' = ', '\n=\t').replace(
            '&gpio1 8', '&gpio1 /* bank */ 8'), False)

    def test_empty_input(self):
        self.check_content('', True)

    def test_cli_and_make_reject_swapped_real_source(self):
        with tempfile.TemporaryDirectory(prefix='ghostblade-dts-cli-') as directory:
            root = Path(directory)
            for name in ('tools/validate_dts.py', 'Makefile', 'GhostBlade.mf',
                         'firmware/rp2350b/include/board_pins.h'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            shutil.copytree(ROOT / 'software/dts', root / 'software/dts')
            path = root / 'software/dts/ghostblade-rk3576.dts'
            original = path.read_text(encoding='utf-8')
            for swapped in (False, True):
                content = original
                if swapped:
                    content = content.replace('apex,int-req-gpio = <&gpio1 8',
                                              'apex,int-req-gpio = <&gpio1 9')
                    content = content.replace('apex,host-rdy-gpio = <&gpio1 9',
                                              'apex,host-rdy-gpio = <&gpio1 8')
                path.write_text(content, encoding='utf-8')
                for command in ([sys.executable, str(root / 'tools/validate_dts.py')],
                                ['make', '-C', str(root), 'validate-dts']):
                    with self.subTest(swapped=swapped, command=command):
                        result = subprocess.run(command, cwd=root.parent, capture_output=True,
                                                text=True, check=False, timeout=30)
                        self.assertEqual(result.returncode != 0, swapped,
                                         result.stdout + result.stderr)
                        if swapped:
                            self.assertIn('apex,int-req-gpio', result.stdout)


# Author: jayis1
class SPI0PinTest(unittest.TestCase):
    def run_fixture(self, content, fails, diagnostic='PIN_SPI0_'):
        with tempfile.TemporaryDirectory(prefix='ghostblade-spi-pins-') as directory:
            root = Path(directory)
            for name in ('tools/validate_dts.py', 'Makefile', 'GhostBlade.mf'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            shutil.copytree(ROOT / 'software/dts', root / 'software/dts')
            pins = root / 'firmware/rp2350b/include/board_pins.h'
            pins.parent.mkdir(parents=True)
            pins.write_text(content, encoding='utf-8')
            for command in ([sys.executable, str(root / 'tools/validate_dts.py')],
                            ['make', '-C', str(root), 'validate-dts']):
                result = subprocess.run(command, cwd=root.parent, capture_output=True,
                                        text=True, check=False, timeout=30)
                self.assertEqual(result.returncode != 0, fails,
                                 result.stdout + result.stderr)
                if fails:
                    self.assertIn(diagnostic, result.stdout)

    def setUp(self):
        self.pins = (ROOT / 'firmware/rp2350b/include/board_pins.h').read_text()
        self.lines = {name: next(line for line in self.pins.splitlines(keepends=True)
                                if line.startswith('#define ' + name + ' '))
                      for name in ('PIN_SPI0_RX', 'PIN_SPI0_CSN',
                                   'PIN_SPI0_SCK', 'PIN_SPI0_TX')}

    def test_current_source(self):
        self.run_fixture(self.pins, False)

    def test_each_pair_swap_is_rejected(self):
        import itertools
        import re
        for first, second in itertools.combinations(self.lines, 2):
            with self.subTest(first=first, second=second):
                a, b = self.lines[first], self.lines[second]
                am = re.search(r'\s(\d+)\s', a)
                bm = re.search(r'\s(\d+)\s', b)
                assert am is not None and bm is not None
                av, bv = am[1], bm[1]
                changed = self.pins.replace(a, a.replace(av, bv, 1)).replace(
                    b, b.replace(bv, av, 1))
                self.run_fixture(changed, True)

    def test_each_missing_definition_and_comment_only_definition(self):
        for name, line in self.lines.items():
            for replacement in ('', '// ' + line, '/* ' + line.strip() + ' */\n'):
                with self.subTest(name=name, replacement=replacement):
                    self.run_fixture(self.pins.replace(line, replacement), True, name)

    def test_duplicate_definitions_in_either_order(self):
        for value in ('16', '19'):
            duplicate = f'#define PIN_SPI0_RX {value}\n'
            for content in (duplicate + self.pins, self.pins + '\n' + duplicate):
                with self.subTest(value=value, content=content[:40]):
                    self.run_fixture(content, True, 'PIN_SPI0_RX')

    def test_whitespace_and_comments(self):
        changed = self.pins
        for line in self.lines.values():
            changed = changed.replace(line, line.replace('#define ',
                                      '  # /* directive */ define\t').replace(
                                      '        ', ' /* value */ ', 1))
        self.run_fixture(changed, False)

    def test_empty_header(self):
        self.run_fixture('', True)

    def test_unsupported_value_expression_fails_closed(self):
        line = self.lines['PIN_SPI0_RX']
        for value in ('(16)', '0x10', '16 + 1', 'OTHER_PIN'):
            with self.subTest(value=value):
                self.run_fixture(self.pins.replace(line, line.replace('16', value, 1)),
                                 True, 'PIN_SPI0_RX')


if __name__ == '__main__':
    unittest.main()
