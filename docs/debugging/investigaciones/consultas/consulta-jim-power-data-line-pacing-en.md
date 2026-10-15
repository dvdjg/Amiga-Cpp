# Consultation (English): Jim Power per-line DATA + POS repeats — exact Copper pacing

> Ask Grok in English (per `AGENTS.md` §1.3). Self-contained; related demo
> `demos/techniques/amiga/sprites/215_jim_power` and reference
> `docs/reference/amiga/techniques/sprite-tricks-games.md` §Jim Power.

## Context

Amiga OCS/A500, 4-bitplane 320x256 playfield, `BPLCON2=$0024` (sprites in front). We want to
reproduce the *Jim Power* background: **two non-attached sprites** (channels 6 and 7) form a
4-colour, **32 px** pattern; the Copper, **per scanline**, (a) loads each channel's DATA
(`SPR6DATA/DATB`, `SPR7DATA/DATB`) and (b) **repositions** each channel across the line
(`SPR6POS`, `SPR7POS`, step 16 px) so the 32 px pattern tiles up to 320 px. The pattern DATA is
**animated per line** (it differs line to line and frame to frame; no pre-shifted blits).

What we have working: the channels are fed by Copper with valid OFF DMA structures
(`VSTART=VSTOP=254` header + terminator) and `CTL` set once to the band range
(VSTART=44/VSTOP=204) so the horizontal comparator is alive; the DATA patch per frame works;
a first `WAIT` at `arm_hpos=$40` (after the sprite DMA fetch, like our demos 207/213) prevents
the DMA fetch from overwriting the Copper-written DATA.

## Question

Give the **exact per-line Copper sequence (MOVEs/WAITs, hpos head-starts) and the ordering
rules** to tile 320 px with two 16-px channels from a 32-px pattern, loading DATA once per line
per channel and repeating only `SPRxPOS`. Concretely:

1. **Ordering and head-start.** Where must each `SPRxPOS` write land relative to the beam for
   the previous column to finish, given the known bus rule that **1 Copper MOVE = 2 bus
   cycles = 8 lo-res px**? For a 32-px period with 2 channels (2 MOVEs = 16 px of Copper time
   per period) the Copper runs ~2x ahead of the beam: does the recipe need (a) a `WAIT`
   every N periods with a specific head-start, (b) 16-px periods (1 MOVE per channel per
   column in strict alternation), or (c) DATA per column (`POS+DATB+DATA` each 16 px, like our
   Free Form layer)?
2. **DATA load point.** Must the DATA be written in the channel's **first column of the line**
   (arming the shifter at that X), or can it be loaded once at the band top and reused all
   lines while only POS is rewritten each line? What `SPRxCTL`/`SPRxPT` state is required for
   the per-line POS-only re-arm (shifter copy on X match) to work for a **non-attached**
   channel?
3. **Observed failure modes** (to explain): (a) writing all POS of the line in one burst
   without WAITs leaves only the **last** X of each channel visible; (b) adding a `WAIT` per
   period as in our `208_risky_woods` non-attached bands (`WAIT` at `x - 32 px`) paints the
   first column and then the Copper stalls on the next WAIT (the beam already passed that
   hpos). What is the correct head-start formula so the Copper never arrives late but never
   drifts ahead either?
4. If the 32-px/two-channel variant fundamentally requires **4 channels** or a 16-px period
   to stay beam-locked (we recall the `2N MOVE = period` rule for attached bands), please say
   so and give the minimum viable arrangement for a full-width background.

Please include a short reference Copperlist snippet (register names + values) and cite any
documented rule (AHRM chapter 4 “Reusing Sprite DMA Channels”, codetapper Jim Power analysis).

## Follow-up with measured results (after applying your recipe)

We implemented exactly the proposed shape: per line, `WAIT (vpos, hpos $20)`; the DATA for
each channel is served **once per line by the channel's own DMA** (a header structure with
`CTL` = band VSTART/VSTOP, so no DATA MOVEs compete with the fetch and no OFF structure is
needed); then a **pure burst** `SPR6POS=$40, SPR7POS=$48, SPR6POS=$50, … $d8` (+$8 = 16 px,
no mid-line WAITs), 160 lines. `BPLCON2=$0024`, 4-bitplane playfield, DDFSTRT=$38.

**Measured result: only the last ~2 columns of the 20 are painted** on every line (the first
columns never appear), i.e. the comparator only ever sees the final `SPRxPOS` values of each
channel. Adding or removing **4 dummy MOVEs** right after the `WAIT` (to mimic the real
copperlist's DATA reload delay) did not change the visual outcome.

That suggests our Copper is far more than one 16-px period ahead of the beam on this line.
Questions:

1. What is the **exact cost of one Copper `MOVE`** on OCS while **4 bitplanes + sprite DMA are
   active** (bus stealing included) — 2 bus cycles (8 lo-res px) or longer? We need the real
   number to size the head-start.
2. With that cost, what is the **correct head-start** (WAIT hpos and/or the first POS X) so the
   Copper stays between 0 and one period ahead for the whole 320-px run? Is starting *behind*
   the beam (each POS written just as the beam reaches its X) the actual working regime?
3. Why does the real Jim Power copperlist use a **per-line variable** `WAIT` hpos ($1c / $20 /
   $24)? Does it compensate line-dependent bus stealing or the per-channel sprite DMA slot?
4. If the 2-channel/16-px-cadence pure burst cannot stay locked in emulation, is the
   documented fallback `POS+DATB+DATA` per column (our `213_spr_layer` Free-Form style) the
   correct one for this effect, and what would it cost per line?
