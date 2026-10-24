#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Check inline Markdown link/image paths; not anchors or reference-style links.

Author: jayis1
"""

import argparse
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parent.parent
# Deliberately limited to inline destinations without nested parentheses.
INLINE_LINK = re.compile(
    r'\[[^\]]*\]\(\s*(?:<([^>\n]*)>|([^\s)]+))'
    r'(?:\s+(?:"[^"\n]*"|\'[^\'\n]*\'))?\s*\)'
)


def check_links(root):
    """Return (source, destination, resolved target) tuples for missing paths."""
    broken = []
    for source in sorted(root.rglob('*.md')):
        if '.git' in source.relative_to(root).parts:
            continue
        content = source.read_text(encoding='utf-8')
        for match in INLINE_LINK.finditer(content):
            destination = match.group(1) or match.group(2) or ''
            try:
                url = urlsplit(destination)
            except ValueError:
                broken.append((source, destination, 'invalid URL'))
                continue
            if url.scheme or url.netloc or not url.path:
                continue
            # GitHub repository-root links start at the selected repository root.
            path = unquote(url.path)
            target = root / path.lstrip('/') if path.startswith('/') else source.parent / path
            if not target.exists():
                broken.append((source, destination, target))
    return broken


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT,
                        help='repository root (default: parent of tools/)')
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if not root.is_dir():
        parser.error(f'not a directory: {root}')
    broken = check_links(root)
    if broken:
        print('BROKEN LINKS:')
        for source, destination, target in broken:
            print(f'  {source.relative_to(root)}: {destination} -> {target}')
        return 1
    print('No broken inline internal link paths found.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
