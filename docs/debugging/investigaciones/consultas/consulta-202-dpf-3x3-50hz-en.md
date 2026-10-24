# Consultation: why does a 3+3 DPF scroll consume ~87k CPU cycles per frame on A500?

## Context

This is an Amiga OCS/A500 engine and demo, built with a modern C++23 toolchain. The demo is `202_xlimited_dpf`: a dual-playfield (DPF) 3+3 display, 320x208 visible pixels, using the XLimited/corkscrew scroll algorithm. The intended result is a smooth 1-pixel/frame scroll of both playfields at 50 Hz PAL, with a large CPU margin. This is not a game yet; it has no audio, no sprite workload and no game logic, so the CPU cost should be well below a normal 50 Hz game.

The problem is not just “slightly below 50”. The measured long-window result is **48.37 fps** with **146663 cycles/frame**, while PAL 50 Hz has a field budget of **141876 cycles**. The frame cadence is **1.03374 fields/frame**, so some frames miss VBlank. Short windows begin around 49.3–50.6 fps, then degrade to 46–47 fps.

The important anomaly is the CPU breakdown: in the horizontal phase the demo spends about **87.4k CPU cycles** per frame while the nominal field has 141.9k. That leaves only ~55k cycles of CPU headroom before display DMA, Blitter contention and VBlank pacing are considered. A 3+3 DPF game such as Jim Power or Lionheart also runs game logic, audio and sprites at 50 Hz, so 87k for two scroll updates plus a Copper patch is implausible. Something in the current design is doing far too much per frame.

The 202 demo is intended to be the canonical 3+3 DPF example. Reducing it to 2+2 is not the desired fix. A temporary 2+2 build (`kPlanes=2`) did measure **50.00 fps stable** and 141887 cycles/frame, but that only proves that the 3+3 display/Blitter budget crosses a threshold; it is not an acceptable final result and its palette/map data were not adapted.

## Measured evidence

All measurements are `A500_release`, PAL A500, `tools/debug/measure-fps.mjs`, 12-second window, commit `2a760c23`.

```text
3+3, 320x208, kShareY=true, max_step=1:
  emulated = 48.37 fps
  cycles/frame = 146663
  fields/frame = 1.03374
  window fps = 49.67 / 49.52 / 50.58 / 49.26 / 48.19 / 45.78 / 47.29 / 46.68

Temporary 2+2 build (same demo, kPlanes=2):
  emulated = 50.00 fps
  cycles/frame = 141887
  fields/frame = 1.00008
  window fps = 50.53 / 50.58 / 49.45 / 49.73 / 49.92 / 50.58 / 49.27 / 49.92
```

The same slowdown occurs with the vertical scroll phase disabled and with narrower viewports (288 and 256 pixels), so the immediate trigger is not the vertical path or the viewport width. It is not simply a missing HUD interval either: changing the host HUD from every 8 frames to every 32 frames did not change the result.

CPU-section instrumentation, `prof_clock`, horizontal phase, maximum observed values:

```text
FG update_scroll (one field, 1 px/frame)   ~22.9k
BG update_scroll (one field, 1 px/frame)   ~29.0k
compose()                                  ~21.6k
rest (plan setup/execute/install/etc.)     ~14k
total measured CPU sections                ~87.4k
```

At 1 px/frame each field should add about one incoming 16x16 block, not tens of thousands of cycles. `compose()` is documented as a pointer/WAIT/BPLCON1 patch path costing ~2–3k, but the measured value is ~21.6k. Either the patch path is not being taken, or the supposedly cheap path has hidden work.

## Relevant code path

- `demos/techniques/amiga/playfield/202_xlimited_dpf/src/main.cpp`: scene setup, `kShareY=true`, `max_step=1`, per-frame calls to `scene.fg().update_scroll()` and `scene.bg().update_scroll()`, `execute_frame_plan()`, `scene.compose()`, and `scene.install()`.
- `engine/include/eng/field/xlimited_scene.hpp`: `begin`, `fill`, `update`, `compose`, `install`, DPF configuration, and the dual compositor wiring.
- `engine/include/eng/field/xlimited_playfield.hpp`: `update_scroll`, `add_draw`, `draw_block_job`, `map_tile_at`, `save_word`, hardware view.
- `engine/include/eng/field/scroll_engine.hpp`: `scroll_right`, `scroll_down`, `scroll_up`, `burst_right`, `burst_right_px`, the corkscrew ring geometry and saveword handling.
- `engine/include/eng/field/scroll_profile.hpp`: `ProgressiveFill`, `TileBurstFill`, `SubTileFill`, and the lookahead profiles.
- `engine/include/eng/field/xlimited_composer.hpp`: `XlimitedDualComposer::compose`, `patch`, `emit_full`, split/pointer slots, and `valid`.
- `engine/include/eng/graphics/frame_plan.hpp`: `FramePlan`, `add_tile_block_copy`, budgets and job storage.
- `engine/include/eng/api/device.hpp`: `execute_frame_plan` wrapper.
- `engine/include/eng/platform/amiga/backend.hpp`: Amiga execution of the frame plan, Blitter waits and display ownership.
- `demos/techniques/amiga/playfield/202_xlimited_dpf/VALIDATION.md`: current measured validation and design notes.
- `tools/debug/measure-fps.mjs`: measurement methodology and VBlank/field counting.
- `docs/guides/optimization/METODO_OPTIMIZACION.md`: repository performance method and bus-budget rules.
- `docs/debugging/investigaciones/consultas/consulta-scroll-optimizacion-en.md`: earlier consultation on the same scroll family; use it to avoid repeating already-discarded ideas.

