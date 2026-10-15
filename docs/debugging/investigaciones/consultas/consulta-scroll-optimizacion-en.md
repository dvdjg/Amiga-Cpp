# Technical consultation: optimizing tile-scroll algorithms on the Amiga 500 for 50 fps

> Self-contained question (to ask an external model, in English). No repo access required.

## 1. Goal and acceptance criteria

2D engine for the **Amiga 500 (OCS, PAL)**. I need tile-based scrollers that run **comfortably at 50 fps** (1 logical frame = 1 PAL VBlank) with **ridiculously low CPU usage in the hot path** (ideally < 5–10 % of the frame; the rest left for game logic). If an algorithm does not reach 50 fps with low CPU, **it is not acceptable**.

Requirements: **every "limited" variant** (X, Y, XY), clamped and wrapping maps; **16×16 and 32×32 tiles** (interleaved bitmaps, 4–6 planes); **scroll steps from 1 to 16 px/frame**; **DPF 3+3** as an option.

## 2. Hardware (A500 OCS PAL)

- 68000 at 7.09 MHz, Chip RAM shared with DMA.
- Frame: **312 lines**, ~**227 bus slots/line** → ~**70 800 slots/frame**. Odd slots are real-time DMA (refresh 4, disk 3, audio 1/channel, sprites 2/channel); even slots are shared **Copper > Blitter > CPU**.
- **Bitplanes**: ~20 words/line per 320-wide lores plane. 4 planes ≈ 80 slots/line; **DPF 3+3 = 6 planes ≈ 120 slots/line**, leaving ~107 for Copper/Blitter/CPU.
- **Copper**: ~2 slots/MOVE, 3/WAIT. Patching `BPLxPT`/`BPLxMOD` is cheap; re-emitting the list is not.
- **Blitter**: ~1 word/cycle; it **must finish before being reprogrammed**. It paints to an interleaved destination as one "tall" column (a single blit of N words × M plane-lines).

## 3. Reference variants (ScrollingTricks, Steger)

16×16 blocks, 4 planes, 320×256, single-buffered, interleaved.

| Variant | Axes | Bitmap | Fetch | Split |
|---|---|---|---|---|
| XUnlimited | X | 704 | 1x2x4x | no |
| XLimited / _64 | X | 352 / 384 | 1x2x / 1x4x | no |
| YUnlimited | Y | 320×576 | 1x2x4x | no |
| YUnlimited2 | Y | 320×288 | 1x2x4x | yes |
| XYLimited / _64 | XY | 352 / 384 | 1x2x / 1x4x | yes |
| XYUnlimited / _64 | XY | 352×289 / 384×289 | 1x2x / 1x4x | yes |
| XYUnlimited2 / _64 | XY | 352×289 / 384×289 | 1x2x / 1x4x | yes |

"Limited" = clamped map; "Unlimited" = wrapping. X = X ring (bitmap = viewport + 32/64 px guard); XY = corkscrew (X ring + vertical ring `viewport_h + 2*tile_h` with Copper split); Y = vertical ring.

## 4. Current design (two paths)

**A) Corkscrew** (`ScrollEngine` + `XLimitedPlayfield`, a port of Scroller_XYLimited): interleaved bitmap of width `viewport_w + extra(32/64)` and height `display_height = viewport_h + 2*tile_h`; the display walks the ring and at `split_line` the Copper re-points to the base. Per scrolled pixel it paints the **incoming column** as **16×16 blocks, one per blit** (Blitter plane-shift), placed around the ring; a **saveword seam guard** is saved/restored on direction change. It advances in 1-px sub-steps (with a per-tile burst). **Constraint found**: `mapy` reaches `tile_w+1`, so it requires `bitmap_blocks_per_col ≥ tile_w+2` (16-px tile with a 288 ring: 18 ≥ 18 ✓; **32-px tile would need a ring ≥ 1088 lines** ✗) → it looks **hard-wired to 16-px tiles**.

**B) Tile scroll driver**: Copper ring (`BPLxPT`/modulo) patched per frame (no re-emit) + Blitter filling the incoming columns/rows into a staging band. Single-playfield reference at 50 fps.

## 5. Measurements (WinUAE, A500 PAL, release)

| Path | fps | fields/frame |
|---|---|---|
| Tile scroll driver X single (5 planes) | **49.9** | 1.00 |
| Tile scroll driver XY **DPF** | 30.3 | 1.65 |
| Corkscrew Y | 14.5 | 3.44 |
| Corkscrew XY + **DPF** | ~17 | 2.9 |

Breakdown of the XY corkscrew demo (A/B): **world streaming ≈ 1 field + scroll ≈ 1 field + DPF base ≈ 1 field**. With DPF 3+3 the bus is saturated, so even tiny blits cost about one field. The 2-px scroll issues only ~2–4 tiny blits.

## 6. Concrete questions

1. **Optimal display mechanism per variant** (X ring, Y ring, XY corkscrew, per-line split) for 50 fps. Should I standardise on the Copper ring + strips (which hits 50 fps single) and drop the corkscrew, or can the corkscrew be optimized down to 1 field?
2. **DPF 3+3 at 50 fps**: with 6 planes (~120 slots/line), is it viable to leave enough CPU/Blitter? If not, what is the realistic ceiling (25 fps?) and the per-line budget left?
3. **Steps 2..16 px** (16 and 32 tiles): is a **px-burst** (paint the N-px strip with the crossing geometry computed once) enough, or is **block fusion** (one blit per several contiguous rows) needed? With a 32-px tile and a 16-px step the plane-shift **is not 0**; how should the guard band be sized (in **words**) and when should blits be issued so that no pixel is ever revealed unpainted? Should I **pre-render strips** into the guard and only move pointers, and how does that combine with the vertical split?
4. **Hot path**: what to precompute at setup/compile time (per-column/row ring pointer tables, per-tile crossing geometry, a patchable Copper list) so the per-frame loop does only a few Copper MOVEs and ≤1–2 blits per crossed column.
5. **32×32 tiles in the corkscrew**: how to adapt it (the `mapy` that reaches `tile_w+1` ties it to 16), or should 32×32 be handled only by the strip path?

## 7. Criteria

Per variant: build OK, **50 fps (1 field)** on A500 PAL, **low CPU**, no gaps/tearing, continuity test at 1 px and at 16 px. If something is inherently impossible in hardware, document the limit and why, and implement the best version that does reach 50 fps.
