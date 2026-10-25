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


if __name__ == '__main__':
    unittest.main()
