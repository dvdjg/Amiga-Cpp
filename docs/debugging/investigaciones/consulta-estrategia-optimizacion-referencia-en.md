# Consultation — How to structure an optimisation campaign when an optimised reference implementation exists

Self-contained English question. Please answer in English.

## Context (project and tooling)

I work on **Amiga-Cpp**: a C++23 engine plus ports of classic Amiga demos, validated against their
original 68k assembly sources. Everything runs in **WinUAE-DBG**, a fork of WinUAE with an
instrumented debugger. For this consultation assume I have the following capabilities (all real,
all currently underused by me):

- **Both sources**: the original 68k assembly of the reference demo, and my C++ port. Same
  assets, same copperlists, same per-frame work plan.
- **A working port and a runnable reference binary**, both executable in the same emulator.
- **GDB/RSP server** with symbol resolution for the port (breakpoints, watchpoints, register and
  memory access), plus a **side channel** (separate TCP port) that can, while the app runs:
  read/write memory (`mem`, `poke`), read CPU registers and the app's run-status (per-update
  counter), take screenshots, and **toggle render channels at runtime** (bitplanes, sprites,
  blitter, copper — including making the Copper skip writes on a line range). These are **hot
  modifications**: no rebuild, no relaunch.
- **Watchpoints filtered by access source** (cpu / copper / blitter / dma / bitplane / sprite
  channels) and by value/must-change, so I can break when, say, the blitter writes a range, or
  when COP1LC is written.
