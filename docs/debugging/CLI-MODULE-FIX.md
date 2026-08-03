# Fix: CLI Module Detection for Background Processes

## Problem

When using `Run a.exe` in AmigaOS startup-sequence, the program runs as a module loaded within a "Background CLI" process, not as a separate process named "a.exe". This caused `qOffsets` to fail finding the process and return `baseText=0`, breaking breakpoint relocation.

## Root Cause

The original `find_process_by_name()` function only searched for processes by their `ln_Name` field. But when a program is launched via `Run`, AmigaOS creates a "Background CLI" process that loads the module internally. The module name is stored in the CLI structure's `cli_CommandName` field, not in the process name.

## Solution

Added a new function `find_cli_with_module()` that:

1. Iterates through all processes
2. For each process with a CLI structure (`pr_CLI`), examines the `cli_CommandName` field
3. Matches the module name against the `debugging_trigger` pattern
4. Returns the process address and the module's `segList`

The `qOffsets` handler now:
1. First tries `find_process_by_name()` (original behavior)
2. If not found, tries `find_cli_with_module()` (new fallback)
3. Uses the returned `segList` to calculate `baseText`

## Files Modified

- `WinUAE-DBG/od-win32/barto_gdbserver.cpp`:
  - Added `find_cli_with_module()` function
  - Modified `qOffsets` handler to use CLI module search as fallback
  - Modified `monitor findproc` command to also search CLI modules
  - Enhanced process listing to show CLI command names and module addresses

## AmigaOS Structure Offsets

```
Process structure:
  +0xAC: pr_CLI (BPTR to CommandLineInterface)
  +0x80: pr_SegList (BPTR to SegList)

CLI structure:
  +0x10: cli_CommandName (BPTR to BSTR - module name)
  +0x3C: cli_Module (BPTR to SegList of loaded module)
```

## New Monitor Commands

### monitor findproc [name]

Re-scans process list and CLI modules, updates `baseText`, and relocates pending breakpoints.

Example output:
```
Found module ':a.exe' at proc=0xc0e820, segList=0xc0f6fc, baseText=0xc0f700, size=0x35e8
```

If not found, lists all processes with their CLI info:
```
Process/Module not found. Current processes:
  TaskWait: 'Initial CLI' at 0xc06730 (CLI cmd='', module=0x0)
  TaskWait: 'Background CLI' at 0xc0e820 (CLI cmd='a.exe', module=0xc0f6fc)
```

## Testing

1. Start WinUAE with `mcp-amiga-debug.uae` config
2. Wait for the calculator to load
3. Connect to GDB server
4. Call `qOffsets` - should return non-zero addresses
5. Set breakpoint at ELF address (e.g., `Z0,16f6,2`)
6. Verify relocation in log: `Z0: RELOCATED 0x16f6 -> 0xc109f6`

## Date

2026-02-22
