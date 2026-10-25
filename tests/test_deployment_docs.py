#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Check documented shell syntax and module-install guards without target access.

Author: jayis1
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def shell_blocks(name):
    content = (ROOT / 'docs' / name).read_text(encoding='utf-8')
    return re.findall(r'^```sh\n(.*?)^```', content, re.MULTILINE | re.DOTALL)


class DeploymentDocsTest(unittest.TestCase):
    def test_documented_shell_syntax(self):
        for name in ('getting-started.md', 'flashing-guide.md'):
            blocks = shell_blocks(name)
            self.assertTrue(blocks, name)
            for index, block in enumerate(blocks):
                with self.subTest(document=name, block=index):
                    result = subprocess.run(
                        ['/bin/sh', '-n'], input=block, text=True,
                        capture_output=True, check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)

    def run_install(self, *, present=True, release='target-test', probe_status=0):
        blocks = [block for block in shell_blocks('flashing-guide.md')
                  if block.startswith("ssh ghostblade-target 'set -eu\n")]
        self.assertEqual(len(blocks), 1, 'exactly one guarded module-install example')
        with tempfile.TemporaryDirectory(prefix='ghostblade-deployment-docs-') as directory:
            root = Path(directory)
            home = root / 'home'
            home.mkdir()
            if present:
                (home / 'apex_bridge.ko').write_bytes(b'test fixture, not a module')
            bin_dir = root / 'bin'
            bin_dir.mkdir()
            log = root / 'sudo.log'
            # No real SSH, sudo, module inspection, or installation is performed.
            fixtures = {
                'ssh': 'test "$1" = ghostblade-target || exit 91\n/bin/sh -c "$2"\n',
                'uname': 'printf "%s\\n" target-test\n',
                'modinfo': 'test "$PROBE_STATUS" = 0 || exit "$PROBE_STATUS"\n'
                           'printf "%s SMP test\\n" "$MODULE_RELEASE"\n',
                'sudo': 'printf "%s\\n" "$*" >> "$INSTALL_LOG"\n',
            }
            for name, body in fixtures.items():
                path = bin_dir / name
                path.write_text('#!/bin/sh\n' + body, encoding='utf-8')
                path.chmod(0o755)
            env = dict(os.environ, HOME=str(home), PATH=str(bin_dir) + os.pathsep
                       + os.environ.get('PATH', ''), INSTALL_LOG=str(log),
                       MODULE_RELEASE=release, PROBE_STATUS=str(probe_status))
            result = subprocess.run(
                ['/bin/sh', '-c', blocks[0]], env=env, cwd=root,
                text=True, capture_output=True, check=False,
            )
            calls = log.read_text(encoding='utf-8').splitlines() if log.exists() else []
            return result, calls, home

    def test_install_uses_target_release_and_staged_module(self):
        result, calls, home = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [
            'install -d /lib/modules/target-test/extra',
            f'install -m 0644 {home}/apex_bridge.ko '
            '/lib/modules/target-test/extra/apex_bridge.ko',
            'depmod -a target-test',
        ])

    def test_release_mismatch_stops_before_sudo(self):
        result, calls, _ = self.run_install(release='wrong-kernel')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_missing_module_stops_before_sudo(self):
        result, calls, _ = self.run_install(present=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_failed_module_inspection_stops_before_sudo(self):
        result, calls, _ = self.run_install(probe_status=7)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
