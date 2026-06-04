# AGENTS

## What This Repo Is
- This repo keeps a legacy root Amiga C workspace (`Makefile`, `out/a.exe`) and a newer C++23 engine demo flow under `demos/` + `tools/`; do not mix them accidentally.
- For engine work, use the PowerShell scripts in `tools/` instead of invoking the root `Makefile` directly.

## Required Local Tooling
- Windows + PowerShell + Node.js are required for the scripted demo runner flow (`tools/run/run-demo.ps1` -> `tools/run/run-demo.mjs`).
- The Amiga toolchain is resolved in this order: `AMIGA_BIN_PATH`, then Cursor extension path, then VS Code extension path for `bartmanabyss.amiga-debug-1.8.2`.
- `tools/run/run-demo.mjs` imports `../../../mcp-winuae-emu/dist/winuae-connection.js` (sibling repo output); if that path is missing, run scripts fail before launching WinUAE.

## Canonical Commands
- Build one demo: `powershell -ExecutionPolicy Bypass -File .\tools\build\build-demo.ps1 demos\000_toolchain_cpp23 -DebugBuild -Clean`
- Run one demo and capture: `powershell -ExecutionPolicy Bypass -File .\tools\run\run-demo.ps1 demos\000_toolchain_cpp23`
- Analyze one demo: `powershell -ExecutionPolicy Bypass -File .\tools\analyze\analyze-demo.ps1 demos\000_toolchain_cpp23`
- Full regression: `powershell -ExecutionPolicy Bypass -File .\tools\test-regression.ps1`
- Single-demo regression loop: `powershell -ExecutionPolicy Bypass -File .\tools\test-regression.ps1 -Demo demos\101_ehb_tile_scroll_driver -Warp`

## Verification Order (Do Not Skip)
- Default order is `build -> run -> analyze`; `analyze-demo.ps1` expects existing `.exe/.elf/.map` and optionally validates `out/run/<demo>/screenshot.png`.
- `tools/test-regression.ps1` already enforces this order per demo and also runs `analyze-sequence.ps1` automatically when present.
- Regression defaults to debug-style builds (`-DebugBuild` internally). Use `-ReleaseBuild` only when you explicitly need release optimization behavior.

## Runner/Emulator Behavior You Must Know
- `run-demo` sets WinUAE `warp=false` by default. Use `-Warp` only for throughput/quick loops, not visual smoothness judgments.
- Modern demos should expose `g_amg_run_status`; runner waits for side-channel `READY` on `127.0.0.1:2346` and fails fast if not reached.
- Timeout fallback is opt-in (`-AllowTimeoutFallback` / `--allow-timeout-fallback`), intended for diagnostics.
- Sequence captures are cleaned before each run (`out/run/<demo>/sequence` is deleted/recreated) to avoid mixing old frames.

## High-Value Paths
- Engine entry loop: `engine/include/amg/engine.hpp` (`update -> wait_vblank -> render`; render is commit point).
- Amiga backend implementation: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Demo-specific strong temporal validation example: `demos/101_ehb_tile_scroll_driver/analyze-sequence.ps1`.
- Build/run docs and operational details: `docs/BUILD_AND_RUN.md`.

## Code/Design Constraints To Preserve
- Engine dialect/constraints are strict and intentional: `gnu++23`, no exceptions, no RTTI, no gameplay-time dynamic allocation (`docs/CODING_STYLE.md`).
- Keep game logic backend-agnostic; Amiga-specific register/DMA details belong in backend/driver layers, not high-level game logic.
