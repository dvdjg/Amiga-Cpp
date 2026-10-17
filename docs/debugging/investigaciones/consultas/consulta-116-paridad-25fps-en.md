# Consultation (EN): why can't our high-level-API port of `flatshade-convex` reach the original's 25 fps (2.0 PAL fields/frame)?

## Context

Target machine: **stock Amiga A500, OCS, PAL, 68000**, emulated **cycle-exact** (WinUAE-DBG fork: `cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`). One PAL field = 312 scanlines = **141,876 CPU cycles** at 7,093,790 Hz. "Fields/frame" below = emulated cycles per completed game frame / 141,876.

Reference: the demoscene original `flatshade-convex.c` (repo `demoscene-repo-orig/effects/`, C compiled with Bebbo `m68k-amigaos-gcc`, `-O2 -fomit-frame-pointer -mregparm=2`, plus `register ... asm("dN")` hints). We booted its ADF in the cycle-exact emulator and profiled it per field: its displayed frames come in **identical field pairs**, i.e. it runs at exactly **2.0 fields/frame = 25 fps**. Its own scanline profiler reports per frame: **transform 156 lines (≈71k cycles) + drawing 130 lines (≈59k) + area fill 289 lines (≈131.5k)**.

Our port (demo `116_flatshade_convex` in our engine) is a faithful rewrite on top of our **high-level engine API** (no hand-written asm allowed by project policy; every optimization must be a reusable engine primitive, not a demo trick). Same mesh (60 vertices / 90 edges / 32 faces; ~34 visible edges/frame, each drawn in ~1.5–2 of the 4 bitplanes per its colour mask → ~51–57 blitter line blits per frame). It currently runs at **2.31 fields/frame (21.6 fps)**. The user's goal is **2.0 fields/frame** (parity), via generic, game-reusable engine code.

## What both implementations do per frame (same technique)

