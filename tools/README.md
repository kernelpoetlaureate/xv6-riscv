parse_qemu_log
=================

Small helper to parse flat qemu/kernel logs (like `kernel/data.md`) and emit
neater outputs (Markdown summary, CSV of freed pages).

Usage
-----

Run from the repository root (WSL on Windows):

python3 tools/parse_qemu_log.py kernel/data.md tools/output

This writes `tools/output.md` and `tools/output-kfree.csv`.

What it extracts
----------------

- kfree freed page addresses
- a compact proc table summary (index, addr, pid, state, kstack)
- syscall lines (PID, syscall number, name, heap end, user SP) and RETURNs

Notes & next steps
------------------

- The script is intentionally small and tolerant; it doesn't attempt to parse
  the full verbose dumps or raw hexdumps into structured binary fields.
- If you want HTML output, JSON, or richer tables (e.g., searching/interactive),
  I can extend the script to emit them.
