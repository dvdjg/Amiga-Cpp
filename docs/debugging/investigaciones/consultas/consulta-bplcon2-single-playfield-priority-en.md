# Consultation (English): BPLCON2 priority in a single-playfield setup

> Ask Grok in English (per `AGENTS.md` §1.3). Self-contained; copy this file's text into the
> chat. Related: `docs/reference/amiga/techniques/sprite-layer.md` §5,
> `docs/reference/emulators/winuae/sprite-color-priority.md`, demo
> `demos/techniques/amiga/sprites/214_attached_object`.

## Context

Amiga OCS/A500 demo. Display is a **single playfield** of 4 bitplanes (320x256, `BPLCON0=$4200`),
plus 8 hardware sprites used as objects (one *attached* pair of 15 colours + six 3-colour
sprites). Our Copper list was writing `BPLCON2 = $0000` at the top of every field. With that
value, all sprites were **invisible wherever the playfield pixel had a non-zero colour index**;
they only appeared over the border and over playfield areas whose pixel index was 0. Writing
`BPLCON2 = $0024` made all sprites appear in front of the playfield, as intended.

We then found in the Amiga Hardware Reference Manual (3rd ed.):

- Table 7-1: bits 2-0 = `PF1P2-PF1P0` ("Playfield 1 placement with respect to the sprites"),
  bits 5-3 = `PF2P2-PF2P0` ("Playfield 2 placement...").
- Table 7-2 ("Priority of Playfields Based on Values of Bits PF1P2-PF1P0") lists `000 ->
  PF1 SP01 SP23 SP45 SP67 ... 100 -> SP01 SP23 SP45 SP67 PF1`.
- A note right after Table 7-2 says: *"Be careful: PF2P2-PF2P0, bits 5-3, are the priority bits
  for normal (non-dual) playfields."*
- The manual's own example (chapter "Setting the Priority Control Register" / register list)
  uses `MOVE.W #$0024,BPLCON2 ;Sprites have priority over playfields`.

So `$24` = `PF1P=100`, `PF2P=100`.

## Questions

1. **Single playfield (non-DPF):** which field actually places *the* playfield in the priority
   chain relative to sprites - `PF1P` (bits 2-0) or `PF2P` (bits 5-3)? The Table 7-2 note says
   `PF2P` is the one for normal (non-dual) playfields, yet Table 7-2 and the register
   description are written in terms of `PF1P`. Is `PF1P` ignored in non-dual mode, or does it
   apply to playfield 1 anyway (e.g., both fields ORed into the chain)?
2. **Reset value:** what is the reset value of `BPLCON2` on OCS (A500) and ECS? Is it `$0024`?
   (The manual chapter does not state it; we want the authoritative reset/default.)
3. **`BPLCON2 = $0000` semantics:** is it correct that with `PF1P=000` (and/or `PF2P=000`) the
   playfield is placed **in front of all four sprite groups**, so sprites are only visible over
   playfield pixels with colour index 0 (plus the border)? We observed exactly that and want it
   confirmed as the documented behaviour (and not emulator-specific).
4. **4 vs 5+ bitplanes:** the manual/code mention an OCS/ECS quirk when `plf2pri >= 5` and
   bitplane 5 is set. Does any of that change the answer for a plain 4-bitplane screen
   (`BPLCON0=$4200`)?
5. **Recommended canonical value** for "one playfield; all sprites in front": confirm `$0024`,
   or discuss `$0004` (`PF1P=100`, `PF2P=000`) if that is more precisely defined for
   single-playfield mode. Which one would real games/demos use, and why?

## What we are using now

`BPLCON2 = $0024` written once per field (engine default:
`copper::Scheduler::emit_planes_display`), with a comment citing AHRM Table 7-2 and the
`MOVE.W #$0024,BPLCON2` example. Sprites are visible over the whole playfield; regression of the
affected demos is green. We want to make sure this is the canonical/portable choice and to fix
the reference documentation with the correct rule.

## Answer (Grok, 2026-10)

1. **Single playfield: the controlling field is `PF2P` (bits 5-3).** The AHRM note after
   Table 7-2 overrides the general `PF1P` description: *"PF2P2-PF2P0, bits 5-3, are the priority
   bits for normal (non-dual) playfields"*. Real hardware and demos confirm it: changing only
   bits 5-3 moves the (single) playfield relative to the four sprite pairs; bits 2-0 do not
   control it.
2. **No documented reset/power-on value for `BPLCON2`.** It is undefined/residual; every correct
   copper list (including Kickstart's) writes an explicit value. Treat it as unknown and always
   initialise it.
3. **`BPLCON2 = $0000` semantics confirmed:** `PF2P=000` puts the playfield at the front of the
   priority chain (`PF SP01 SP23 SP45 SP67`); sprites are visible only over colour-index-0
   pixels or the border. This is real OCS behaviour, not an emulator artefact.
4. **The `PFxP>=5` OCS/ECS quirk needs ≥5 bitplanes**; it never triggers on a plain 4-bitplane
   screen, so nothing above changes for `BPLCON0=$4200`.
5. **Canonical value: `$0024`** (the manual's own example). `$0020` (only `PF2P=100`) is enough
   for pure single-playfield, but `$0024` is the portable, dual-playfield-safe choice used by
   virtually every game/demo. **Keep `$0024`.**

Outcome: the engine fix (`BPLCON2=$0024` in `emit_planes_display`) is the correct, portable and
documented solution. Reference docs updated with the `PF2P` nuance (`sprite-layer.md` §5,
`winuae/sprite-color-priority.md`).
