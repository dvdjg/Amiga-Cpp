# Consultation: why a simple EHB tilemap still misses exact 50 Hz PAL

## Context

Amiga A500, PAL 50 Hz, OCS/ECS. Demo `207_tiled_live_layers_animation`:
- 320x208 tile field + 48 px HUD, 6-plane EHB, corkscrew ring.
- Beginning Fields tilemap, live overlays for Flowers/Water and a fused 32x32 Campfire.
- Scroll is hardware (BPLxPT/BPLCON1); there are no per-frame scroll blits.
- The only animated object at the fire stop is the Campfire; water/flowers are static there.
- Current policy: no animation patch during scroll; at each stop only the stop's focus layer animates; patch every 5 frames; host HUD every 8 frames.

## Measured state (A500_release, 2026-10-31)

- Full tour: **49.52 fps**, 143,238 cycles/frame, 1.010 fields/frame, windows 48.35-50.32.
- Campfire stop (`DEMO_TOUR_STOP=0`): **49.97 fps**, 141,965 cycles/frame, 1.001 fields/frame, windows 49.73-50.32.
- `DEMO_NO_LIVE=1` base: 49.87 fps, 142,243 cycles/frame, 1.003 fields.
- Theoretical PAL field: 141,875 CPU cycles (7,093,790 / 50); measured "50 fps" is ~142,102 cycles/frame.

Profile at the fire stop (`DEMO_PROF=1`, 251 frames):
- scroll (scene.update + execute): 6,518 cycles/frame (max 39,722)
- patch_build: 23,837 cycles/frame, **max 238,956**
- patch_exec: 4,951 cycles/frame, max 31,422
- compose: 130 cycles/frame
- HUD: 9,009 cycles/frame, max 72,044
- leftover (VBlank wait + render + loop): 97,656 cycles/frame

Before the current focus policy the same stop was ~41.5 fps and the full tour ~39 fps, with patch_build ~69k and patch_exec ~43k average. The policy recovered ~49 fps but full-tour still misses 50 by ~1.1k cycles on average, and the max patch/HUD spikes indicate occasional multi-field work.

## Instrumentation update (after adding `prof_anim_*` counters)

The large `patch_build` peak is **not** the fire cookie-cut being split per cell. The Campfire group is already a 32x32, one-cell group. Measured at the fire stop:

- `rebuild_anim_visible`: average 17 cycles, **max 189,024** (once when entering a stop).
- candidate list construction: 13,156-17,514 cycles per patch pass.
- `patch_group` (32x32 fire): average ~11.9k for 6.8 calls/frame; `patch_exec` only ~4.4k.
- HUD: average 9,009, **max 72,926**.
- Full tour still measures 49.51 fps (1.010 fields); fire stop 49.97.

So A/B (a) "force one 32x32 job" is already true; A/B (b) pre-baking the fire frames would not attack the 189k rebuild. The remaining questions are: how to build the visible-cell table incrementally instead of a 189k rebuild at every stop, and how to keep the HUD/patched frame under one field when the rebuild coincides with the VBlank phase.

## Questions

1. Is 49.52 fps with 1.010 fields explained only by a ~1.1k-cycle overrun, or does the VBlank-synchronized loop quantize it? What exact fixed overhead should be removed to guarantee 50.00 without cutting visible animation?
2. Why does a single 32x32 Campfire produce `patch_build` spikes of ~239k cycles when only one 32x32 group (one cell?) changes every 5 ticks? Is the cost in `xlimited_playfield.hpp::patch_animations_impl` candidate building, `rebuild_anim_visible`, `MapCellSaveCache`, or `draw_group`? Which counter should be added to separate them?
3. Is the current restore+frame strategy for the fire (save-under + cookie-cut per cell) the right one on OCS? Would a precomposed 32x32 opaque/masked fire frame, a single save-under for the whole 32x32 group, or a dirty-span restore remove the 239k spike and still preserve the static bank/rock pixels?
4. What should the next A/B be to prove the remaining cost is structural: (a) fire group enlarged to one 32x32 job, (b) fire frames pre-baked at startup, (c) no HUD, (d) double buffer, (e) VBlank phase offset? Give the order and the metric that falsifies each.
5. Is 6-plane EHB at 320x208 + 48 px HUD already consuming enough display DMA that the remaining margin is effectively fixed? If so, is exact 50 Hz realistic with any per-frame CPU patch, or is the correct target a clean 49.x with no flicker and a documented 1-field budget?

## Relevant relative paths

Demo/runtime:
- `demos/techniques/amiga/playfield/207_tiled_live_layers_animation/src/main.cpp`
- `demos/techniques/amiga/playfield/207_tiled_live_layers_animation/README.md`
- `demos/techniques/amiga/playfield/207_tiled_live_layers_animation/VALIDATION.md`

Engine patch/animation:
- `engine/include/eng/field/xlimited_playfield.hpp` (`patch_animations_impl`)
- `engine/include/eng/field/xlimited_scene.hpp`
- `engine/include/eng/field/tile_animation.hpp`
- `engine/include/eng/field/tile_layer_stack.hpp`
- `engine/include/eng/field/xlimited.hpp`
- `engine/include/eng/graphics/frame_plan.hpp`
- `engine/include/eng/graphics/blit_job.hpp`
- `engine/src/platform/amiga/amiga_blitter.cpp`

Assets/generated animation data:
- `out/assets/tiled/beginning-fields-live-flowers/selected/animations.h`
- `out/assets/tiled/beginning-fields-live-flowers/selected/anim_sheet.bin`
- `out/assets/tiled/beginning-fields-live-flowers/selected/anim_masks.bin`
- `out/assets/tiled/beginning-fields-live-flowers/selected/tilebank.xlimited.bin`

Cook/measurement:
- `tools/tiled/cook-map.mjs`
- `tools/tiled/validate-cook-plan.mjs`
- `tools/debug/measure-fps.mjs`
- `tools/debug/profile.mjs`

Architecture notes:
- `docs/engine/architecture/TILED_MAP_RUNTIME.md`
- `docs/engine/architecture/TILED_COOK_PLAN.md`
- `docs/guides/optimization/OPTIMIZACION_TILED_ANIMADO.md`
- `docs/guides/optimization/PRESUPUESTO_PRIORITARIO_FRAME.md`
