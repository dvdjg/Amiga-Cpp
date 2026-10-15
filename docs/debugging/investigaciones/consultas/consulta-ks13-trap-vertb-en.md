# Consultation (English): Kickstart 1.3 ROM — INTENA VERTB cleared under a process-installed level-3 vector, and CPU stuck at ROM $FC08E6 with a5=0

> Ask Grok in English (per `AGENTS.md` §1.3). Self-contained. Related demo:
> `demos/techniques/amiga/os/212_message_loop`. Investigation log:
> `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md` (to be written).
> Emulator: WinUAE-DBG (BartmanAbyss fork), cycle-exact A500 (`quickstart=a500,1`,
> `cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`),
> Kickstart 1.3 ROM, 512K+512K.

## Context

Our program runs as a **normal AmigaDOS process** (the runner's startup-sequence is
`stack 131072 / cd dh1: / :a.exe`), so the OS (exec/intuition/graphics/dos) stays alive.

What the program does:

- Replaces the **level-3 autovector** (vector `$6C`) with its own RAM handler, saving the
  previous ROM vector (observed saved values `$00FC0E40` / `$00FC0D14` in different runs).
- Enables `INTENA = SETCLR|INTEN|VERTB` (`$C020`).
- Allocates an Exec signal (`AllocSignal`/`FindTask`) and the main loop sleeps in `Wait()`;
  the level-3 handler increments a counter and `Signal()`s the task on every VBlank.
- Does **not** take over the AmigaOS display: it is an overlay-only demo that draws through
  the WinUAE debugger trap at `$F0FF60` (`jsr $f0ff60`). DMACON/bitplanes are not used by it.

Symptom: the program reaches "ready", renders frame 0, and then the per-frame counter
stalls. Built at `-O0` it advances ~7-13 frames (~1 fps) and then the CPU is found stuck in
ROM (below). At `-O2` it dies earlier (separate compiler issue).

## Observation 1 — ROM clears VERTB in INTENA while our vector is installed

A hardware watchpoint on INTENA (`$DFF09A`, CPU writes) caught this instruction executing:

```text
$FCD5AC: MOVE.W #$0020,(a2,$009a)   ; a2=$00DFF000 -> INTENA, bit15=0 => CLEAR VERTB
```

Context around it (ROM):

```text
$FCD5A0: AND.L #$ff,d0
$FCD5A6: MOVE.L #$a,d1
$FCD5A8: CMP.L d0,d1
$FCD5AA: BGT.B ...
$FCD5AC: MOVE.W #$0020,(a2,$009a)   ; clear VERTB
$FCD5B2: MOVE.L a1,(a3,$0022)
$FCD5B6: BEQ.B ...
$FCD5B8: MOVE.W (a1,$0010),(a3,$009e)
$FCD5BE: MOVEA.L (a1,$0004),a0
$FCD5C2: MOVE.L (a0,$0004),(a3,$0032)
...
$FCD5D2: JSR.L $00FCD4CC
```

At the stop, the stack contained this (all-ROM) caller chain: **`$00FC63D4` ← `$00FE0496`
← `$00FDC7AC`**:

```text
$FC63CC: MOVE.L a1,-(a7)
$FC63CE: JSR.L $00FCD564
$FC63D4: ADDA.L #$4,a7
$FC63D6: RTS

$FE0488: MOVE.L a6,-(a7)
$FE048A: MOVEA.L (a6,$0064),a6
$FE048E: MOVEA.L (a7,$0008),a1
$FE0492: JSR.L (a6,-$00DE)
$FE0496: MOVEA.L (a7)+,a6
$FE0498: RTS

$FDC798: PEA.L (a2,$0022)
$FDC79C: JSR.L $00FE0460
$FDC7A2: PEA.L (a2,$0022)
$FDC7A6: JSR.L $00FE0488
$FDC7AC: JSR.L $00FE04F4
$FDC7B2: MOVE.W #$8100,$00DFF096   ; DMACON: SETCLR|BPLEN
$FDC7BA: MOVE.W #$8020,$00DFF096   ; DMACON: SETCLR|SPREN
```

At the watchpoint hit: `D7` = our program's Task pointer (`$00C06730`), `A6` = `$00FF4128`
(SysBase); other registers looked like leftovers from our program's context.

INTENA read right after: `$606C` (INTEN|EXTER|BLIT|PORTS|SOFT — **VERTB cleared**); in another
run: `$536C` (VERTB set, **EXTER cleared**). INTREQR was `$1000` (DSKSYN) with no VERTB pending.

## Observation 2 — CPU stuck in a ROM trampoline at $FC08E6 with a5=0

Later, paused state: `PC=$00FC08E6`, `SR=$2508` (supervisor, **IPL=5**), `A5=0`, `A6=$00C00276`,
`A7=$00C37420` (SSP).

