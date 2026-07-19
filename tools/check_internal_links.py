#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 GhostBlade Project
"""Check internal markdown links in the GhostBlade repository."""
import re, os, sys

root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(root_dir)

md_files = []
for root, dirs, files in os.walk('.'):
    for f in files:
        if f.endswith('.md') and '.git' not in root:
            md_files.append(os.path.join(root, f))

all_links = {}
broken = []
for md in md_files:
    with open(md) as fh:
        content = fh.read()
    links = re.findall(r'\[([^\]]*)\]\(([^)]+)\)', content)
    for text, url in links:
        if url.startswith('http') or url.startswith('#'):
            continue
        link_from = os.path.dirname(md)
        target = os.path.normpath(os.path.join(link_from, url.split('#')[0]))
        if target not in all_links:
            all_links[target] = []
        all_links[target].append((md, text))

for target, refs in sorted(all_links.items()):
    if not os.path.exists(target):
        for src, text in refs:
            broken.append((src, text, target))

if broken:
    print('BROKEN LINKS:')
    for src, text, target in broken:
        print(f'  {src}: [{text}] -> {target}')
    sys.exit(1)
else:
    print('No broken internal links found.')
    sys.exit(0)