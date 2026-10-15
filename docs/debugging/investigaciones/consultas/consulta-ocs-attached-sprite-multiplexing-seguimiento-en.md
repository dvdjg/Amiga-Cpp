# Consulta a Grok — Follow-up: attached-pair reuse still uneven + first-column artifacts (inglés)

Follow-up consultation (ask Grok in English, AGENTS §1.3). Self-contained; describes the current
broken output precisely because the reviewer cannot attach the PNG.

---

## Setup

Amiga 500, OCS, PAL, 4 bitplanes, one Copperlist per frame. A demo draws **three horizontal
background bands** with sprites multiplexed per scanline (rewriting `SPRxPOS` across the width;
palette registers reloaded once per band):

- **Band A**: 8 **non-attached** sprites (hill figure), pattern 128 px, channels 0..7.
- **Band B**: 6 **non-attached** sprites (valley figure), pattern 96 px, channels 2..7; plus two
  free sprites ("objects") in channels 0/1 that roam on top.
- **Band C**: 4 **ATTACHed pairs** (channels 0..7), concentric-rings figure, pattern 64 px,
  15 colours (even channel = colour bits 0-1, odd = bits 2-3, `SPRxCTL` bit 7 = ATTACH on the odd).

Per line the Copper does, for each pattern period: a `WAIT` (head-start) before the target X and
then one `MOVE` of `SPRxPOS` per channel of the run (both channels of each pair, same X, in the
order even, odd). `SPRxCTL` is **never** rewritten mid-line. Structures are DMA-valid with a
header + terminator.

## What already works

- Band A renders correctly.
- The attached pair shows the full **15 colours** at its **initial arm** position.
- Raising the Copper `WAIT` head-start from 24 to **56 px** removed a per-frame **flicker** of the
  first column of each reposition group (verified over 12 consecutive frames: all columns stable
  98-99% content).
- Repositioning **both** `SPRxPOS` of each pair recovered ~15 colours over **part** of band C.

## Problems that remain (measured from captured frames)

1. **Band C is uneven left→right.** The first ~3 pattern periods (≈192 px) render 15-colour
   (rainbow bands); the later periods fall to **4 colours** (the odd channel's high bits are lost)
   even with a 56 px head-start. Pixel test on 16-px columns: `16 16 14 11 16 16 14 11 4 4`.
2. **An empty (navy) gap** appears in band C where a period should be, and a **small stray ring**
   is drawn **outside** the band at the bottom-right.
3. **The first column of each band** renders as a **solid vertical strip** (band B's channel 2 at
   the first period shows one flat colour instead of the valley profile).
4. A **one-scanline row of stray pixels** between bands (line 128) shows the object palette
   (red/white/green) at two x clusters (~136-148 and ~232-244 lo-res), i.e. a band-A channel/object
   boundary artifact.

## Questions

1. For an attached pair reused N times per scanline, is there a **hard limit on the number of
   reuses** (Denise shifter reload / DMA slots) that explains why only the first ~3 periods keep
   the 15 colours, or is it purely (head-start / channel order)?
2. Within a pair, does the **order** of the two `SPRxPOS` writes matter (odd before even)? Must both
   land before the target, or is there a per-channel window that the even/odd pair must share?
3. Typical causes for the **"first column renders as a solid strip"** and the **stray ring outside
   the band** when a sprite channel is reused across bands (same channel serving band A then band
   B) — stale DMA fetch of a header, missing/wrong terminator, or a `SPRxCTL`/`SPRxPT` timing issue?
4. Is there a **known-good reference Copperlist** for a full-width, 15-colour, attached-pair
   multiplexed background (Risky Woods–style) we can follow register by register?

Please answer with concrete Copper sequences / limits, and say explicitly if any of the above is a
real OCS hardware limit versus a sequencing bug.
