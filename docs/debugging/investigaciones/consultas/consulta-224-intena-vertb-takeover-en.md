# Consultation (English): KS 1.3 keeps clearing VERTB in INTENA under a live AmigaDOS process — why, and what is the correct takeover/restore sequence?

> Ask Grok in English (per `AGENTS.md` §1.3). Self-contained follow-up to `consulta-ks13-trap-vertb-en.md` and `consulta-ks13-trap-vertb-seguimiento-en.md`. Demo: `demos/techniques/amiga/sprites/224_sprite_reuse_carousel` (A500 OCS PAL, 320x256, 5 bitplanes, 8 sprite channels reused 5 times vertically). Emulator: WinUAE-DBG (BartmanAbyss fork), cycle-exact A500, Kickstart 1.3, `quickstart=a500,1` (512K chip + 512K slow), `cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`, `ntsc=false`.

## Context: how the program runs

The runner boots KS 1.3 and executes this startup-sequence:

```
stack 131072
cd dh1:
:a.exe
```

So `a.exe` is a **normal AmigaDOS process**: the Shell parent is alive and blocked waiting for the child, and exec/intuition/graphics/dos are still resident. The program is not a bootblock/trackloader; it is loaded by dos.library and then tries to take over the display.

Engine bootstrap (`eng::amiga::run_demo`):

1. `ensure_boot()` (SysBase from `*(4)`), `configure_memory`.
2. `app.set_display(display)` + `app.start()`; the demo's `init` reserves Chip RAM and calls `app.device().takeover_copper(m_lists[0].mem_view())`.
3. `AmigaBackend::takeover_display(list)`:
   - waits until `(VPOSR & 0x1ff00) == (311 << 8)`;
   - `INTENA = 0x7FFF` (clear all) and `INTREQ = 0x7FFF`;
   - `wait_blitter()`;
   - `DMACON = 0x7FFF` (clear all);
   - `COPCON = 0x0002` (CDANG);
   - `COP1LC = list`;
   - `DMACON = SETCLR | MASTER | COPPER`, then `COPJMP1 = 0x7fff`.
4. `Engine::run_frames` then installs a VBlank service: `set_vblank_service(&VBlankHeartbeat::run, hb)` -> `level3_sync()`:
   - replaces the level-3 autovector (`$6C`) with `level3_irq`, saving the ROM vector;
   - `INTENA = SETCLR | INTEN | VERTB` (`0xC020`).
5. Per frame the engine waits with `wait_vblank_run()`:
   - at entry, if `g_vbl_task != nullptr && (INTENA & 0x20) == 0`, it calls `level3_sync()` again (re-arm);
   - then polls `VPOSR` in a tight loop until `(VPOSR & 0x1ff00) == (311 << 8)`;
   - then `debug_stop_idle()` and returns; the loop calls `update()`/`render()`.

The demo itself uses `VblankWait::Active` in this failing case: the loop is **not** sleeping in `Wait()`; it polls VPOSR and relies on the level-3 IRQ for the per-frame heartbeat counter (`hb.frames`). The frame is advanced only when the IRQ increments that counter.

### The system takeover code exists, but is never called by run_demo

`AmigaBackend` has both a system takeover and a restore path:

```cpp
bool AmigaBackend::takeover_system() {
    if (g_sys.active) return true;
    Forbid();                                  // 1) freeze the scheduler
    g_sys.forbidden = true;
    GfxBase = OpenLibrary("graphics.library", 0);
    if (GfxBase) {
        g_sys.old_view = GfxBase->ActiView;
        LoadView(nullptr);                     // 2) kill the Intuition display
        WaitTOF(); WaitTOF();
        OwnBlitter(); WaitBlit();
    }
    g_sys.old_dmacon = INTENA_READ | 0x8000;   // 3) save hardware state
    g_sys.old_intena = INTENA_READ | 0x8000;
    g_sys.old_adkcon = ADKCON_READ | 0x8000;
    g_sys.active = true;
    return true;
}

void AmigaBackend::restore_system() {
    // 1) wait_blitter; DMACON/INTENA/INTREQ/ADKCON = 0x7FFF
    // 2) restore level-3 vector; g_vbl_task = nullptr; g_blit_task = nullptr
    // 3) COP1LC = GfxBase->copinit; COPJMP1; restore old DMACON/INTENA/ADKCON
    // 4) DisownBlitter; LoadView(old_view); WaitTOF x2; CloseLibrary; Permit()
}
```

