# Consultation — Optimal lateral scroll for a "Free Form Sprite Layer" (minimise CPU + Blitter)

Self-contained English question. Please answer in English.

## Context

- OCS Amiga (A500), PAL, low-res, **320×256** visible.
- Technique: **Free Form Sprite Layer** (Jeroen Knoester): a **non-repeating** full-width background built from the **8 hardware sprites**, all 3-colour (+transparent). Reference implementation: `spr_layer/` (Jeroen Knoester), `GFX/layer.asm` + `Data/copperlists.asm`.
- **World**: 40 columns × 16 px = **640 px wide × 256 px tall**, 3 colours.
- **Viewport**: 320 px (20 columns) × 256 px. The background is **not** a repeating pattern — every column is distinct.
- **8 sprite channels**. The first 8 view columns are drawn by **sprite DMA** (one tall sprite per channel, each channel's Chip-RAM structure carries header `POS`+`CTL` + per-line `DAT`/`DATB`). The remaining 12 view columns are drawn by the **Copper reusing** those channels horizontally: per scanline, after one `WAIT`, the Copper writes, for each extra column `k`, `SPRxPOS` + `SPRxDATB` + `SPRxDATA` (no `SPRxCTL`), with `ch = k % 8`; at the end of each line the 8 channels are repositioned back to the left (reverse order). The copperlist is **emitted once** and never re-emitted.
- **Goal**: scroll the viewport **laterally 1 px/frame** across the 640 px world and wrap (≈640 frames per loop), **minimising both CPU and Blitter usage**. The Blitter is available. Chip RAM is plentiful (≥512 KB target).

## What the reference does (my reading — please correct if wrong)

- The fine 1-px step is done by writing **only the odd/even bit of `SPRxCTL`** (`UpdateSprCtl`, every other frame, one word per channel); `SPRxPOS` moves in 2-px steps.
- The `SPRxPOS` words are rewritten with the **Blitter in clear mode** (clear pattern = the new position value), spread over **4 frames** (*"4.75 columns ≈ 1064 words of sprite positions each frame"*).
- New column **DATA** is written with **BlitCopy**, spread over **~32 frames** (*"≈420 words of sprite data each frame"*).
- Four copperlists (double-buffered pairs) are used so the updates happen in the off-screen list while the other is displayed; the lists are swapped every 32 frames.

## Questions (please give the concrete optimal algorithm)

1. **Fine step (1 px).** Is writing only the `SPRxCTL` odd/even bit per (line, column) really enough, and how many words is that per frame? Does it need the full `CTL` (VSTART/VSTOP) or only bit 0? Any gotcha with interrupts on the exact pixel where `CTL` is written vs the beam?

2. **2-px step (`SPRxPOS`).** What is the minimal-word way to move all positions by 2 px: one Blitter clear-mode fill per column into the strided `SPRxPOS` words? How many words, how many blits, and what is the best frame split?

3. **New-column DATA.** Every 16 px of scroll a new world column enters the right edge (and one leaves the left). What is the cheapest way to put its 2 words/line (DAT+DATB) into the right Copper positions — clear-mode fill, copy, or a single tall interleaved blit? Given the per-line Copper layout is `POS,DATB,DATA`, what is the best destination layout to make one blit write `DATB`/`DATA` for all 256 lines of one column?

4. **Immediate DATA vs `SPRxPT` structures.** Would storing each world column as a sprite **structure** and using `SPRxPT` (pointer) instead of immediate `SPRxDATA/DATB` reduce the scroll cost? Note the channel reuse means the `PT` also changes per position and per line — does that make pointers better, worse, or equal for a Blitter-driven scroll?

5. **Copperlist strategy.** Is 4 copperlists (two double-buffered pairs, swapped every 32 frames) the minimum to avoid tearing without stalling the CPU, or is there a cheaper scheme (1 list + careful VBlank patch)? What exactly must be swapped and when?

6. **Frame split.** What is the optimal spreading of the position update (2-px) and the data update (new column) across frames so the per-frame Blitter budget and Copper bandwidth both stay safe, given the free-form Copper already costs ~85 DMA cycles/line?

7. **Line-end reposition.** The free-form repositions the 8 channels back to the left at the end of every line. Does that set of `SPRxPOS` words also have to be patched on scroll, and does it change the Blitter plan?

8. **Anything we are missing** to make a 1-px/frame, 640-px-wide, non-repeating sprite scroll run at 50 fps on a stock A500 with the CPU basically idle.

Thank you — a concrete register-level recipe (which words to patch, with which Blitter modulos, per frame) would be ideal.
