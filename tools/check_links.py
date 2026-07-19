#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 GhostBlade Project
"""Check markdown links in the GhostBlade project."""
import re, os, glob

broken = []
for mdfile in glob.glob('**/*.md', recursive=True):
    with open(mdfile) as f:
        content = f.read()
    dirn = os.path.dirname(mdfile)
    for m in re.finditer(r'\[([^\]]*)\]\(([^)]+\.md[^)]*)\)', content):
        link = m.group(2).split('#')[0]
        if link.startswith('http'):
            continue
        if dirn:
            target = os.path.normpath(os.path.join(dirn, link))
        else:
            target = link
        if not os.path.isfile(target):
            broken.append((mdfile, link, target))

for src, link, target in sorted(broken):
    print(f'BROKEN: {src} -> {link} (resolved: {target})')
if not broken:
    print('No broken links found')