Only demos `066_bob_showcase` and `119_proc_world` call `takeover_system()` manually. **`run_demo_impl` never calls it**, and this demo does not either: it only calls `takeover_display` (display registers), then the engine enables VERTB. The `restore_system()` exit path is also never exercised here.

## Symptom

With 5 bitplanes and five prebuilt copperlists rotated by a COP1LC swap every 50 frames, the demo freezes at run-status frame **198-200** (about 4 s). Everything else keeps running: DMACON is `0x03A0` (DMAEN/BPLEN/COPEN/SPREN, no BLTEN), BPLCON0 is `0x5200` (5 planes + color, LPEN=0), COP1LC points at a valid list, the Copper/DMA state looks normal. The frame counter stops, the run-status heartbeat stops, and the CPU is found in `wait_vblank_run`'s VPOSR loop.

Observed at the stop:

- `INTENAR = 0x2000` (bit 14 INTEN=0, bit 13 EXTER=1) or `0x7001`; the **VERTB bit (`0x20`) is clear**.
- `INTREQR = 0x0020` (VERTB pending).
- `g_vbl_task` is non-null (`0x00C15D30` / `0x00C15D62` in different builds), `g_vbl_task_user` is a valid stack address, `g_level3_installed = 1`.
- `g_vblank_seq` is stuck at ~199-201 (one more than the frozen frame).
- The CPU is executing (PC changes, `A7`/registers evolve), and VPOSR sometimes reads live values (e.g. VPOS=291, HPOS=0xD1); on other samples the same register reads a fixed `0xA0000502` (VPOS=5, HPOS=2).
- PC is inside `AmigaBackend::wait_vblank_run` (`$DFF004` polling loop, compare `#0x13700`).

### Related prior evidence (same KS 1.3 investigation)

A hardware watchpoint on `INTENA` previously caught KS 1.3 ROM code clearing VERTB while a process-installed level-3 vector was active:

```
$FCD5AC: MOVE.W #$0020,(a2,$009A)   ; a2 = $DFF000 -> INTENA, bit15=0 => CLEAR VERTB
```

with a ROM caller chain `$FC63D4 <- $FE0496 <- $FDC7AC`; right after, the code re-enables DMACON `$8100`/`$8020`. A follow-up consultation (still open) analyzed the KS 1.3 supervisor-dispatch table at `SysBase-$36` (`$C00240`) and a crash where the ROM trampoline at `$FC08E6` was reached with `a5=0` from a `Wait()` path; entries #0/#1 force `INTENA=0xC000`/use `INTREQ=0x8004` for Disable/Enable bookkeeping. The exact mechanism that rewrites INTENA (exec Disable/Enable, Wait, graphics.library display bookkeeping, or something else) was not conclusively named.

## What we tested (A/B, all one variable)