```text
$FC08E6: ORSR.W #$2000            ; enter supervisor
$FC08EA: PEA.L $00FC08F4
$FC08F0: MVSR2.W -(a7)            ; push SR
$FC08F2: JMP.L (a5)               ; a5 == 0 !!
$FC08F4: RTS

$FC08F6: ORSR.W #$2000
$FC08FA: SUBA.L #$8,a7
$FC08FC: MVSR2.W (a7)
$FC08FE: MOVE.L #$00FC08F4,(a7,$0002)
$FC0906: MOVE.W #$0020,(a7,$0006)
$FC090C: JMP.L (a5)
$FC090E: CMP.L #$00FC08E6,(a7,$0002)
$FC0916: BEQ.B ...
$FC0918: CMP.L #$00FC08F6,(a7,$0002)
```

The supervisor stack at that stop contained `$00FC1F4C`, `$00C0D936`, `$00FF4128` (SysBase),
`$00000005`, `$80000000`, `$00C19AF0`, `$00C0DD68`, ... Disassembly at the ROM frame
`$FC1F4C`:

```text
$FC1F4C: MOVEA.L a0,a5
$FC1F4E: MOVEA.L (a6,$0114),a1      ; ExecBase+$114 = ThisTask -> $00C06730 (our Task)
$FC1F52: MOVE.L (a1,$0016),d0
$FC1F56: MOVE.L (a1,$001a),d1
$FC1F5A: AND.L d0,d1                ; tc_SigWait & tc_SigRecvd
$FC1F5C: BEQ.B ...
```

The TRAP #0..#15 vectors (`$80`..`$BC`) all point into `$00FC0836`..`$00FC0854`
(2 bytes apart).

## Questions

1. Can you identify these KS 1.3 ROM routines (from the public Kickstart 1.3
   disassembly/source), with names:
   - `$FCD564` (the routine containing the `INTENA=#$0020` VERTB clear at `$FCD5AC`);
   - `$FC63CC` (a 2-instruction stub that `JSR`s `$FCD564`);
   - `$FE0488` (a stub that does `JSR (a6,-$DE)` through a library base);
   - `$FDC780`..`$FDC7AC` (which ends re-enabling DMACON `$8100`/`$8020`);
   - `$FC08E6` (the `ORSR #$2000; PEA; MVSR2; JMP (a5)` trampoline) and `$FC1F4C`
     (reads ThisTask, then `tc_SigWait & tc_SigRecvd`);
   - what is at the TRAP vectors `$FC0836`..`$FC0854` (2-byte spaced entries)?
2. Given (1): which of these is the **interrupt-enable bookkeeping** that would recompute
   INTENA and thereby clear VERTB (e.g. exec's `SetIntVector`/`AddIntServer`/`RemIntServer`,
   or an OS display/window teardown path)? Is it expected that a normal CLI process which
   replaced vector `$6C` and enabled VERTB has its enable silently cleared by exec later?
3. What is the KS 1.3 mechanism at `$FC08E6`? What is `a5` supposed to hold (tc_TrapCode /
   tc_ExceptCode)? What does KS 1.3 do when a **user task executes a TRAP #n with
   tc_TrapCode = 0** — guru, task kill, or a spin? Could the observed state
   (`PC=$FC08E6`, `a5=0`, `SR` IPL=5) be an **exception storm** (e.g. `JMP (a5)` to 0 ->
   address error -> ROM handler -> back to the dispatcher)?
4. Most likely sequence: (i) our process takes an exception/TRAP for some reason, KS kills
   it / spins in the dispatcher, and the OS's subsequent bookkeeping clears VERTB
   (observation 1 = consequence); or (ii) exec's bookkeeping clears VERTB first (killing our
   VBlank IRQ), and the ROM trap state is a separate red herring?

## Already ruled out (please don't re-suggest)

- WinUAE's INTENA/INTREQ semantics: `custom.cpp:3377` (`INTENA()` setclr), `custom.cpp:2308`
  (`INTENAR()` read mask), `custom.cpp:3214-3242` (`intlev()` needs `intreq & intena`),
  `cia.cpp:287-288` (CIA-B -> EXTER), `cia.cpp:378,587` (Timer B INMODE=00 counts E clock).
- Code corruption: the runtime bytes at the fault PCs match the ELF byte-for-byte and are
  legal instructions.
- The per-frame `probe_when_ready` / `eng_debug_ready_probe` "ready probe": it is a plain
  GDB breakpoint in the runner; disabling the call does not prevent the stall.
- Pure optimization artifact: the stall persists with everything built at `-O0`. (At `-O2`
  there is also an early crash where the inlined `probe_when_ready` tail uses a garbage
  pointer — a separate gcc 15 m68k issue we track locally.)
- Our own re-arm attempts: a CIA-B Timer B (level 6/EXTER) watchdog plus a VERTB re-arm
  check; the OS's INTENA rewrites also clear EXTER, so the watchdog stops; VERTB is
  sometimes re-enabled by the OS's own rewrite.

If you can, cite the Kickstart 1.3 disassembly listing (or exec 1.3 source) you use for the
addresses.
