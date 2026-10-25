#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Exercise the production libapex Makefile without device access.

Author: jayis1
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LibapexBuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ghostblade-libapex-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.library = self.root / 'software/libapex'
        shutil.copytree(ROOT / 'software/libapex', self.library,
                        ignore=shutil.ignore_patterns('build', '__pycache__'))
        shutil.copy2(ROOT / 'Makefile', self.root / 'Makefile')

    def make(self, *args, success=True, top=False):
        result = subprocess.run(
            ['make', '--no-print-directory', '-C', str(self.root if top else self.library),
             *args], capture_output=True, text=True, timeout=30,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def artifacts(self):
        return {name: (self.library / 'build' / name).stat().st_mtime_ns
                for name in ('libapex.o', 'libapex.a', 'libapex.so', 'libapex.pc')}

    def test_header_change_rebuilds_object_and_libraries(self):
        self.make('all')
        before = self.artifacts()
        time.sleep(0.05)
        header = self.library / 'include/libapex.h'
        header.write_text(header.read_text() + '\n/* dependency regression */\n')
        self.make()
        after = self.artifacts()
        for name in ('libapex.o', 'libapex.a', 'libapex.so'):
            self.assertGreater(after[name], before[name], name)
        self.assertEqual(after['libapex.pc'], before['libapex.pc'])

    def test_unchanged_default_build_is_noop(self):
        self.make('-j4', 'all')
        before = self.artifacts()
        self.make('-j4')
        self.assertEqual(self.artifacts(), before)

    def test_top_level_parallel_build_is_incremental(self):
        self.make('-j4', 'libapex', top=True)
        before = self.artifacts()
        self.make('-j4', 'libapex', top=True)
        self.assertEqual(self.artifacts(), before)

    def test_transitive_header_dependency(self):
        header = self.library / 'include/libapex.h'
        header.write_text(header.read_text() + '\n#include "build_probe.h"\n')
        probe = self.library / 'include/build_probe.h'
        probe.write_text('/* first version */\n')
        self.make()
        before = self.artifacts()
        time.sleep(0.05)
        probe.write_text('/* second version */\n')
        self.make()
        self.assertGreater(self.artifacts()['libapex.o'], before['libapex.o'])

    def test_failed_compilation_recovers(self):
        self.make()
        header = self.library / 'include/libapex.h'
        original = header.read_text()
        header.write_text(original + '\n#error deliberate regression failure\n')
        self.make(success=False)
        header.write_text(original)
        self.make()
        self.assertTrue((self.library / 'build/libapex.so.0').is_file())

    def test_clean_removes_dependency_files(self):
        self.make()
        self.assertTrue((self.library / 'build/libapex.d').is_file())
        self.make('clean')
        self.assertFalse((self.library / 'build').exists())
        self.make()


if __name__ == '__main__':
    unittest.main()
