#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 jayis1
"""Check inline Markdown paths and reference-definition destinations; not anchors.

Author: jayis1
"""

import argparse
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parent.parent
INLINE_START = re.compile(r'\[[^\]\n]*\]\([ \t]*')
INLINE_END = re.compile(
    r'(?:[ \t]+(?:"[^"\n]*"|\'[^\'\n]*\'))?[ \t]*\)'
)
# Markdown backslash escapes apply only to ASCII punctuation, before URL decoding.
MARKDOWN_ESCAPE = re.compile(r'\\([!"#$%&\'()*+,\-./:;<=>?@\[\]\\^_`{|}~])')


def inline_destinations(content):
    """Yield inline destinations without truncating balanced/escaped parentheses.

    This is a path-checking subset, not a complete Markdown syntax validator.
    Malformed/unclosed destinations are ignored rather than checked as prefixes.
    """
    for match in INLINE_START.finditer(content):
        start = position = match.end()
        if position < len(content) and content[position] == '<':
            end = content.find('>', position + 1)
            if (end != -1 and '\n' not in content[position:end]
                    and INLINE_END.match(content, end + 1)):
                yield content[position + 1:end]
            continue
        depth = 0
        while position < len(content):
            char = content[position]
            if (char == '\\' and position + 1 < len(content)
                    and MARKDOWN_ESCAPE.match(content, position)):
                position += 2
                continue
            if char.isspace():
                break
            if char == '(':
                depth += 1
            elif char == ')':
                if depth == 0:
                    break
                depth -= 1
            position += 1
        if (position > start and depth == 0
                and INLINE_END.match(content, position)):
            yield content[start:position]


# Same-line definitions with at most three leading spaces. Check every
# destination, including unused definitions; resolving reference uses is outside
# this path check's scope. Multiline definitions are deliberately not parsed.
REFERENCE_DEFINITION = re.compile(
    r'^ {0,3}\[[^\]\n]+\]:[ \t]*(?:<([^>\n]+)>|([^\s<>]+))'
    r'(?:[ \t]+(?:"[^"\n]*"|\'[^\'\n]*\'|\([^\n]*\)))?[ \t]*$',
    re.MULTILINE,
)


def without_fenced_code(content):
    """Exclude top-level backtick/tilde fences, preserving line boundaries.

    This is intentionally not a full Markdown parser: container fences inside
    blockquotes/lists and inline/indented code are outside this filter's scope.
    """
    lines = []
    fence = None
    for line in content.splitlines(keepends=True):
        candidate = re.match(r'^ {0,3}(`{3,}|~{3,})([^\r\n]*)', line)
        if fence is not None:
            if candidate:
                marker, suffix = candidate.groups()
                if (marker[0] == fence[0] and len(marker) >= len(fence)
                        and not suffix.strip(' \t')):
                    fence = None
            lines.append('\n')
        elif candidate and not (candidate[1][0] == '`' and '`' in candidate[2]):
            fence = candidate[1]
            lines.append('\n')
        else:
            lines.append(line)
    return ''.join(lines)


def check_links(root):
    """Return (source, destination, resolved target) tuples for missing paths."""
    broken = []
    for source in sorted(root.rglob('*.md')):
        if '.git' in source.relative_to(root).parts:
            continue
        content = without_fenced_code(source.read_text(encoding='utf-8'))
        destinations = list(inline_destinations(content))
        destinations.extend(match.group(1) or match.group(2) or ''
                            for match in REFERENCE_DEFINITION.finditer(content))
        for destination in destinations:
            destination = MARKDOWN_ESCAPE.sub(r'\1', destination)
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
    print('No broken internal link paths found (inline and reference definitions).')
    return 0


if __name__ == '__main__':
    sys.exit(main())