1. **Contour**: for every visible edge, for every bitplane of its colour mask, one blitter LINE-mode blit (`ONEDOT | EOR`, minterm BC0F_LINE_EOR), Bresenham params computed once per edge.
2. **Area fill**: one DOWNWARD `FILL_XOR | BLITREVERSE` sweep over the 4 contiguous planes (technically a 1024-row sweep = 16k words, ≈131k cycles — matches the original's own 131.5k).
3. **Clear**: the next (hidden) buffer pre-cleared with one blit of 1024 rows (4 contiguous planes), launched without waiting.
4. **Transform + lighting + face/edge visibility** on the 68000 in C++ (7 `muls.w` + 2 `divs.w` per visible vertex; the original does the same math with register pins).
5. Triple-buffer pipeline (3 × 4-plane buffers), double buffer via `BPLxPT` patching, frame loop vblank-locked through an Exec signal (sleep in `Wait()`), HUD on a 5th 1bpp plane inside a *legal* copper band (WAIT raster 44 … WAIT raster 58, both <255).
6. **BLTPRI (blitter-nasty) globally ON**, like the original (`DMAF_BLITHOG` at init).

## Hard measurements of OUR port (release, cycle-exact)

- Frame: **328,030 cycles = 2.312 fields (21.63 fps)**.
- Blitter serial work per frame: lines ≈66k + fill ≈131k + clear ≈65k = **≈262k ≈ 1.85 fields** (so a perfect-overlap bound of ≈27 fps exists).
- DMA-recorder per field (words accessed: CPU / Blitter / Bitplane): ours **19.8k / 21.4k / 20.5k = 62.7k** vs original **17.8k / 26.8k / 16.4k = 61.0k** — i.e. **per-field bus activity is at parity**; the difference is that the original completes a frame in 2.0 fields and we need 2.31.
- Section timeline of one update (measured, release):
  - wait-for-blitter at update start (previous fill+clear tail): **≈77k**
  - gather visible edges + program ~57 line blits + launch fill: **≈80k**
  - transform + lighting + visibilities (runs while the fill blits, BLTPRI on): **≈90k**
  - commit (`BPLxPT` patch) + launch next clear: **≈11k**
  - render/HUD + engine loop + *sleeping in Wait() for the next vblank*: **≈70k** (the frame sometimes fits in 2 fields and then sleeps, sometimes needs 3)
- Ablation builds (release, section removed → exposed cost): transform+overhead floor (**no edges, no fill, no clear**) = **142,102 cycles = 1.000 field**; fill exposed ≈66k; edges (contour blits) exposed ≈112k; clear exposed ≈100k.
- Per-blit serial cost of one line blit (incl. BBUSY wait + 6 custom-register writes with BLTPRI on): ≈1.1–1.2k cycles. The original's Draw total (≈59k / ~57 blits) is ≈1.0k per blit.
- Register programming per line already minimized: BLTCON0/1 in one 32-bit write, BLTBMOD/AMOD in one 32-bit write, 3 pointer longs, BLTSIZE = 6 custom accesses.
- Pipeline-order experiments (all else equal): clear launched at **end of update** = 21.23 fps; before the transform = 20.50; at the start of the next update = 21.01. The original's order is "clear first, right after vblank, then transform, then draw, then fill".
- `-O3` on the demo TU: +0.3 fps; `-O3` on the engine TUs: no change.
- HUD 5th-plane fetch: band-limited to ~14 lines/field (was ~290 with the plane on all frame).

## Facts we verified in the emulator source (so you don't need to re-derive them)

- WinUAE restarts an active blitter only when **BLTSIZE** is written while busy (`custom.cpp:3981` `BLTSIZE_func` → `do_blitter`; debug log "blitter was already active"). Other register writes while busy do not restart it — **but** in **LINE mode** the emulation reads `bltapt`, `bltcon0/1`, `bltamod`, `bltbmod` and the C/D pointers **live** during the whole blit (`blitter.cpp:731–745` `blitter_line_proc_apt`, write-handler at `:1097–1110` applies BLTCON changes mid-blit). Conclusion we drew: **you cannot shadow-program the next line while the current one runs**; the per-line BBUSY wait is a hardware reality, not engine overhead.
- Area fill in XOR mode carries per-column state across scanlines, so a bbox-limited fill cannot be a single blit spanning disjoint plane regions; and as 4 per-plane jobs it serializes (measured: worse than the full sweep).
- Our clear is already ONE blit of 1024 rows (`BitmapClearFast`-style).

## Constraints for any proposal

- Must be expressible as **reusable engine C++** (generic primitives usable by a real game: line/polygon/fill drawing, blitter scheduling, copper setup), **not** demo-specific tricks and **not** hand-written asm for the demo. Small assembly *inside the engine backend* would only be acceptable at the very end, matching gcc's output quality, and only if clearly generalizable.
- Accuracy/fidelity of the reference effect is non-negotiable (XOR contour + XOR area fill parity, exact original palette/geometry). Deterministic (must pass a pixel `freeze-diff`).
- We measure everything with the emulator's DMA recorder + per-section cycle counters; any idea should come with an expected saving (in cycles/fields) and a cheap falsification test.

## Questions

1. Given the measured section timeline and the ≈262k blitter-serial floor vs the original's 284k total (2.0 fields), **where do you think our extra ≈44k of non-overlapped time comes from**, and which single scheduling change would you test first? We observe that with BLTPRI ON the CPU stretches when it overlaps a blit (nasty steals the bus), so "more overlap" is not free — is there a known best-practice order for [clear | transform | lines | fill | commit] under blitter-nasty on OCS? The original's order measured worse for us (20.50 vs 21.23) — is our interpretation of the coupling wrong?
2. The 57 short line blits (≈66k blitter + per-line waits) are the least efficient part (~1.1k per blit of which most is per-blit serial overhead). **Is there a canonical OCS technique to draw a convex mesh contour with far fewer blitter starts** while keeping generic code (e.g. one blit per scanline set via area-fill boundaries, LINEMODE patterns, drawing the contour only into one plane and copying, or rethinking which primitives the engine should expose)? Note the contour doubles as the XOR fill boundary, so parity matters.
3. The 131k area-fill sweep (1024 rows over 4 contiguous planes) is at parity with the original, but it is 40% of our frame. Is there a known safe way to cut the sweep (e.g. seeding above the object bottom / below the top, exploiting that only ~204 of 256 rows per plane are dirty) that does NOT break the XOR parity state, either as one blit or as a schedule that doesn't serialize 4 jobs?
4. Our 70k of per-frame render+vblank sleep: is this simply the vblank quantisation of a 2.0–3.0 fields cadence, or is there a classic engine-level mistake here (e.g. waiting for the wrong edge of vblank, losing a field of parallelism)? How would you schedule to average 2.0 fields with jitter-free output?
5. Anything else in the numbers above that smells wrong to you? If you had to bet on ONE reusable engine primitive that would close a 25–45k cycle gap in cases like this, what would it be?

Please answer with concrete, measurable proposals (expected cycles saved per frame, what to instrument, and the falsification test). Assume we can patch hot spots on the fly via a side channel and re-measure within minutes.
