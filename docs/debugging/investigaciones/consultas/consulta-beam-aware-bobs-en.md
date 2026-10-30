# Beam-aware bitmap BOBs on OCS: correct design and debugging

## Context

Amiga A500, PAL, OCS, single 320x256 5-bitplane framebuffer (interleaved planes, 40 bytes per plane row, BPL1MOD=160). The goal is a stress demo with many bitmap BOBs moving chaotically over a flat-colour background at 50 fps, with no tearing, no trails, no partial objects, and no writes outside the BOB's intended footprint. This is a bitmap-BOB / Blitter problem, not a hardware-sprite problem.

The engine is C++23, 68000, GCC 15.1 (m68k-amiga-elf-g++), no heap/STL. The relevant worktree is `C:\Users\dvdjg\Documents\programa\AI\Amiga-Cpp-fix`, branch `fix/render-207`. The clean reference is `C:\Users\dvdjg\Documents\programa\AI\Amiga-Cpp-ref` at commit `3bc5cac7`. `master` (`C:\Users\dvdjg\Documents\programa\AI\Amiga\Amiga-Cpp`, `12e99041` plus dirty changes) contains an earlier beam-safe attempt; we do not want to copy its errors.

## Hardware/engine context

- Display: 5 planes, 320x256, PAL 50 Hz, DIWSTRT=$2C81, DIWSTOP=$2CC1, DDFSTRT=$0038, DDFSTOP=$00D0, BPL1MOD/BPL2MOD=$00A0.
- Single display buffer; no double buffer. Background is flat palette index 0 (navy); BOB art uses colour indices 1,2,4,8,16 (one bitplane per BOB).
- Each BOB is currently an 8x8 or 8x8+padding one-plane opaque bitmap. `bob_draw_plane_opaque` produces one `BlitJob` with minterm D=A, one plane, source planar 1bpp sheet, destination interleaved 5-plane bitmap, source/destination modulo computed for one plane.
- For an unaligned x, `words_per_row` is 2 (image + guard), so the actual destination footprint is 32 pixels wide and its guard word is zero. If the sheet has no padding, the old 8x8 art remains at previous positions, producing diagonal trails (looking like the scene vibrates). Adding 2 px of padding around the 8x8 art and using the real footprint fixes the trails.
- With 6 padded BOBs the demo measures 49.88 fps (1.002 fields/frame); with 8 it is 47.07 fps (1.062 fields/frame); with 12 it was ~32 fps (1.56 fields/frame).
- A static two-BOB probe (`tests/amiga/l1_backend/007_bob_plane_base`) draws one BOB at y=100 and one at y=240, checks both, checks canaries before/after the bitmap and the sheet, and reports `detail=31`; the display/base/BobTarget are correct in isolation.
- A frame-step sequence of the 6-BOB version now shows six complete objects and no trails. Before padding, the same sequence showed long diagonal lines and 1-pixel bars; those were not bitplane register corruption but old Opaque art outside the new 16/32-pixel blit window.
- Reading `BPL1..5` live with a side channel gave values that looked like corrupt base pointers, but those are Agnus's current DMA pointers at the sampled line. A CPU write watchpoint on BPL4PTH/PTH did not fire. The copperlist at COP1LC contains the correct base. Do not use live BPL readback as a base check.

## Current beam scheduler implementation

Files:

- `engine/include/eng/graphics/beam_schedule.hpp`: `BeamWriteDecision`, `beam_group_decision(line, display_start, top, bottom, words)`, `beam_retry_line`.
- `engine/include/eng/graphics/blit_job.hpp`: `BlitJob::bob`, `bob_group`, `bob_top`, `bob_bottom`; `blit_job_within`.
- `engine/include/eng/graphics/frame_plan.hpp`: `tag_bob_group(group, first_job)`, `has_grouped_bobs`.
- `engine/include/eng/graphics/bob.hpp`: `mark_bob`, `bob_draw`, `bob_draw_plane`, `bob_draw_plane_opaque`, `bob_copy_plane`.
- `engine/include/eng/platform/amiga/backend.hpp`: `FramePlanExecution`, `last_frame_plan_execution`, `set_bob_beam_wait`.
- `engine/src/platform/amiga/amiga_blitter.cpp`: `execute_frame_plan_groups`, `bob_group_span`, `wait_bob_group`, `submit_blit_job`; `execute_frame_plan` selects the grouped path.
- `demos/techniques/amiga/playfield/210_beam_aware_bobs/src/main.cpp`: 6 padded one-plane opaque BOBs, one `bob_draw_plane_opaque` per BOB, one group per BOB, flat background.
- `tests/amiga/l1_backend/007_bob_plane_base/src/main.cpp`: static two-BOB probe with canaries.

Simplified actual scheduler:

