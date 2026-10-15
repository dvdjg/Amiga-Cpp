# Follow-up consultation: reference implementation of the Copper-ring + strip scroller (A500 OCS PAL)

Thanks — your recommendation matches my measurements (Copper ring + incoming-strip painting; keep single-playfield; DPF 3+3 realistically 25 fps; pixel-burst + block fusion; precomputed tables; 32×32 via the strip path only). To implement it I need a **concrete reference implementation** with exact numbers and verifiable invariants. A500 OCS, PAL, 320×256, single-playfield 5 planes for the base case, 16×16 and 32×32 tiles, steps 1..16 px/frame, target 50 fps (1 field) with <10 % CPU hot path.

Please provide, in the same style (pseudocode or m68k/C, with exact register values), the following.

## A. X-only strip scroller (single playfield, 5 planes, 16×16, 320×256, PAL)

1. **Ring bitmap geometry**: exact width in bytes, height, where the 32/64-px guard sits, and how the visible window maps into it.
2. **Static Copper list**: the full list (WAIT/MOVE order) and exactly which words are patched per frame (`BPLxPT`, `BPLCON1`, `BPLxMOD`, `DDFSTRT/STOP` if needed) — and why only those.
3. **Per-frame Blitter job(s) for the incoming strip**: `BLTCON0/BLTCON1` (minterm, shift `ASH`), `BLTAFWM`/`BLTALWM`, `BLTAMOD/BLTBMOD/BLTCMOD/BLTDMOD`, `BLTSIZE`, and the source (tile bank) / destination (ring) offsets — as concrete numbers for one worked example (e.g. 5-px step, non-aligned x).
4. **Order of operations** relative to the beam: blit → patch Copper → display. How do you guarantee no unpainted pixel is shown (guard rule in **words**, not pixels)?
5. **Precomputed tables**: what arrays to build at setup (per-column ring pointer, per-column source offset, per-x-offset crossing geometry…) and how the per-frame loop indexes them in O(1).
6. **Map streaming**: how the new tiles enter the ring only when a tile column is crossed, without a per-frame scan (double-buffered guard region?).
7. **Steps 1..16 px**: how the pixel-burst is parameterized; when (and how) to fuse rows; the exact guard formula, e.g. `guard_words >= ceil(step_px / 16) + 1`?

## B. XY strip scroller with vertical ring + one Copper split

1. Vertical ring geometry (`viewport_h + 2*tile_h`?), the exact split (`WAIT` line + `BPLxPT` re-point), and how the split interacts with the X ring.
2. When and how the incoming **column** and **row** strips are painted (order, guards in both axes).
3. Precomputed tables for 8-way (per direction/axis), and the seam handling (does the strip path need the corkscrew's saveword at all?).

## C. 32×32 tiles

The concrete differences: 2-word-wide tiles, 64-px guard, 32-px-high interleaved strips, fusion of consecutive rows into one blit. Worked example with numbers.

## D. DPF 3+3

If 50 fps is truly impossible: the **minimal safe design at 25 fps** (what exactly to cut: fetch mode, guard, number of blits, world streaming) and the per-line budget left for Blitter/CPU.

## E. Verification without hardware

What **host-side test** proves the invariant "no pixel is ever revealed unpainted" and "the per-frame blit count is bounded" for random maps/steps? (I already have an equivalence/coherence test harness for the blit sequence.)

Wherever possible give **exact values** (registers, modulos, byte offsets, sizes) and the **invariants** so I can bake them into compile-time `static_assert`s and host tests.
