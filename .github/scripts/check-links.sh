#!/bin/bash
# check-links.sh — Check internal markdown links
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 GhostBlade Project
#
# Verifies that all internal markdown links (relative paths)
# point to files that actually exist in the repository.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BROKEN=0

echo "=== Checking internal markdown links ==="

# Find all markdown files and extract relative links
while IFS= read -r mdfile; do
    # Extract markdown links: [text](url) where url doesn't start with http
    while IFS= read -r link; do
        # Strip anchor
        target="${link%%#*}"
        # Skip empty targets (same-file anchors)
        [ -z "$target" ] && continue
        # Skip external links
        [[ "$target" == http:* ]] && continue
        [[ "$target" == https:* ]] && continue
        [[ "$target" == mailto:* ]] && continue

        # Resolve relative to the markdown file's directory
        dir="$(dirname "$mdfile")"
        resolved="$dir/$target"

        # Check if file exists
        if [ ! -f "$resolved" ]; then
            echo "BROKEN: $mdfile -> $link (resolved: $resolved)"
            BROKEN=$((BROKEN + 1))
        fi
    done < <(grep -oP '\[([^\]]*)\]\(([^)]+)\)' "$mdfile" | grep -oP '(?<=\()[^)]+(?=\))' | grep -v '^http' | grep -v '^https' | grep -v '^mailto')
done < <(find "$REPO_ROOT" -name '*.md' -not -path '*/node_modules/*' -not -path '*/.git/*')

if [ "$BROKEN" -eq 0 ]; then
    echo "✓ All internal markdown links are valid"
    exit 0
else
    echo "✗ $BROKEN broken link(s) found"
    exit 1
fi