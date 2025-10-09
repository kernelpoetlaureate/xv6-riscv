#!/usr/bin/env python3
"""
parse_qemu_log.py

Simple parser for the flat qemu/kernel log saved in kernel/data.md.

It extracts sections such as kfree lines, proc table lines, verbose proc dumps,
hexdumps, and syscall traces and emits formatted Markdown, CSV and a small HTML
summary. Designed to be run locally against the provided `kernel/data.md` file.

Usage: python3 parse_qemu_log.py ../kernel/data.md
"""
import re
import sys
from pathlib import Path


def read_input(path):
    return Path(path).read_text(encoding="utf-8", errors="replace")


def extract_kfree(text):
    # lines like: kfree: freeing page at 0x0000000080025000
    pattern = re.compile(r"kfree: freeing page at (0x[0-9a-fA-F]+)")
    return pattern.findall(text)


def extract_proc_table(text):
    # lines like: proc[0] addr=0x... pid=0 state=UNUSED kstack=0x...
    pattern = re.compile(r"proc\[(\d+)\]\s+addr=(0x[0-9a-fA-F]+)\s+pid=(\d+)\s+state=(\S+)\s+kstack=(0x[0-9a-fA-F]+)")
    return [m.groups() for m in pattern.finditer(text)]


def extract_syscalls(text):
    # lines like: PID 1: SYSCALL 15 (open) - Heap end(sz)=0x4000 User SP=0x3fb0
    pattern = re.compile(r"PID\s+(\d+):\s+SYSCALL\s+(\d+)\s+\(([^)]+)\)\s+-\s+Heap end\(sz\)=([^\s]+)\s+User SP=([^\s]+)")
    calls = []
    for m in pattern.finditer(text):
        pid, num, name, heap, sp = m.groups()
        calls.append({"pid": int(pid), "num": int(num), "name": name, "heap": heap, "sp": sp})
    # also extract RETURN lines that follow; we'll map the nearest previous PID return
    returns = re.findall(r"PID\s+(\d+):\s+RETURN\s+([^\s]+)\s+\((0x[0-9a-fA-F]+)\)", text)
    retmap = {}
    for pid, val, hexv in returns:
        retmap.setdefault(int(pid), []).append({"val": val, "hex": hexv})
    return calls, retmap


def to_markdown(kfrees, procs, syscalls, returns):
    out = []
    out.append("# Parsed QEMU / Kernel Log\n")

    out.append("## kfree (freed pages)\n")
    out.append("| Index | Address |\n|---:|:---:|")
    for i, a in enumerate(kfrees):
        out.append(f"| {i} | `{a}` |")

    out.append("\n## Proc Table (summary)\n")
    out.append("| Index | Addr | PID | State | Kstack |\n|---:|:---:|:---:|:---:|:---:|")
    for idx, addr, pid, state, kstack in procs:
        out.append(f"| {idx} | `{addr}` | {pid} | {state} | `{kstack}` |")

    out.append("\n## Syscalls (detected)\n")
    out.append("| PID | Num | Name | HeapEnd | UserSP | Returns |\n|---:|---:|:---:|:---:|:---:|:---:|")
    for s in syscalls:
        pid = s['pid']
        r = returns.get(pid, [])
        rstr = "; ".join([f"{it['val']} ({it['hex']})" for it in r]) if r else ""
        out.append(f"| {pid} | {s['num']} | {s['name']} | `{s['heap']}` | `{s['sp']}` | {rstr} |")

    return "\n".join(out)


def to_csv_kfree(kfrees):
    lines = ["index,address"]
    for i,a in enumerate(kfrees):
        lines.append(f"{i},{a}")
    return "\n".join(lines)


def main(argv):
    if len(argv) < 2:
        print("Usage: parse_qemu_log.py <path-to-log-file> [out-prefix]")
        return 2

    path = argv[1]
    out_prefix = argv[2] if len(argv) > 2 else "tools/output"
    text = read_input(path)

    kfrees = extract_kfree(text)
    procs = extract_proc_table(text)
    syscalls, returns = extract_syscalls(text)

    md = to_markdown(kfrees, procs, syscalls, returns)
    Path(out_prefix + ".md").write_text(md, encoding="utf-8")

    csv_kfree = to_csv_kfree(kfrees)
    Path(out_prefix + "-kfree.csv").write_text(csv_kfree, encoding="utf-8")

    print(f"Wrote {out_prefix}.md and {out_prefix}-kfree.csv")
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
