#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Exercise both real link-checker entry points in disposable repositories.

Author: jayis1
"""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'


class DocLinksTest(unittest.TestCase):
    def check_document(self, content, expected, files=()):
        with tempfile.TemporaryDirectory(prefix='ghostblade-links-') as directory:
            root = Path(directory)
            (root / 'docs').mkdir()
            (root / 'docs' / 'guide.md').write_text(content, encoding='utf-8')
            for name in files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('[ignored](missing.md)' if '.git' in path.parts else '',
                                encoding='utf-8')
            for script in ('check_internal_links.py', 'check_links.py'):
                with self.subTest(script=script):
                    result = subprocess.run(
                        [sys.executable, str(TOOLS / script), '--root', str(root)],
                        cwd=root.parent, capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    if expected == 1:
                        self.assertIn('docs/guide.md:', result.stdout)

    def test_relative_paths_images_and_fragments(self):
        self.check_document('[read](../README.md#intro) ![art](image.png)', 0,
                            ('README.md', 'docs/image.png'))

    def test_missing_markdown_fails_both_entry_points(self):
        self.check_document('[missing](absent.md)', 1)

    def test_missing_non_markdown_asset_fails(self):
        self.check_document('![missing](absent.png)', 1)

    def test_urls_and_local_anchors_are_not_files(self):
        self.check_document('[web](https://example.org/a.md) [mail](mailto:a@example.org) '
                            '[ftp](ftp://example.org/a.md) [cdn](//example.org/a.png) '
                            '[heading](#intro)', 0)

    def test_titles_encoded_paths_and_angle_destinations(self):
        self.check_document('[one](a%20b.md "title") [two](<a b.md> \'title\')', 0,
                            ('docs/a b.md',))

    def test_root_relative_path_and_query(self):
        self.check_document('[root](/README.md?raw=1#intro)', 0, ('README.md',))

    def test_git_metadata_is_excluded(self):
        self.check_document('[ok](../README.md)', 0,
                            ('README.md', '.git/ignored.md'))

    def test_default_root_is_script_relative(self):
        with tempfile.TemporaryDirectory(prefix='ghostblade-links-') as directory:
            root = Path(directory) / 'checkout'
            tools = root / 'tools'
            tools.mkdir(parents=True)
            (root / 'README.md').write_text('[broken](absent.md)', encoding='utf-8')
            for name in ('check_internal_links.py', 'check_links.py'):
                shutil.copy2(TOOLS / name, tools / name)
            for name in ('check_internal_links.py', 'check_links.py'):
                with self.subTest(script=name):
                    result = subprocess.run(
                        [sys.executable, str(tools / name)], cwd=directory,
                        capture_output=True, text=True, check=False,
                    )
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertIn('README.md:', result.stdout)

    def test_missing_root_fails_instead_of_false_success(self):
        with tempfile.TemporaryDirectory(prefix='ghostblade-links-') as directory:
            result = subprocess.run(
                [sys.executable, str(TOOLS / 'check_internal_links.py'),
                 '--root', str(Path(directory) / 'absent')],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
