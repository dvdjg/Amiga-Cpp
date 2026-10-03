# Follow-up 2: gaps in the reference implementation (tile-bank source, exact modulos, streaming)

Thanks — the structure is clear, but a few points block a faithful implementation. Please close them with exact values/invariants.

1. **Source contiguity for the "one tall blit".** In A.3 you set `BLTSIZE = (RING_H*PLANES << 6) | 1` (1280 planelines) with `BLTAMOD = 0`, i.e. the source is a contiguous 1280-planeline strip. But a 16×16 tile is 80 planelines (160 B), and a full-height incoming column is **16 tiles that are not contiguous in a tile bank**. Which is the intended mechanism, and what is the resulting per-frame blit count and memory?
   - (a) pre-compose each map column into a full-height strip at map load (memory cost? do you cache only the visible/guard columns?);
   - (b) blit one tile per call (16 blits + 16 Blitter waits per column — does that still fit 1 field?);
   - (c) reorganise the tile bank so a column of tiles is contiguous.
   Give the recommended one and why.

2. **Exact interleaved modulos.** Confirm: `BPL1MOD = BPL2MOD = (PLANES-1) * RING_W_BYTES = 4*46 = 184 = $B8`, and for the tall column `BLTDMOD = RING_W_BYTES - 2 = 44 = $2C`; for 32×32 `BLTDMOD = RING_W_BYTES - 4`. And the source `BLTAMOD` when blitting per tile (with the tile stored as 80 contiguous planelines)? Give the exact modulos for the per-tile case too.

3. **Ring wrap / guard side formula.** With `visible_x = scroll_x % RING_W_PX`, when the window wraps past the right edge the incoming column must be painted into the region the window just left. Give the exact **destination byte offset** as a function of `scroll_x` and the double-buffered guard, and say how you guarantee the destination is always currently invisible.

4. **Streaming without per-frame cost.** "Tile IDs read from the map once": when a column is crossed, the 16 tile IDs come from the map and the strip must be composed. Spell out: is it composed only on crossing (and only the new column)? What work happens on a frame where no tile boundary is crossed (ideally 0 blits beyond the Copper patch)?

5. **XY split.** For `RING_H = 288`, give the exact Copper `WAIT` line and the 10 `BPLxPT` re-point words, how it interacts with the X-ring, and confirm the strip path needs **no** saveword seam.

6. **`DDFSTRT = $30`.** Confirm it is correct for 5-plane lores 320 with a 1-word scroll extra (vs standard `$38`), and the matching `DDFSTOP`.

7. **DPF 3+3 (25 fps).** With pre-composed/strip columns, the extra Chip RAM and the per-frame cost at 25 fps; and whether the strips can be shared between the two playfields.

8. **Host verification** (your §E): the exact predicate and data structures for "no pixel revealed unpainted", so I can implement it in a host test with the same strip descriptors.

Where possible, give the numbers as compile-time constants I can wrap in `static_assert`.
