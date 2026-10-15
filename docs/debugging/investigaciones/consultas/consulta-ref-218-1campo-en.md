# Consultation — Port of Knoester's "SPR Layer" to a C++23 engine: why can't we match the original's 1-field cadence? (English, for Grok)

## Context

We ported Jeroen Knoester's *SPR Layer* demo (Amiga 500 / OCS, PAL; original in m68k assembly) to a
modern C++23 68000 engine (m68k-elf-gcc 13, `-mcpu=68000`). The port is **structurally faithful**:
same subtree of sections in the main loop, same anchor (raster line `$2c` in the original; we
measured and then moved it for the campaign — see below), same 4 copper lists with the same
publication scheme, same 8+11 sprite columns, same 19 position fills, same 28 half-tile data
blits, same 9 BOB cookie-cuts, same 32-frame calendar, same 3 FG buffers, same restore scheme.

Both binaries run on the same emulator (WinUAE fork, cycle-exact), same machine config, and we
measure both with the same tools.

## Hard measurements (all reproducible; methods noted)

**Loop cadence**
- Reference: **1.00 field per loop**: 141.6–142.6k CPU cycles between successive `COP1LC` writes
  (GDB watchpoint on `$DFF080` + side-channel cycle counter). 50 updates/s.
- Port: **2 fields** (284.2k cycles, stable). 25 updates/s.

**Bus placement** (frame profiler DMA grid, 227×313 cells = one bus slot per (hpos,vpos); DMA
types from `debug.h:361-372`)
- Display DMA identical in both (Copper ~19.2k, bitplanes ~17.9k, sprites ~3.6k, refresh ~1.2k per
  field).
- Blitter: port ~22.5k slots/update, **82% inside the 89-line VBlank window** (top+bottom
  borders); reference 23.0k/field, 75% in borders.
- CPU (chip bus): port ~9.9k slots/field vs reference ~4.7k.
- Total bus density: reference 98.1%, port 89.7%.

**Per-job measurements (port, debug-peripheral cycle counters)**
- Pure emission per BOB (`BlobBatch::one`, CookieCut, measured from `wait()` exit to `BLTSIZE`
  write, temporary counter): **290–356 cycles/job**. Disassembly shows ~38 instructions; the
  original's `BlitBob` is ~30–35 instructions and uses a **precomputed `CON0+CON1` table per shift**
  (`bl_bob`, `GFX/blitter.asm:114-119`).
- Tiny data-column jobs (28 × 1 word × 16 lines, 48 bus slots each) cost **~2.35k cycles/job** in
  our schedule vs **~1.25k/job** in the reference (same shapes). The measured portion is the band:
  they run in the display window (~125 cycles/line).
- The fills (19 × 1 word × 224 lines) cost ~1.3k/job in our schedule (border speed, ~2.9
  cycles/slot) — faster per job than the reference's `BlitPattern` (~3.43k).
- The 9 BOBs cost ~4.4–5.0k/job (1,536 slots each) when they land in the borders — same as the
  reference's `BlitBob` (~4.64k).

**Experiments tried (one variable each, measured; full log in the campaign journal)**
1. Anchor sweep 298–308 (hot-patch): flat 163.1–164.3k (plateau).
2. Section re-orderings (restore first vs after fills; data block first vs last): all tie or get
   worse (the ~26k-cycle border window is a zero-sum resource: moving one section in expels
   another to the display at ~11 cycles/slot vs ~2.9 in the border).
3. Linear staging (CPU de-interleave + 2 wide blits) vs the original's 28 direct blits: tie
   (~164.5k); we kept the original's structure.
4. 9 → 7 BOBs (real work cut): **−10k measured** (163.5k → 153.5–155k), still > 142k, and the
   period becomes unstable (light frames hit 1 field → mixed 50/25 Hz = judder), so we reverted.
5. BOB strips (one blit per row, barrel-shifter across cells): rejected — the shifter bleeds bits
   through the 1-word gaps (`BLTALWM=0` only protects line ends), 8.7k–18.4k px/frame diff.
6. IRQ-chained blitter queue (no per-job waits): works, but no gain here (no CPU work to overlap;
   268k vs 221k in the older build).
7. Merging the 19 position fills into one wide blit: not viable — the 19 POS values differ; a
   pattern-source merge would need A fetches (+4.2k slots). The original also uses one `BlitPattern`
   per column.

## The arithmetic that we cannot close

To reach 142.1k we must remove **~21.5k** from 163.5k. Identified faithful levers:
- 7 BOBs: −10k (measured; but 7 bobs is already a fidelity cut and does not fit either).
- Emission flattening (CON0+CON1 table + op specialization): −2 to −5k (measured emission implies
  ~40–80 cc/job over ~59 jobs/update).
Sum: **~148–151k — still 6–9k short**. The remaining work is real content (fills/columns/BOBs),
not placement or emission.

## Questions

1. **Reconcile the reference's loop**: its measured budget is 23k blitter slots/field, 17.4k of
   them in the 89-line border window (≈40k cycles at its ~2.3 cycles/slot), plus 5.5k display
   slots (~11 cycles/slot ≈ 60k) ≈ 100k of blitter wall, plus its CPU body, all inside 142.1k. Our
   totals are nearly identical (~22.5k slots, 82% border) yet we land at ~163.5k. What are we
   mis-modelling? (Is the border window effectively larger? Does its CPU overlap the display-window
   blits? Is our ~11 cycles/slot display estimate wrong for its case?)
2. Is there **any avenue faithful to the original** (no changes to the effect: 9 BOBs, 19 columns,
   28 data blits, 4 lists, same calendar) that can recover ≥20k, which we have missed?
3. The nasty prefix: in both implementations each job starts with nasty OFF (the next job's
   `BlitWait` enables it). Is this a real, measurable penalty in cycle-exact WinUAE for jobs
   shorter than ~1 raster line, and is there a faithful way to keep the blitter prioritised across
   a batch?
4. Given the numbers, is the honest conclusion "the C++-engine emission overhead plus the demo's
   total work cannot close the last ~13% at 1 field, and the residual is structural" — or is there
   a specific structural difference in the original we have mis-ported?

Constraint: we will **not** deviate from the original's algorithm/effect to reach 50 fps; we only
want to know if we are missing something faithful, or if the floor is confirmed.
