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

## Addendum (2026-10-22): clean cooperative without Copper still stops after one VBlank

After the two-mode answer, we tested the truly clean cooperative path on a **CLI/overlay** app (demo 212): **no** `LoadView`, **no** `OwnBlitter`, **no** Copper/DMACON writes, no display takeover, no keyboard/CD32 (only a joystick poll in the heartbeat). The app only calls `AddIntServer(INTB_VERTB)`, the engine raises an Exec signal in the server, and the main task sleeps in `Wait()`. It does **not** reach frame 60; the run-status stays at frame 0 with `detail=0x10010000` (diagnostic pack: `g_irq_diag_vbl_calls` low word = 0x1001, i.e. **one** server call, frame = 0). The first frame is delivered, then the server is never called again. This is a different failure from the 21-frame one: there is no Copper takeover at all.

The engine does not write `INTENA` to install the server; it only **sets** VERTB (`INTENA = SETCLR|VERTB`) if `INTENAR` shows it off before a wait. The app runs from the AmigaDOS CLI with only the host debug overlay; there is no Intuition screen/window opened by the app.

New questions for this clean case:

1. In a KS 1.3 **CLI** process (no Intuition screen/window opened by the app), is VERTB normally enabled/kept enabled by graphics/Intuition? If `AddIntServer` is not the first server, it does not enable the interrupt; does exec's saved `INTENA`/`Wait`/`Switch` bookkeeping turn VERTB off after the first frame because the OS screen is static? How can a CLI process keep VERTB enabled while sleeping in `Wait()` without taking the display?
2. Does the app need to open a minimal Intuition screen/window (or call `WaitTOF`) so graphics/Intuition keeps the VBlank server/interrupt alive? Is that the canonical cooperative pattern, or is a CLI + host overlay + `Wait()` impossible?
3. If `AddIntServer` is not enough in this environment, is `SetIntVector`/`Disable`+`Enable` or an Exec VBlank server installed before graphics the supported alternative? What is the minimal supported app that gets a VBlank signal while the OS/display stays alive?


## Addendum 2 (2026-10-22): checklist fixes applied; still one call

We applied the checklist from the last answer: the server is an ASM stub that saves D2-D7/A2-A6, calls the C body, restores them, sets **A0=**, then MOVEQ #0,D0; RTS (no RTE); the VERTB server priority was tried at **100** and at **-60**; the cooperative path does **not** replace the vector or touch the Copper, and only sets INTENA = SETCLR|INTEN|VERTB if either bit is missing before a wait. The signal is AllocSignal(-1) + FindTask(NULL) + Signal(task, 1<<sig) from the server; the main task waits on the same bit. Result is still exactly **one** server call (frame 0, g_irq_diag_vbl_calls=0x1001); the first frame is delivered, then silence. Same with input (keyboard/CD32) enabled or disabled.

Remaining questions: how can we inspect, without GDB, whether the node is still on the VERTB chain and whether the chain walker is reaching it? Could the graphics/Intuition server after ours still be breaking the chain despite A0/Z? Is Signal() every VBlank in the server compatible with the RKM CLI pattern, or does it trigger a Wait/Switch that restores INTENA and disables VERTB? What is the exact minimal KS 1.3 server+main loop (priority, A0, Z, Signal, INTENA) that increments a counter **and** signals a sleeping task every frame from a CLI?


## Addendum 3 (2026-10-22): counter-only VERTB probe also stops after two calls

We installed a minimal VERTB server in the **polling** 212 path: static struct Interrupt, priority -60, the ASM stub (A0=, Z, RTS), body only increments a counter (**no Signal, no INTENA, no INTREQ, no Wait path**). The runner screenshot 0.5 s after READY shows bl: 0x1002 (two calls) and rames: 0; at 50 Hz it should be ~25 calls. So the chain/VERTB is stopping after two fields even without the cooperative Wait/Signal. This changes the diagnosis: the single-shot is not explained only by the Wait/INTENA re-arm; the node/body/chain/OS side must be inspected. Next: a **standalone** probe (no engine/demo) with a breakpoint on the server and a watchpoint on INTENA/the level-3 vector, plus side-channel PC/INTENAR/INTREQR at the stall; then profile.


## Resolution (2026-10-22): local HUD overflow, not the OS chain

The cooperative chain was never the problem. Minimal on-target probes reached 60 VBlanks in every layer: a direct counter-only server (006), the `AmigaBackend` cooperative service (007), the `Engine` (008), the `MessagePumpGame` (009), `os::init` with joystick/all input (010/011), tasks (012) and the demo-like overlay/input/timer (013). The real bug was the raw `char line[64]` HUD buffer with the local `append` helpers: the counter line is >64 bytes and overflowed the stack. Polling tolerated it; the cooperative loop corrupted the stack and stopped after the first frame. The 212 now composes the HUD with `eng::util::TextLine<160>` + `operator<<`/`hex`; it runs cooperative (frames 57, msgs 30, t 29, bg 29) with the OS display and no Copper takeover. No Intuition screen, no `WaitTOF`, no vector change, no `INTENA` write and no further Grok question are needed. The failed 21-frame and one-call runs were the same corruption path plus overlay variants, not evidence that CLI + `AddIntServer` + `Wait()` is impossible.