- **A frame profiler** (same data as vscode-amiga-debug's Graphics Debugger / Frame Profiler):
  per-scanline DMA and CPU samples, custom-register snapshots, blitter resource use, one
  screenshot per captured frame, dumped to a binary file I can parse. **I have not used it yet.**
- **Deterministic step capture**: a breakpoint at a per-update hook freezes the CPU once per
  update; screenshots at that point are reproducible within a build.
- **Publishable counters**: the port writes per-section cycle counts into a debug peripheral
  readable from the debugger (I use them for a section budget).
- **Bus-level accounting** is reachable: the profiler reports per-scanline DMA; the Copper cost of
  the effect is documented; the emulator is cycle-exact to the extent WinUAE is.

## The concrete case

Demo **218** is a faithful port of Jeroen Knoester's *Free Form Sprite Layer*: a non-repeating
full-width background drawn by 8 sprite channels (8 DMA columns + 11 Copper columns per line,
per-line `POS/DATB/DATA` writes, end-of-line reposition), 4 copperlists in 2 double-buffered
pairs, sprite-structure double buffering, a 4-plane playfield on top and 9 blitter BOBs.

- Port: loop period **284 204 CPU cycles = exactly 2 PAL fields → 25 updates/s**. Update body
  ≈ 220 600 cycles (positions 35k, column data 40k after a linear-staging change, BOBs 122k,
  rest ~20k). One update = 2 fields; the wait for the raster anchor fills the remainder.
- The user asserts the **reference runs 1 field per iteration (50 updates/s)** and wants the port
  to match (or at most ~15% slower). With a COP1LC write watchpoint I measured the reference at
  ~142k CPU cycles per iteration (1 field) — but the reference capture showed a different scene
  state than expected, so I do not trust that measurement.
- I proved that a "no-wait" diagnostic build (wait loops turned into no-ops) showed a 100k body
  and a 1-field period, but that was **invalid**: with waits removed the blitter jobs overwrite
  each other and the work is not actually done.
- I implemented a **lock-free SPSC blitter job queue chained by the level-3 blitter-finished IRQ**
  (INTF_BLIT) in the engine. It works, but it did **not** speed the demo up: serialized blitter
  wall time (under arbitration with Copper/bitplanes) ≈ 230k cycles ≈ 1.6 fields; the queue only
  allows CPU/blitter overlap and there is no CPU work to overlap in this update. Encodings: the
  queue in slow RAM (no Fast RAM on the emulated A500) costs ~6k cycles per enqueue while the
  blitter is running, so I now enqueue with the blitter stopped and "kick" at the end.

## The failure pattern I want to fix (the reason for this consultation)

This is the third session on the same goal, and the user's diagnosis is blunt and correct:

1. **I do not isolate.** I measure the whole scene (screen captures, masked colour correlations)
   instead of instrumenting one function/section at a time. Whole-scene heuristics alias
   (periodic dithers), mix layers (playfield vs sprite layer), and have produced false positives
   and negatives that I then chased for hours.
2. **Big-bang changes.** I change several things at once (a staging rewrite + a reorder + an IRQ
   queue + engine changes), then cannot attribute the deltas and have to reinterpret everything.
3. **I do not use the profiler** even though it exists specifically for "where do the cycles go".
4. **I do not use hot patching.** With the side channel I could disable/patch a single blit or a
   single section at runtime and measure immediately; instead I did edit→build→capture→analyse.
5. **I do not use the reference as a measurement baseline.** I never profiled the reference with
   the same tools to build a **per-section budget delta** (ours vs theirs). I mostly reasoned
   statically from their source and from my own numbers.
6. **Diagnosis by reinterpretation.** After noisy measurements I reinterpreted earlier ones
   instead of designing a decisive experiment that could falsify a hypothesis.
7. **I do not go step by step** despite being told to. I attempted portfolio optimisations and
   only late established a correct cost model (blitter wall time under DMA arbitration is the
   bound, not the CPU wait loops).

## What I want from you

Give me a **concrete, ordered strategy** (a campaign recipe) for closing a performance gap
against an **existing optimised reference**, given the tooling above. Specifically:

1. **Phase structure and gates**: how would you sequence the work from "reference is faster" to
   "gap explained (and closed or proven impossible)"? What are the gates and stop conditions?
2. **Differential budget**: the exact procedure to build an *attributable* per-component budget
   for **both** implementations with the same tools (reference included), so that the global gap
   becomes a table of deltas. Which measurements in which order? How to attribute a 2× global
   gap to specific jobs/sections/blits without guessing?
3. **Hot-patch A/B protocol**: a disciplined way to use runtime patching (memory pokes, render
   channel toggles, single-section disable, register forcing) to perform **single-variable**
   experiments without rebuilds. How to specify the metric, the baseline, the expected delta and
   the falsification criterion for each experiment?
4. **When to model vs when to measure**: rules of thumb to switch between static accounting
   (bus slots per line, blitter words, Copper moves) and empirical profiling; how to avoid the
   artifact-chasing loop I fell into.
5. **Reference as oracle**: any known-good methodology for "port fidelity vs performance"
   campaigns — e.g., reconstruct the reference's per-frame budget from its source *and* validate
   it by profiling the reference binary; how to compare per-frame work across two programs when
   only one has counters (what watchpoints/profiling do you use).
6. **Multi-session discipline**: how to keep a work log/decision journal, invariants and
   artifacts so that three sessions on one goal do not become three restarts.
7. **Checklist**: a short, printable checklist I can follow for each optimisation step
   (metric → baseline → one change → verification → revert rule).

If it helps, structure your answer as: (A) strategy/campaign; (B) concrete recipe for this
Amiga case; (C) anti-patterns to avoid and the smallest habit changes that would have the
biggest effect on my behaviour.

---

## Respuesta de Grok (recibida) — resumen incorporado

La respuesta completa se recibió y se **sintetizó en el documento canónico**
[METODO_OPTIMIZACION.md](../../guides/optimization/METODO_OPTIMIZACION.md) (fases F0–F5 con
gates, tabla de presupuesto diferencial, protocolo A/B con hot-patch y criterio de falsación,
modelar vs medir, referencia como oráculo, disciplina multi-sesión, checklist y anti-patrones),
cableado además en AGENTS.md §1.15/§4 y en DOC-MAP-PRINCIPAL.md §3. Puntos clave de la
respuesta que quedan como contrato:

- **Fidelidad primero**: no se optimiza hasta que ambos binarios producen la misma salida en los
  mismos puntos deterministas y el mismo plan de trabajo por frame.
- **La referencia se mide con las mismas herramientas** (profiler + watchpoints) antes de tocar el
  port; el hueco global se convierte en una **tabla de deltas atribuibles** (ninguna celda «creo
  que»).
- **Una variable por experimento**, con métrica, baseline, delta esperado y **criterio de
  falsación escritos antes**; hot-patch primero; reversión gratis.
- **El presupuesto diferencial es el único medidor de progreso**; si tras tres sesiones no hay una
  tabla viva, parar y reconstruirla.
- Cierre: cada delta > ~5 % del hueco global se cierra o se declara irreductible con motivo medido
  (objetivo ≤ 15 %).