```text
for each job in plan:
  if job is not a grouped BOB: execute it
  else:
    group = aggregate consecutive jobs with same bob_group (min bob_top, max bob_bottom, total words)
    line = current_raster_line()          // VPOSR
    decision = beam_group_decision(line, DIWSTRT>>8, group.top, group.bottom, group.words)
    if decision == Now or debug_wait:
       if debug_wait and decision != Now: wait until line >= bottom+margin
       execute all jobs of the group
       wait_blitter()
    else:
       skip the whole group; keep previous art
```

`beam_group_decision` currently uses:

```cpp
top_abs = DIWSTRT_line + top
bottom_abs = min(DIWSTRT_line + bottom, 309)
blit_lines = ceil(words / kBeamWordsPerLine) + 2
if line >= bottom_abs + 4: Now
else if line + blit_lines + 4 < top_abs: Now
else if line < top_abs: Defer
else: Wait
```

The constant has been changed from 48 to 3 and 1 words/line during experiments. It is not calibrated against actual Blitter throughput; values were guesses. `execute_job_range` submits jobs with `submit_blit_job`, which waits for the previous Blitter job and writes the common registers; `wait_blitter()` is called after a group.

## Symptoms and measurements

1. Six padded one-plane opaque BOBs at 8x8 art in a 12x12 sheet: 49.88 fps, six complete objects, no trails in a six-frame sequence. This is the current stable baseline.
2. Eight BOBs: 47.07 fps, 1.062 fields/frame, visual still stable but below 50.
3. Twelve BOBs: ~32 fps; too slow.
4. Without padding, moving BOBs leave diagonal lines / 1-pixel bars because the new opaque blit does not cover the old art. This is the origin of the perceived "scene vibration". It is not a resource overlap: the static probe canaries are intact and the two far-apart BOBs render correctly.
5. Forcing `bob_beam_wait=true` with an over-conservative cost model made almost every group wait/defer and produced frames with missing BOBs, not a fix.
6. The flat background itself does not change between frames; only the BOB pixels change. The worktree/engine has no double buffering for this demo.

## Questions for Grok

Please review the current design and answer concretely, with pseudocode or exact register-level sequences where relevant. Focus on a correct, reusable engine system for bitmap BOBs, not hardware sprites.

1. What is the correct invariant for a single-buffer bitmap BOB blit to be beam-safe? Is "the beam is outside the destination box at submission time" sufficient if the Blitter can still be running while the beam reaches the top of another box? How should the scheduler reason about a blit that starts above the beam but finishes after the beam enters the box?
2. How should the Blitter cost be calibrated on OCS with 5-plane display DMA? Give a practical method using VPOSR before/after a known blit, and a conservative formula for `blit_lines` or cycles that accounts for display DMA, Blitter nasty/BLTPRI, the number of channels, and the guard word. What margins are defensible?
3. Is a per-BOB group (one opaque blit per group) the right atomic unit, or should the implementation defer the entire frame's BOB set? Compare visible latency/jitter, CPU cost, and correctness when a group is partially submitted. What does a correct `Now/Wait/Defer` policy look like for groups sorted by deadline?
4. How should the executor handle the case where a group is deferred: keep the old art, re-emit it next frame, and what state must be preserved? What diagnostics/counters should expose deferred groups and maximum lateness?
5. How should a one-plane opaque BOB be drawn so its footprint is exactly the protected box, including the guard word at unaligned x? Should the engine use a 2-word footprint, a padded sheet, a separate mask, or a different minterm/modulo? Give the exact source/destination modulo and `BLTSIZE` reasoning.
6. What is the safest way to measure/verify BPL base and copperlist correctness without being misled by live BPL DMA pointers? Recommend watchpoints, VBlank stops, COP1LC/copperlist dumps, and acceptance tests.
7. Given the measured budget, how many 8x8/12x12 5bpp bitmap BOBs can realistically run at 50 fps on a 5-plane 320x256 OCS display with one-opaque-blit BOBs? If 12 is impossible, what exact technique keeps the bitmap-BOB route (Copper-triggered blits, double buffer, dirty strips, grouped blits, reduced display DMA) and what are the trade-offs?
8. Please propose a minimal engine API and pseudocode for a reusable beam-aware BOB executor, including registration of BOB boxes/groups, per-frame planning, safe execution, deferral, telemetry, and host/on-target tests. Point out any flaws in the current `execute_frame_plan_groups` approach.

## What not to propose

- Do not propose replacing the BOBs with hardware sprites; that avoids the beam-aware bitmap problem instead of solving it.
- Do not propose a full double buffer unless you explain the memory/bus cost and why it is compatible with a 5-plane 320x256 scene on A500.
- Do not rely on reading BPL registers as stable base pointers during active display.
- Do not assume the current `kBeamWordsPerLine` values are correct; they were experimental.
