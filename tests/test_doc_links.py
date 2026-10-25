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

    def test_reference_definitions_check_paths_and_images(self):
        self.check_document('[guide][ref] ![art][image]\n\n'
                            '[ref]: ../README.md#intro "Guide"\n'
                            '   [image]: <a b.png> (Image title)\n', 0,
                            ('README.md', 'docs/a b.png'))

    def test_missing_reference_destinations_fail(self):
        for destination in ('absent.md', 'absent.png', '<absent file.md>'):
            with self.subTest(destination=destination):
                self.check_document(f'[ref]: {destination}\n', 1)

    def test_reference_urls_encoded_paths_and_root_paths(self):
        self.check_document('[one]: a%20b.md\n[two]: /README.md?raw=1\n'
                            '[web]: https://example.org/guide\n'
                            '[anchor]: #heading\n[mail]: mailto:a@example.org\n',
                            0, ('docs/a b.md', 'README.md'))

    def test_missing_collapsed_and_shortcut_reference_targets(self):
        self.check_document('[guide][] and [guide]\n\n[guide]: absent.md\n', 1)

    def test_undefined_references_are_not_path_destinations(self):
        self.check_document('[undefined][ref] and [plain text]\n', 0)

    def test_indented_code_is_not_a_reference_definition(self):
        self.check_document('    [example]: absent.md\n', 0)

    def test_reference_definitions_allow_parentheses_in_paths(self):
        self.check_document('[ref]: part(1).md\n', 0, ('docs/part(1).md',))

    def test_fenced_examples_are_not_live_links(self):
        for fence in ('```', '~~~', '````', '~~~~'):
            with self.subTest(fence=fence):
                self.check_document(
                    f'{fence}markdown\n[example](absent.md)\n'
                    f'[ref]: absent.png\n{fence}\n[real](../README.md)\n',
                    0, ('README.md',))

    def test_indented_fences_and_longer_closers(self):
        self.check_document('   ```markdown\n[example](absent.md)\n'
                            '  `````  \n[real](../README.md)\n', 0, ('README.md',))

    def test_short_or_wrong_marker_does_not_close_fence(self):
        self.check_document('````markdown\n```\n~~~\n[example](absent.md)\n'
                            '````\n[real](../README.md)\n', 0, ('README.md',))

    def test_closing_fence_cannot_have_info_text(self):
        self.check_document('```\n```not-a-closer\n[example](absent.md)\n```\n', 0)

    def test_unclosed_fence_extends_to_end_of_document(self):
        self.check_document('~~~markdown\n[example](absent.md)\n[ref]: absent.png\n', 0)

    def test_live_missing_links_after_fence_still_fail(self):
        for live in ('[missing](absent.md)', '[ref]: absent.md'):
            with self.subTest(live=live):
                self.check_document(f'```\n[example](not-live.md)\n```\n{live}\n', 1)

    def test_backtick_in_info_string_is_not_a_fence(self):
        self.check_document('```bad`info\n[missing](absent.md)\n', 1)

    def test_four_space_fence_is_not_a_top_level_fence(self):
        self.check_document('    ```\n[missing](absent.md)\n', 1)

    def test_balanced_parentheses_in_inline_paths(self):
        self.check_document('[guide](part(1).md) ![image](images/a(b(c)).png)', 0,
                            ('docs/part(1).md', 'docs/images/a(b(c)).png'))

    def test_missing_parenthesized_paths_fail(self):
        for link in ('[guide](absent(1).md)', '![image](a(b(c)).png)'):
            with self.subTest(link=link):
                self.check_document(link, 1)

    def test_parenthesized_path_with_title_query_and_fragment(self):
        self.check_document('[guide](part(1).md?raw=1#intro "Read (this)")', 0,
                            ('docs/part(1).md',))

    def test_escaped_parentheses_are_literal_path_characters(self):
        self.check_document(r'[one](part\(1\).md) [two](part\).md)', 0,
                            ('docs/part(1).md', 'docs/part).md'))

    def test_angle_and_reference_destinations_unescape_punctuation(self):
        self.check_document(r'[one](<part\(1\).md>)' + '\n' +
                            r'[ref]: part\(1\).md' + '\n', 0,
                            ('docs/part(1).md',))

    def test_encoded_parentheses_are_decoded_after_markdown_parsing(self):
        self.check_document('[one](part%281%29.md) [two](part%29.md)', 0,
                            ('docs/part(1).md', 'docs/part).md'))

    def test_multiple_links_are_checked_independently(self):
        self.check_document('[ok](part(1).md) [missing](absent(2).md)', 1,
                            ('docs/part(1).md',))

    def test_existing_prefix_cannot_hide_missing_full_path(self):
        self.check_document('[guide](part(1).md)', 1, ('docs/part(1',))

    def test_unbalanced_destination_is_not_a_markdown_link(self):
        self.check_document('[not a link](absent(1).md', 0)

    def test_make_docs_check_propagates_parenthesized_missing_path(self):
        with tempfile.TemporaryDirectory(prefix='ghostblade-links-make-') as directory:
            root = Path(directory)
            (root / 'tools').mkdir()
            for name in ('check_internal_links.py', 'check_links.py'):
                shutil.copy2(TOOLS / name, root / 'tools' / name)
            shutil.copy2(TOOLS.parent / 'Makefile', root / 'Makefile')
            (root / 'README.md').write_text('[guide](absent(1).md)', encoding='utf-8')
            result = subprocess.run(['make', 'docs-check'], cwd=root,
                                    capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('absent(1).md', result.stdout)

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
