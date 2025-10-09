# Unified Capture and Parse Tool

The `capture_and_parse.py` script combines the functionality of both `capture_and_process.py` and `parse_qemu_log.py` into a single unified tool.

## Features

- **Capture Mode**: Run any command and capture its output to a timestamped log file
- **Parse Mode**: Parse existing log files to extract structured data
- **Unified Workflow**: Automatically capture and parse in one step
- **Multiple Output Formats**: Generates both Markdown and CSV outputs

## Usage

### 1. Capture and Parse xv6 Output
```bash
python3 tools/capture_and_parse.py --run-xv6
```

### 2. Capture and Parse Any Command
```bash
python3 tools/capture_and_parse.py -- <command> [args...]
```

Examples:
```bash
# Capture ls output
python3 tools/capture_and_parse.py -- ls -la

# Capture cat of kernel data
python3 tools/capture_and_parse.py -- cat kernel/data.md

# Capture make output
python3 tools/capture_and_parse.py -- make qemu
```

### 3. Parse Existing Log File Only
```bash
python3 tools/capture_and_parse.py --parse-only <log-file> [output-prefix]
```

Examples:
```bash
# Parse with default output name
python3 tools/capture_and_parse.py --parse-only tools/raw/20251009T092317Z.log

# Parse with custom output prefix
python3 tools/capture_and_parse.py --parse-only tools/raw/20251009T092317Z.log my-analysis
```

## Output Files

The script generates:

1. **Raw Log**: `tools/raw/<timestamp>.log` - Raw captured output
2. **Markdown Report**: `tools/output-<timestamp>.md` - Formatted analysis with tables
3. **CSV Data**: `tools/output-<timestamp>-kfree.csv` - Structured data for analysis

## What Gets Parsed

The parser extracts and formats:

- **Memory Operations**: `kfree` calls showing memory deallocation
- **Process Table**: State of all process slots with addresses and states  
- **System Calls**: Traced syscalls with parameters and return values
- **Boot Sequence**: Kernel initialization messages

## Options

- `--out-dir DIR`: Change output directory (default: `tools`)
- `--run-xv6`: Shortcut to run `make clean && make qemu`
- `--parse-only FILE`: Only parse an existing log file
- `--help`: Show help message

## Examples of Generated Output

### Markdown Report
The `.md` file contains formatted tables like:

```markdown
## kfree (freed pages)
| Index | Address |
|---:|:---:|
| 0 | `0x0000000080025000` |
| 1 | `0x0000000080026000` |

## Syscalls (detected)  
| PID | Num | Name | HeapEnd | UserSP | Returns |
|---:|---:|:---:|:---:|:---:|:---:|
| 1 | 15 | open | `0x4000` | `0x3fb0` | -1 (0xffffffffffffffff) |
```

### CSV Data
The `-kfree.csv` file contains structured data:

```csv
index,address
0,0x0000000080025000
1,0x0000000080026000
```

This unified tool replaces the need for separate capture and parse scripts, providing a streamlined workflow for xv6 analysis and debugging.