# Follow-up 3: two OCS hardware errors in the reference (split line VPOS, BLTSIZE height)

I implemented your reference and hit two hardware limits. Please confirm my corrections or give the right solution, with exact values (I will wrap them in `static_assert`).

1. **Split line vs 8-bit VPOS.** You set `SPLIT_LINE = 0x2c + 256 = 300`. On OCS the Copper `WAIT` VPOS field is **8 bits (0–255)**, so line 300 **cannot** be waited. My fix: for split modes, cap the viewport at **`viewport_h ≤ 208`** (`0x2c + 208 = 252 ≤ 255`). Is that the intended limit, or is there a standard technique to split below line 255 (move `DIWSTRT` earlier, use `DIWSTOP`, or a different `WAIT` sequence)?

2. **`BLTSIZE` height is 10 bits (max 1024).** Your "one tall column" is `RING_H*PLANES` planelines = **1280** (256×5) or **1040** (208×5) — it does **not** fit in one blit. My fix: split it into `ceil(column_planelines / 1024)` blits. Please give the **exact per-chunk registers**: source address offset per chunk, dest address offset per chunk, `BLTSIZE`, `BLTAMOD`/`BLTDMOD`, so the column is contiguous with no skipped source/dest. For 1040 (chunk0 = 1024, chunk1 = 16): the chunk1 source starts 1024 words into the composed column — what is the exact dest offset within the interleaved ring (in bytes) and the `BLTSIZE`?

3. **Fallback (no pre-composed column).** If I ever blit **per tile** instead of a pre-composed column (tile = 80 contiguous planelines), give the exact `BLTAMOD`/`BLTDMOD`/`BLTSIZE` and how the 16 tiles are chained into the ring so the column is correct.

4. Confirm the **guard** invariant in **words** for steps up to 16 px and for tile 32 (`guard_words ≥ ceil(step_px/16) + 1`?) and the exact `BLTDMOD` for the 32×32 case (`RING_W_BYTES − 4`?).

Give the numbers as compile-time constants.
