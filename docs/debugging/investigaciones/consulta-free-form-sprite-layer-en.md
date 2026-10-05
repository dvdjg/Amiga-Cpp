# Consultation — "Free Form Sprite Layer": Copper-repositioned/reloaded columns do not render

Self-contained English question for an external AI. Context: OCS Amiga (A500), lores, OCS chipset,
PAL. Technique to implement: **Free Form Sprite Layer** (Jeroen Knoester,
https://powerprograms.nl/amiga/spr-layer.html): a **non-repeating 320px background** built only from
the 8 hardware sprites, using **horizontal sprite multiplexing**. The article's plan: the 8 channels
show the first 128px via sprite DMA; the Copper then **repositions and reloads the sprite data** of
those channels further to the right (≥24px apart) until 320px are covered; every scanline ends by
repositioning all 8 channels back to the left. Costed at "1 wait + 41 moves per line = 85 DMA
cycles/line".

## What I have implemented

1. 8 sprite DMA channels. Each has a Chip-RAM structure `[POS, CTL, DAT0, DATB0, … DATn, DATBn, 0, 0]`;
   `SPRxPT` points to the structure; `SPRxCTL` has `VSTOP = band_top + height`. These 8 channels draw
   columns 0..7 (128px) **via DMA** — **these render correctly and each column is distinct.**
2. Per scanline, after a single `WAIT` at hpos `0x40`, the Copper emits a burst, for each extra column
   `k = 8..19`:
   ```
   MOVE SPRxCTL[ch] = ctl ; MOVE SPRxPOS[ch] = pos ; MOVE SPRxDATB[ch] = datb ; MOVE SPRxDATA[ch] = dat
   ```
   with `ch = k % 8`, `pos = (line<<8) | ((hpos0 + k*16) >> 1)`, `ctl = (line+1)<<8` (VSTOP = line+1),
   and `dat`/`datb` = the image's column `k` for that scanline (distinct per column).
3. **Symptom**: the extra Copper columns **never appear**; only the 8 DMA columns render (128px).
   In an earlier variant with **no DMA structures at all** (every column written by the Copper as
   `POS + DATB + DATA`, no `CTL`, 3 moves/column), I saw a **~1/3 pattern** (roughly every other
   column, with a dashed/striped look inside the columns) — which I interpreted as the Copper falling
   behind the beam.

## Questions

1. **Does the sprite DMA fetch clobber the Copper's mid-line writes?** The AHRM (ch. 4, sprite data
   registers) says the data registers "are either rewritten by the user or **modified under DMA
   control**". At each line start Agnus fetches the two data words into `SPRxDATA`/`SPRxDATB`. If I
   reload `SPRxDATA`/`SPRxDATB` by Copper **later in the same line** (before the beam reaches that
   column's X), do those writes survive and get displayed, or does the DMA re-fetch overwrite them?
   If they are overwritten, how does the Free Form work at all — must the channel's sprite DMA be
   **disabled** and the whole line driven by the Copper in manual mode?
2. **Correct per-position register sequence?** AHRM: writing `SPRxCTL` *disables* the horizontal
   comparator; writing `SPRxDATA` *arms* it and makes it output at the `SPRxPOS` H position. Is the
   right sequence `SPRxCTL + SPRxPOS + SPRxDATB + SPRxDATA`, or just `SPRxPOS + SPRxDATB + SPRxDATA`?
   Does writing `SPRxCTL` on every reposition break the sprite, and must it precede `SPRxPOS`?
3. **What exactly must be ≥24px apart** — the original DMA usage of a channel and its
   Copper-repositioned copy, or two successive reuses of the **same** channel? What does 24px
   correspond to in Copper/DMA cycles (roughly 3 Copper `MOVE`s)?
4. **Which channel should a repositioned column reuse?** For extra column `k`, should the Copper
   reuse `k % 8`, or `(k - dma_channels) % dma_channels` (only the channels that DMA is drawing), or
   any free channel? Is it a problem that a reposed channel already has an active sprite DMA
   structure/POS from the DMA arm?
5. **What `SPRxCTL` should the Copper write?** What `VSTART`/`VSTOP` (I use `VSTOP = line+1` for a
   1-line sprite), and does that disturb the channel's ongoing DMA state (the DMA's own `VSTOP`)? Is
   a `SPRxCTL` write needed at all, or is `SPRxPOS` + reload enough to re-arm the sprite at the new X?
6. **Timing**: is one `WAIT` at the start of the line + a single burst enough, or does each
   reposition need its own `WAIT`/spacing so the Copper stays ahead of the beam? (My reference doc
   claims "one WAIT per channel at its X does NOT work", only some channels arm.)
7. **Line-end reset**: must the 8 channels be repositioned **back to the left** at each line end so
   the next line's DMA arm works, or is that only for the article's scroll update?
8. **DMA enable**: does `DMACON` sprite DMA (bit 9) have to remain **enabled** throughout, or does the
   Free Form run with sprite DMA **disabled** (pure manual mode via the Copper)?

References I have: AHRM 3rd ed. ch. 4 ("Manual Mode" ~p.117, "Reusing Sprite DMA Channels" ~p.115);
WinUAE source `custom.cpp:10088` (sprite DMA fetch only while `hp <= plfstrt_sprite`, i.e. line start)
and `custom.cpp:10098` (Copper writes to SPRx regs routed to Denise state).
