#!/usr/bin/env python3
import sys
import re

if len(sys.argv) < 2:
    print('Usage: parse_dump_gaps.py <dump_pages.txt>')
    sys.exit(1)

path = sys.argv[1]
pat = re.compile(r'pa=0x([0-9a-fA-F]+)')
addrs = []
with open(path, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        m = pat.search(line)
        if m:
            addrs.append(int(m.group(1), 16))

if not addrs:
    print('No physical addresses found in file')
    sys.exit(0)

addrs = sorted(set(addrs))
PAGE = 0x1000
missing_total = 0
gaps = []
for a, b in zip(addrs, addrs[1:]):
    diff = b - a
    if diff == PAGE:
        continue
    elif diff < PAGE:
        # unexpected: overlapping or duplicate addresses
        print(f'Warning: non-increasing or overlapping addresses: {hex(a)} -> {hex(b)} (diff {diff})')
    else:
        missing = diff // PAGE - 1
        missing_total += missing
        gaps.append((a, b, missing))

if not gaps:
    print('No gaps found: all consecutive pages differ by exactly 0x1000')
else:
    print(f'Found {len(gaps)} gap(s), total missing pages: {missing_total}\n')
    for a, b, missing in gaps:
        start_missing = a + PAGE
        end_missing = b - PAGE
        print(f'Gap: {hex(start_missing)} - {hex(end_missing)}  missing pages: {missing}  (jump {hex(b - a)})')

# Summary
print('\nSummary:')
print(f'total addresses parsed: {len(addrs)}')
print(f'total gaps: {len(gaps)}')
print(f'total missing pages: {missing_total}')
