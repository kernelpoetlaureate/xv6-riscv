#!/usr/bin/env python3
"""
Annotate memdump output using kernel/kernel.sym symbol addresses.

Usage:
  cat memdump_output.txt | annotate_memdump.py [--sym kernel/kernel.sym]

The script understands the xv6 user `memdump` output format (the one that
prints a header like "Memory at 0x...:" and 16-byte hex rows) and will
annotate each printed 16-byte line with the owning kernel symbol (if any)
and the offset into that symbol.

This is a lightweight, offline annotator. For real-time mapping inside the
kernel you'd need to extend the kernel to expose symbol info or use a
debugger. This script makes it easier to read memdumps produced by the
existing userland `memdump` program.
"""
import sys
import argparse
import re
from bisect import bisect_right


def parse_kernel_sym(path):
    """Parse a simple kernel.sym file into a sorted list of (addr, name).
    The file format is '0xaddr name' per line (or decimal as in the xv6
    kernel.sym excerpt). We'll accept hex addresses optionally.
    """
    entries = []
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            # lines like: 0000000080000000 .text
            if len(parts) < 2:
                continue
            addr_s = parts[0]
            name = parts[1]
            try:
                addr = int(addr_s, 16) if addr_s.startswith('0x') or re.match(r'^[0-9a-fA-F]+$', addr_s) else int(addr_s)
            except Exception:
                # fallback: try decimal
                try:
                    addr = int(addr_s)
                except Exception:
                    continue
            entries.append((addr, name))
    # sort by addr
    entries.sort()
    return entries


def build_ranges(entries):
    """Convert sorted (addr,name) into parallel lists of starts and names.
    The end of a symbol is the next symbol start. The last symbol is open-ended.
    """
    starts = [a for a, _ in entries]
    names = [n for _, n in entries]
    return starts, names


def find_symbol(starts, names, addr):
    """Find symbol owning addr. Returns (name, sym_start, offset) or (None, None, None)."""
    i = bisect_right(starts, addr) - 1
    if i >= 0:
        sym_start = starts[i]
        name = names[i]
        return name, sym_start, addr - sym_start
    return None, None, None


def parse_memdump_lines(lines):
    """Parse memdump output into tuples of (base_addr, offset_in_line, bytes).
    We expect a header line like 'Memory at 0x0000...:' then rows like
    '0000: EF F0 9F ...'
    We'll yield (absolute_addr_of_row_start, bytearray_of_16) for each row.
    """
    base_addr = None
    row_re = re.compile(r'^([0-9A-Fa-f]{4}):\s*(.*)$')
    header_re = re.compile(r'Memory at\s+(0x[0-9A-Fa-f]+)')
    for line in lines:
        line = line.rstrip('\n')
        if base_addr is None:
            m = header_re.search(line)
            if m:
                base_addr = int(m.group(1), 16)
            continue
        m = row_re.match(line)
        if not m:
            continue
        off16 = int(m.group(1), 16)
        bytes_str = m.group(2).strip()
        if not bytes_str:
            b = bytearray()
        else:
            parts = bytes_str.split()
            b = bytearray(int(p, 16) for p in parts)
        yield base_addr + off16, b


def annotate(stdin, symfile):
    entries = parse_kernel_sym(symfile)
    if not entries:
        print('No symbols parsed from', symfile, file=sys.stderr)
        return 2
    starts, names = build_ranges(entries)

    lines = stdin.readlines()
    out_lines = []
    # print header lines until rows
    for l in lines:
        if l.startswith('Memory at'):
            print(l.rstrip('\n'))
            break
        else:
            print(l.rstrip('\n'))

    for addr, b in parse_memdump_lines(lines):
        # print the original hex row
        # reconstruct hex bytes as in memdump (uppercase)
        hexbytes = ' '.join(f"{byte:02X}" for byte in b)
        print(f"{addr & 0xFFFF:04X}: {hexbytes}")
        # annotate first and last byte symbol ownership
        if len(b) == 0:
            continue
        first = addr
        last = addr + len(b) - 1
        name_f, s_f, off_f = find_symbol(starts, names, first)
        name_l, s_l, off_l = find_symbol(starts, names, last)
        notes = []
        if name_f:
            notes.append(f"start -> {name_f}+0x{off_f:X}")
        else:
            notes.append("start -> <no-symbol>")
        if name_l:
            notes.append(f"end -> {name_l}+0x{off_l:X}")
        else:
            notes.append("end -> <no-symbol>")
        # if symbol spans multiple rows, we can be more detailed by scanning each byte
        if name_f == name_l and name_f is not None:
            notes.append(f"owner: {name_f} (0x{s_f:X}-0x{(s_f+len(b)-1):X})")
        else:
            # compute per-byte owner summary (compact)
            owners = {}
            for i, byte in enumerate(b):
                a = addr + i
                nm, s, off = find_symbol(starts, names, a)
                owners.setdefault(nm or '<no-symbol>', []).append(i)
            # compact owners into ranges
            pieces = []
            for nm, idxs in owners.items():
                # group contiguous indexes
                ranges = []
                start = prev = idxs[0]
                for x in idxs[1:]:
                    if x == prev + 1:
                        prev = x
                        continue
                    ranges.append((start, prev))
                    start = prev = x
                ranges.append((start, prev))
                ranges_s = ','.join(f"[{a}-{b}]" if a!=b else f"[{a}]" for a,b in ranges)
                pieces.append(f"{nm}:{ranges_s}")
            notes.append('owners: ' + '; '.join(pieces))
        print('  ' + ' | '.join(notes))

    return 0


def main():
    parser = argparse.ArgumentParser(description='Annotate memdump with kernel symbols')
    parser.add_argument('--sym', default='kernel/kernel.sym', help='path to kernel.sym')
    args = parser.parse_args()
    rc = annotate(sys.stdin, args.sym)
    sys.exit(rc)


if __name__ == '__main__':
    main()
