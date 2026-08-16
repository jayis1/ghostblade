#!/usr/bin/env python3
"""Update stats.json with current line counts for the GhostBlade project."""
import json, os, sys

def count_file(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as fh:
            return sum(1 for _ in fh)
    except Exception:
        return 0

def main():
    counts = {'C': 0, 'headers': 0, 'dts': 0, 'docs': 0, 'python': 0,
              'shell': 0, 'makefiles': 0, 'cmake': 0, 'linker': 0}
    total_lines = 0
    total_files = 0

    for root, dirs, files in os.walk('.'):
        if '.git' in root:
            continue
        for f in files:
            path = os.path.join(root, f)
            if os.path.islink(path):
                continue
            key = None
            if f.endswith('.c'):
                key = 'C'
            elif f.endswith('.h'):
                key = 'headers'
            elif f.endswith('.dts'):
                key = 'dts'
            elif f.endswith('.md'):
                key = 'docs'
            elif f.endswith('.py'):
                key = 'python'
            elif f.endswith('.sh'):
                key = 'shell'
            elif f == 'Makefile' or f.endswith('.mk'):
                key = 'makefiles'
            elif f.endswith('.cmake'):
                key = 'cmake'
            elif f.endswith('.ld'):
                key = 'linker'
            if key:
                total_files += 1
                n = count_file(path)
                counts[key] += n
                total_lines += n

    stats = {**counts, 'bom_components': 67, 'total_files': total_files,
             'total_lines': total_lines, 'last_updated': '2026-08-16'}
    with open('stats.json', 'w') as fh:
        json.dump(stats, fh, indent=2)
        fh.write('\n')
    print(json.dumps(stats, indent=2))

if __name__ == '__main__':
    main()