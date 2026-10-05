# Consulta a Grok — OCS attached-sprite multiplexing (inglés)

> **Respondida por Grok.** Es posible en OCS real: escribir **ambos `SPRxPOS`** (mismo valor, **sin** tocar `SPRxCTL`) con **antelación ≥ 16 px lo-res** (si el `WAIT` va cerca, `SPR0POS` llega pero el haz pasa antes de `SPR1POS` → solo 4 colores). La pérdida de los bits altos es comportamiento **físico de Denise**, no artefacto de WinUAE. Resultado aplicado y verificado en la 208 (`kCuGap=56`); ficha: [`sprite-color-priority.md`](../../reference/emulators/winuae/sprite-color-priority.md).

Consulta autocontenida en inglés (AGENTS §1.3). Preguntar a Grok con el texto de abajo.

---

## Question: reusing an ATTACHed sprite pair multiple times per scanline on OCS (A500/OCS Denise)

Setup: Amiga 500, OCS, PAL, 4 bitplanes, one Copper list per frame. I draw a 320px-wide sprite "band" by rewriting `SPRxPOS` several times per scanline (each sprite covers 16px; 6 channels, pattern period 96px, repositioned at 5 X positions per line). A per-line Copper loop does: `WAIT` a bit before each target X (head start ~24px), then `MOVE` the new `SPRxPOS` for each channel of the run. This works fine for NON-attached sprites.

Problem: a second band uses **attached pairs** (2 channels form one 16px, 4-bitplane/15-colour sprite): the even channel supplies colour bits 0-1, the odd channel supplies bits 2-3 (SPRxCTL bit 7 = ATTACH on the odd channel). At the **initial arm** (the pair's first position, where the Copper writes both channels' POS with the frame's VSTART), I get the full 15 colours. When the pair is **reused** at later X positions in the same scanline, only the even channel's 2 bits appear (4 colours) — the odd channel's contribution is lost.

Questions:

1. On real OCS, is it possible to reuse an ATTACHed pair at multiple X positions within one scanline and keep the full 15 colours at every position? If yes, what is the exact register sequence?
2. Is it `SPRxPOS` for **both** channels of the pair (even and odd) at the same X, with a `WAIT`/head-start before each position? Does the odd channel's `SPRxCTL` (ATTACH bit 7) need to be rewritten too, or does rewriting `SPRxPOS` alone re-arm the odd shifter while preserving ATTACH?
3. Does rewriting `SPRxCTL` mid-line disarm the channel (and thus lose ATTACH), so only `SPRxPOS` should be rewritten?
4. Any DMA-bandwidth / head-start constraints specific to attached pairs (two channels per 16px) versus single sprites?
5. Is the "lose the high bits on reuse" a real OCS hardware behaviour, or an emulator artefact? (Reference: WinUAE `drawing.cpp`, `matchsprites2` re-arms a sprite's horizontal shifter only when its own `xpos` matches the beam.)

What we verified in WinUAE source: `sprwrite` recomputes `xpos` on writing POS or CTL and stores the ATTACH flag in the even channel from the odd channel's CTL bit 7; `matchsprites2` re-arms each armed channel's shifter only when the beam equals its own `xpos & ~3`; `denise_render_sprites` ORs each channel's 2 bits and the attached mode combines both channels' 4 bits. So a reused pair plausibly loses the odd bits unless the odd channel's own `xpos` is updated too — but writing the odd `SPRxPOS` did not fix it in our test.

Answer format: concrete Copper register sequence (which registers, in which order, with which WAITs) that works on OCS for attached-pair multiplexing; or state clearly if it is impossible on OCS and why.
