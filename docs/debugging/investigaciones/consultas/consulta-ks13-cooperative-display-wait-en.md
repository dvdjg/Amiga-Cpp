# Follow-up (English): KS 1.3 — cooperative VBlank message loop (`Wait`) vs exclusive display takeover (`Forbid`); the exec VERTB chain stops after ~20 frames

> Ask Grok in English (per `AGENTS.md` §1.3). Self-contained follow-up to `consulta-ks13-trap-vertb-en.md`, `consulta-ks13-trap-vertb-seguimiento-en.md` and `consulta-224-intena-vertb-takeover-en.md`. Emulator: WinUAE-DBG fork, cycle-exact A500, Kickstart 1.3, `quickstart=a500,1`, `ntsc=false`. The program is a normal AmigaDOS process launched from the startup-sequence (`:a.exe`); the Shell is alive.

## Two modes, and the conflict

We want (1) a **message-driven** game loop: the engine registers a VBlank server, posts a `MsgType::VBlank` (or raises an Exec signal) every frame, and the main loop sleeps in `Wait()`/`WaitPort` instead of polling; and (2) a **custom fullscreen display** (own Copper list, sprites, DMA). On KS 1.3 we hit a fundamental conflict.

**Exclusive mode (works, but cannot sleep).** `takeover_system()`: `Forbid()` + `OpenLibrary(graphics)` + `LoadView(NULL)` + 2x `WaitTOF()` + `OwnBlitter()` + `WaitBlit()`; save DMACON/INTENA/ADKCON; the demo takes the Copper (`takeover_display`: clear INTENA/DMACON, set COP1LC, COPJMP1). The engine installs VBlank/BLIT with `AddIntServer(INTB_VERTB/BLIT)` and it is the owner. With `VblankWait::Active` (VPOSR poll, not `Wait`) it runs 35 s at ~50 fps without any debugger attached; the diagnostic counters show `g_irq_diag_vbl_adds=1`, `g_irq_diag_vbl_calls≈frames`, `g_vblank_seq` advancing. `Wait()` under `Forbid` is illegal (RKM), so this mode can only poll/busy-wait.

**Cooperative mode (fails).** No `Forbid()`, but the same `LoadView(NULL)` + 2x `WaitTOF()` + `OwnBlitter()`/`WaitBlit()`; `AddIntServer(INTB_VERTB)`; the demo takes the Copper; the main loop arms an Exec signal (`AllocSignal`) and sleeps in `Wait(1<<sig)`; the VBlank server calls the heartbeat and raises the signal; **we do NOT write INTENA at all** (exec owns it). The mode gets a few frames and stops:

- READY at frame ~18; the run-status frame stays 18/19/26 (depending on the run), no more updates.
- Diagnostic counters (read through the side channel, **no GDB attached**): `g_irq_diag_vbl_adds = 0x2001` (one AddIntServer), `g_irq_diag_vbl_calls = 0x1015` (21 calls), `g_irq_diag_seq_value = 0x15` (21): the server was called 21 times, then never again.
- At the stop: `INTENAR = 0x602C` (bit 14 INTEN=1, bit 5 VERTB=1), `INTREQR = 0x1060` (bit 12 DSKSYN, bit 6 BLIT, bit 5 VERTB pending), `SR = 0x0004` (IPL 0), `PC = 0x00FDFF76` (ROM). VERTB is **enabled and pending**, but the level-3 chain no longer runs our server.
- We also tried: writing `INTENA=SETCLR|INTEN|VERTB` after `AddIntServer` (26 frames then stop); clearing INTENA in the display takeover (18 frames); VERTB server priority 100 (no change). The freeze reproduces with the side channel only (no GDB), so it is not the debugger.
- The cooperative build is otherwise the same demo that works in exclusive mode at 50 fps; only the loop/wait and the no-Forbid/no-INTENA changes differ.

## Questions