The display is a 6-bitplane DPF: PF1 uses hardware planes 1/3/5 and PF2 uses 2/4/6. Both playfields are independent XLimited ring bitmaps with their own `planeaddx` and split/pointer state. `kShareY=true` means both fields share the vertical split and Y, so the comment path is a single split. The routine is still using the progressive 1-pixel fill profile, not a lookahead/pre-render profile. The demo explicitly calls both fields' `update_scroll` per frame even when one axis delta is zero.

## Working hypotheses

1. **The DPF display and Blitter budget is not the only issue; the CPU path is doing per-pixel work that a real game does once per tile or once per frame.** In particular, `scroll_right` creates an incoming-block job for each pixel of X movement, and `draw_block_job` resolves the map tile and emits a job each time. A 3+3 game normally pre-renders a strip or column and then only changes BPLCON1/BPLxPT; the CPU should not spend 22–29k per field on one pixel.
2. **`compose()` may be falling back to a full re-emission.** `patch()` returns false if split presence changed or `m_ok` is false, and `compose()` then emits and flips both Copper blocks. That would explain ~21.6k instead of 2–3k, but the exact reason is not visible from the counters. The patch path must be confirmed, not assumed.
3. **`FramePlan` validation/job expansion may be expensive per block.** `draw_block_job` computes source offsets with 32-bit division/modulo (`block / blocks_per_row_src`, `block % blocks_per_row_src`); `add_draw` resolves `map_tile_at` twice; `FramePlan::add_tile_block_copy` may validate and expand each block into multiple plane jobs. Those costs should be measured, not guessed.
4. **6-plane display DMA plus Blitter and CPU bus contention crosses the 50 Hz threshold even though the CPU-only counter looks under budget.** The 2+2 result is a strong indication that the display/Blitter bus budget is marginal in 3+3. But that does not excuse 87k CPU active; both need to be fixed. We need to know the real effective CPU budget after Agnus/Copper/Blitter steals and whether the CPU counter used by `prof_clock` includes bus-stall cycles.

## Questions for Grok

1. Is a 3+3 DPF, 320x208, 1 px/frame scroll at 50 Hz PAL feasible on a stock 68000 A500 with meaningful CPU margin? What is the realistic per-frame budget after bitplane DMA, Copper, Blitter and refresh? Please calculate the DMA/bus budget explicitly rather than only quoting 141876 CPU cycles.
2. Why would the 3+3 variant miss VBlank while the 2+2 variant is stable, when the measured CPU-section total is only ~87k in both? Is this a 6-plane display DMA/Blitter bus-contention issue, a VBlank synchronization issue, or a measurement artefact? Which experiment would falsify each explanation?
3. Review the `scroll_right`/`add_draw`/`draw_block_job` path. Why can one incoming block cost 20k+ cycles? Are the two 32-bit divisions, repeated `map_tile_at`, `FramePlan` validation, or job expansion the dominant cost? What is the correct low-CPU way to fill the incoming corkscrew column for two 3-plane fields at 1 px/frame?
4. Review `XlimitedDualComposer::compose`/`patch`. What could make the hot path cost 21.6k instead of 2–3k? Is `patch()` returning false every frame because of `m_ok`, split state or pointer-slot changes? How should we instrument it with the least perturbation, and what is the canonical patch schedule for a split whose WAIT line and BPLCON1 change every frame?
5. The demo uses `ScrollProgressive` (one `scroll_right` per pixel). Would a lookahead/strip pre-render or a tile-burst profile eliminate the per-pixel block job while preserving 1-pixel smoothness? If yes, specify the invariant for the guard band, the incoming-column ordering, and how to keep both DPF fields synchronized without tearing. If not, explain why the per-pixel fill is unavoidable.
6. What should the target CPU active time be for this demo to leave game-level headroom (audio, sprites, logic) while retaining 3+3? Give a concrete budget per subsystem (`update` per field, `compose`, Blitter launch/wait, VBlank wait) and a prioritized implementation plan with measurable checkpoints.
7. If 3+3 at 320x208 cannot meet 50 Hz with meaningful CPU margin on this design, what is the minimal, visually honest reduction that preserves the technique (for example, a specific display width/height, a lower scroll fill rate, or a different corkscrew geometry)? Please distinguish “reduce the effect” from “fix the engine path”.

## What counts as solved

- Stable 50 Hz measured over a 12-second window, no repeated 2-field frames.
- CPU active work for the two scroll updates plus compose substantially below 87k; enough headroom for a normal game workload.
- 3+3 DPF retained at 320x208, no hidden visual crop and no palette reduction.
- The fix lives in reusable engine code, not in a demo-only workaround; the 202 demo remains a readable example.
- Any hardware/DMA claim is backed by a measurement or a cited source file/line, not by a guess.
