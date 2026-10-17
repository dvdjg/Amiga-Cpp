# Follow-up consultation (EN): flatshade-convex port at 2.31 fields — your 5 proposals triaged, new hard data, and one engine idea we have not tried

We ran your first-round proposals against cycle-exact measurements (WinUAE-DBG, stock A500, BLTPRI on, triple buffer, vblank-locked). Recap of the target: original reference measured at exactly **2.00 fields/frame (25 fps)**; our high-level-API port runs at **2.31 fields (21.6 fps)**; everything must stay generic engine code (no demo tricks; reference itself is pure C, no asm — we verified `lib/lib3d/` is all `.c`).

## Results of your first-round proposals (all measured)

1. **Clear-first reorder (clear → transform → lines → fill → commit): REFUTED twice.** With BLTPRI restored: **21.02 vs 21.73** for our own order (transform under the fill; commit+clear after it, render+vblank covering the tail). Cause: our clear is ~65k < transform ~90k, so the transform overruns the clear and, worse, the fill ends with zero CPU to hide under. Four orders measured in total; ours wins all four.
2. **`BLTDPT` hoist into the line prelude (write once, save one custom write per line-plane): APPLIED then REVERTED — it broke rendering.** Result: full-width horizontal streaks at specific rotation phases (odd contour crossings). Root cause (new hardware fact): **in LINE mode the blitter mutates the D pointer during the blit** (each line's first pixel goes via D), so rewriting `BLTDPT` per line was NOT redundant; hoisting it sends the next line's first pixel to variable addresses → parity break. Caught only by an 8-frame sequence; single-angle freeze-diff and 2–3 frame vision missed it. Net: no gain available there.
   Corollary for your #2: batch-wide removal of the per-line `_WaitBlitter` stays refuted by the emulator source (LINE mode reads `bltapt/bltcon*/mods` live, `blitter.cpp:731-745`).
3. **Contiguous bbox fill: implemented (corrected formula) as a new engine primitive** `blitter_area_fill(..., bbox)`: seed at row maxY+2 of the LAST plane, sweep ends after row minY−2 of plane 0, ONE contiguous blit, general for N planes. Pixel-exact vs the full sweep (frozen A/B: 0 diffs outside the HUD text). Result: **≈0 fps** in this pipeline — the saved bus cycles fall into the fill's already-absorbed tail. Kept for lower bus cost.
4. **Field-accurate scheduler: rejected.** The 70k "sleep" is the already-complete frame being displayed while the loop waits for the next vblank boundary (pacing), not lost work; it disappears automatically iff real work ≤ 2 fields.
5. **lib3d engine asm: on hold** — the reference has none; it is our last-resort option (user decision), worth ~19k (5.8%).

## New hard data to reason with

- Frame = 328k cycles. Blitter serial work = **262k** (lines 66k + fill 131k + clear 65k) — fits 2.0 fields (284k) on its own! CPU path nominal ≈ 288k (wait-in-start 77k where the blitter is working on fill/clear; draw-program 80k; transform 90k; commit+clear-launch 11k; render ~30k).
- **The binding constraint is the coupling**: when the CPU overlaps a nasty blit, its wall time stretches. With BLTPRI globally on (like the reference), the transform that "costs" 90k standalone takes substantially longer under the 131k fill. Selective nasty (off during the lines phase, on for fill/clear) measured **21.63 vs 21.72 → wash** (the per-line wait re-arms nasty during its poll anyway).
- Ablations: floor (no edges/fill/clear) = 142,102 cycles = exactly 1.00 field; fill exposed ~66k; edges ~112k; clear ~100k.
- DMA per field (CPU/BLT/BPL accesses) is at parity with the reference (62.7k vs 61.0k); the reference just completes in 2.00 fields and we need 2.31.

## The engine idea we have NOT tried (and the main question)

Our engine has a **background task queue that is pumped during blitter waits** (`wait_blitter()` runs registered slices; it is exactly the machinery to do useful work in the dead time). Today this demo disables it (`set_blit_service_enabled(false)`) because the generically-sliced queue added overhead for tiny tasks. But the numbers say we have ~57 line-blit waits (~300–900 cycles each) + the fill/clear wait tails, i.e. tens of thousands of cycles per frame of CPU idle while the blitter works, and the transform (90k standalone) is pure register math that could be *chunked* (per vertex-group, state in memory).

**Questions:**

1. Is chunked/cooperative interleaving worth pursuing as a generic engine primitive — i.e., splitting the transform (and other per-frame CPU work) into bounded slices executed inside `wait_blitter` polls — or does the per-slice state spill/overhead make it a wash on a 68000 (no cache, 16-bit ALU)? If you think it can work, what chunk size and state layout would you use (vertices per slice vs table walk vs fixed 4-group batches)? Any known demoscene precedent for "transform inside blit waits"?
2. Given the measured coupling (nasty stretch) and that the reference achieves 2.00 with the same primitive sequence, is there any *other* structural explanation we are missing for the 0.31-field delta — e.g., is our transform+draw CPU merely fatter per field (reference: 156+130 scanlines ≈ 130k CPU per frame vs our ~170k) and the real fix is only CPU-fat reduction, with the scheduling already optimal?
3. For the contour (57 short LINE blits, ~1.15k each serial): any canonical OCS alternative that a *generic* engine could expose (we will not special-case this demo)? Note the parity constraint (horizontal edges skipped, ONEDOT|EOR, D fixed to bitmap base per line, discovered above).
4. If you had to bet one more time on a single measurable change to go from 2.31 to ≤2.05 fields under these constraints, what would it be, and what is its falsification test?

Data/code refs: `demos/techniques/amiga/effects/116_flatshade_convex/{README.md,VALIDATION.md}` and `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` (bitácora, entries 2026-10-17) in this repo; emulator source refs as quoted above.