1. **What exactly stops the server chain at ~21 frames?** With `INTENAR` showing VERTB enabled and `INTREQR` showing VERTB pending, why does the CPU (IPL 0) not dispatch the level-3 chain? Is `graphics.library`/Intuition reclaiming the level-3 vector with `SetIntVector` (bypassing the exec server chain) when we call `LoadView(NULL)`/`OwnBlitter` **without** `Forbid`? Does `LoadView(NULL)` without Forbid leave a display/bookkeeping task that later restores its own VBlank vector/handler and removes our chain? Is AddIntServer being undone, or is the vector replaced? Which ROM routine (we saw `$FCD5AC` clear VERTB in the 212 investigation; here VERTB stays enabled but is pending) would do this?
2. **Is `LoadView(NULL)` + `OwnBlitter()` without `Forbid()` a supported/legal sequence?** What is the canonical order for a cooperative process: AddIntServer before or after LoadView/WaitTOF/OwnBlitter? Must we avoid `LoadView`/`OwnBlitter` entirely if we want to keep exec's VBlank chain and `Wait()`? Is `OwnBlitter` compatible with the OS still running? Do we need `Disable()`/`Enable()` around the server installation, or a `SetIntVector`-based path instead of AddIntServer?
3. **Can a KS 1.3 process have both**: a custom fullscreen display (own Copper/DMA) **and** a `Wait()`/`WaitPort()` message loop driven by VBlank? Or are the only valid combinations (a) exclusive display + `Forbid` + polling/active (no `Wait`), and (b) OS display/Intuition window + `AddIntServer` + `Wait`/messages? What do classic KS 1.3 games and demos do when they want a fullscreen custom display and still return to the OS / use DOS? Are there known references (RKM, Aminet, source code) for this pattern?
4. **If cooperative fullscreen is impossible**, what is the recommended high-level engine API? We want to hide the low-level choice from the developer: two explicit modes, e.g. `Exclusive` (custom display, no Wait, poll/active) and `Cooperative` (OS display/window, VBlank messages, WaitPort), with the same game logic? Or is there a third pattern (a separate hardware task that does not `Forbid`, a message port, and a main task that waits) that gives both? Note `Forbid()` is global, so a hardware task holding it prevents the main task from running; is there any supported KS 1.3 way around that?
5. **Confirm the Forbid/Wait rule**: `Wait()`/`WaitPort()` with `Forbid()` active is illegal/deadlocks. Is `Disable()`/`Enable()` (interrupt disable) enough for the short critical sections (AddIntServer/LoadView) without forbidding tasks? Does `Disable()` around `LoadView`/`AddIntServer` prevent the display/exec bookkeeping from reclaiming the VBlank vector, while still allowing `Wait()` afterwards? What are the trade-offs?
6. **Detection/diagnosis in the engine**: how to distinguish, at runtime, "our server was removed/replaced" from "VERTB was disabled in INTENA" from "the CPU cannot take the IRQ"? We have `INTENAR`/`INTREQR` and our server call counters; is there an Exec/ROM field or a monitor technique to see which level-3 vector/handler is installed and whether the server node is still on the chain? What should the engine check before declaring the cooperative mode usable?

## Already ruled out (please don't re-suggest)

- GDB/debugger perturbation: the cooperative freeze reproduces with the side channel only, no client attached.
- Memory overlap/corruption (ranges audited; the exclusive build with the same allocations is stable).
- VERTB disabled in INTENA: `INTENAR=0x602C` has VERTB on; `INTREQR=0x1060` has VERTB pending.
- Server priority/ordering: priority 100 tried; the server is called 21 times before stopping.
- Writing INTENA manually: we tried not writing it, writing it after AddIntServer, and clearing it in the takeover; all fail.
- A single missing `Wait` signal: the server call counter stops at 21, so the server itself is no longer invoked.
- The exclusive path with `Forbid` + AddIntServer + Active works (35 s, 50 fps, no GDB).