1. **Rotation disabled** (`K224_ROTATE_ART=0`, only one copperlist, no COP1LC swap): stable 20 s, 49.97 fps, 141,962 cycles/frame, 1.001 fields/frame.
2. **Five prebuilt lists + COP1LC swap every 50 frames**: deterministic freeze at frame 198-200, on every restart, with 5 planes.
3. **Same-address COP1LC commit** (commit the currently active list every 50 frames): also freezes at 198-200. So the contents of the target list are irrelevant; the periodic COP1LC write/timing (or the associated extra CPU work) is enough.
4. **3 planes + rotation**: ran 20 s at 49.92-49.97 fps in the successful runs, i.e. the freeze is not guaranteed by rotation alone; it is timing/load dependent.
5. **Movement of the COP1LC publication**: originally the commit was at the end of `render()` (after patching 160 words, i.e. several microseconds after waking at VPOS=311, landing at VPOS 0-2). We moved it to the start of `render()` (at VPOS=311, one full scanline of margin before the wrap) and patched the now-inactive next list afterwards. The Active freeze persisted (one run even hung in `takeover_display`'s VPOS=311 wait). So the documented COP1LC wrap race (`WinUAE-DBG/custom.cpp:2921-2942`, `copper.md` §3) is not the root cause, though publishing with margin is still the right thing to do.
6. **Memory-overlap audit** (the original hypothesis was that the engine was overwriting reserved blocks): live register pointers at the freeze gave planes `0x15070..0x21870` (5 x 0x2800), sprite sheets `0x2394C..0x245F0`, copperlists `0x258A0..0x260A0`; **no overlap**. `BlockPool` is first-fit, aligns each reservation, and returns invalid on no space. No evidence of memory corruption.
7. **Tooling/debugger**: the freeze reproduces with a GDB client, without GDB, with the side channel only, and with no MCP session at all. So it is not a GDB halt. (Separately, WinUAE's lightpen latch can freeze VPOSR reads: `GETVPOS()` returns `vpos_lpen` while `lightpen_triggered` is set, `custom.cpp:2356-2364, 3613-3636`; an absolute-mouse sample moved the latched value from VPOS 5 to 185. That is a real emulator trap but not the primary cause here, because the demo also freezes with no input/tooling. It is still worth noting for any absolute-pointer use under WinUAE.)
8. **Polling fix (works)**: switching the demo to `VblankWait::Polling` (`run_frames_polling`, `wait_vblank()` polling VPOSR, no dependency on the VERTB IRQ heartbeat) makes 5 planes run past frame 950; the canonical measurement gives 49.92 fps, 142,102 cycles/frame, 1.002 fields/frame, 1013 frames over 20.3 s, stable. The same build at 3 planes gives 49.97 fps, 141,962 cycles/frame, 1.001 fields, stable. Rendering, Copper, sprite DMA, the COP1LC swap and the memory layout are therefore healthy; the failure is in the VERTB heartbeat/INTENA ownership.

## Questions

1. **Confirm or correct the diagnosis.** Under a live AmigaDOS process on KS 1.3, what exactly clears the VERTB bit in `INTENA` after 3-4 s, even though the process replaced the level-3 autovector and set `INTENA = SETCLR|INTEN|VERTB`? Is it exec's Disable/Enable bookkeeping, the `Wait()`/`Signal()` path, a task switch restoring an `INTENA` shadow, graphics.library's display server, or the ROM routine at `$FCD5AC`? The process is blocked in `Wait()` only in other demos; this one polls VPOSR and never sleeps, yet the same thing happens. What is the precise sequence in KS 1.3 (with `SysBase+$124/$126/$127` if relevant)?

2. **Why does the re-arm not save it?** `wait_vblank_run` re-arms `INTENA` at every call when VERTB is off, and the CPU is in the VPOSR loop. If the beam reaches 311, the loop returns, the engine calls `wait_vblank_run` again, and the re-arm rewrites `INTENA = 0xC020`. Why is VERTB still lost? Is the OS clearing INTENA again between our write and the next VERTB (i.e. every field/VBlank, or on every task switch), so the interrupt never asserts? Or is the CPU not returning because VPOSR is latched/frozen (lightpen or another Agnus state)? What would be the correct observation to distinguish "OS clears INTENA at every VBlank" from "Agnus stops generating VERTB"?

3. **Is `takeover_system()`/`restore_system()` the right fix, and is its order correct?** Since it exists but `run_demo` never calls it: should `run_demo_impl` call `backend.takeover_system()` before `app.set_display/start` (and restore at exit), or should each demo opt in? Is `Forbid()` alone sufficient to stop the INTENA rewrites, or is `Disable()`/`Enable()` or a full `graphics.library` takeover (`LoadView(NULL)` + `OwnBlitter`) required? Does `Forbid()` prevent a blocked Shell parent from being switched in? Is `restore_system()`'s order safe (clear DMA/IRQ, restore vector, `copinit`, restore INTENA/DMACON/ADKCON, `DisownBlitter`, `LoadView(old)`, `WaitTOF` x2, `CloseLibrary`, `Permit`), and what should be added for KS 1.3?

4. **Supported alternative?** Instead of swapping the raw level-3 vector and writing INTENA, should the engine use `AddIntServer(INTB_VERTB, ...)` (and `RemIntServer` on exit) so exec's bookkeeping knows about our server? If we keep the raw vector, is there any supported way to keep VERTB enabled while AmigaDOS is alive? Does `Forbid()` + `OwnBlitter()` + raw vector work on KS 1.3, or is it inherently racy? What do classic KS 1.3 trackloaders/demos do when launched from the Workbench/CLI and later return?

5. **Engine design recommendation.** Given the current engine, what is the minimal, safest change that makes `run_demo` demos robust on KS 1.3: (a) call `takeover_system()` in the bootstrap; (b) keep the current display takeover but add `Forbid()` and a periodic/reliable VERTB re-arm; (c) use `AddIntServer` and let exec own INTENA; or (d) keep `VblankWait::Polling` as the default for runner-launched demos and reserve Active for bare-metal/trackloader builds? We need 50 fps, no tearing, and the runner's READY/GDB/screenshot workflow must keep working.

6. **Exit path.** If a demo ever returns to the runner/Workbench, what is the complete, correct restore sequence on KS 1.3 (display, copper, blitter, INTENA, vectors, `Permit`), and what are the common ways to deadlock or guru in that return (e.g. `LoadView` while DMA is still active, closing graphics.library with the Blitter owned, restoring INTENA with VERTB pending)? We have `restore_system()` implemented but untested in a demo; please review its order and list what it is missing.

## Already ruled out (please don't re-suggest)

- Memory overlap/corruption in the engine's Chip reservations (planes, sprite sheets, copperlists audited; no overlap; `BlockPool` bounds/aligned first-fit).
- The COP1LC wrap race: moving the swap from post-wrap to VPOS=311 with a full scanline of margin did not change the Active freeze.
- Pure tooling/GDB interference: freeze reproduces with no GDB client and no MCP session.
- Pure lightpen latch: it can move VPOSR, but the freeze occurs without any pointer input; treat it as an aggravating WinUAE trap, not the root cause.
- Display/Copper/DMA setup: DMACON=0x03A0, BPLCON0=0x5200, valid COP1LC; rendering and sprite DMA are correct with Polling at 50 fps.
- Code corruption: the runtime bytes match the ELF; the fault PC is a legal `move.l $DFF004,d0` in `wait_vblank_run`.
- `Wait()`/signal bit 31 (this demo does not call `Wait()`; it polls VPOSR). The earlier `$FC08E6`/`SysBase-$36` crash was on a `Wait()` path and is a separate signature.

## Grok answer (received) and triage

Grok confirmed the diagnosis: VERTB is a **server-chain** interrupt owned by exec (`AddIntServer`/`RemIntServer`); replacing the raw `$6C` vector and writing `INTENA` fights KS 1.3, and graphics.library/exec clear VERTB on display bookkeeping and Disable/Enable paths. `Forbid()` alone is not enough: the display takeover (`LoadView(NULL)` + `OwnBlitter`) is also required. The re-arm loses because the OS can clear `INTENA` after our write and before the next VBlank, so the sequence counter never advances and the VPOS loop never returns. The classic `takeover_system()`/`restore_system()` pair is the correct sequence; Grok recommends calling the takeover once in the common bootstrap before `set_display`, restoring on exit. The supported, race-free alternative for a well-behaved process is `AddIntServer(INTB_VERTB, ...)` (handler must return with the Z flag set) plus `RemIntServer`. Raw vector + `Forbid` + `OwnBlitter` works for classic demos but remains racy if any other task/display server wakes. Grok also confirmed the restore order (wait blitter, clear DMA/IRQ, restore vector, `copinit`, restore saved regs, `DisownBlitter`, `LoadView(old)`, 2×`WaitTOF`, `CloseLibrary`, `Permit`) and advised clearing `INTREQ` before restoring `INTENA`; keep `Polling` as a robust fallback and do not rely on periodic re-arm without the takeover.

Applied in this repo: `run_demo_impl` calls `backend.takeover_system()` before `set_display` and installs a `scope_guard` with `restore_system()`; the takeover is limited to `VblankWait::Active`/`Polling` because `Wait()` with `Forbid` active can deadlock (`Signal` remains as before). The engine VBlank/BLIT service now uses `AddIntServer(INTB_VERTB/BLIT)` under AmigaDOS (ASM stubs returning with Z set), keeping the raw vector for bare-metal. With that path, 3 and 5 planes run 20 s stably with `Active` (49.87–49.92 fps, 1 field). The 4-plane build still stops at ~frame 1150 with `INTENAR=0x77FF`, `INTREQR=0x20` pending and the CPU in the sequence loop; it is not isolated (server chain, debugger/session interference, or WinUAE's lightpen latch, which can freeze VPOSR). The demo therefore stays on `Polling` (all four profiles: 49.87–49.92 fps, 1.001–1.003 fields, 20 s stable). `measure-fps` now reads the cycle counter through the side channel (GDB reads can perturb the target) and has a `--require-stable` exit gate plus an INTENA/INTREQ diagnostic on stalls. The `Active`+`AddIntServer` path remains experimental until the 4-plane late freeze is closed.
