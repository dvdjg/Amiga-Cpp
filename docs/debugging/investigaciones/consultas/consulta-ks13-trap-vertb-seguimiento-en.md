# Follow-up (English): KS 1.3 exec supervisor-dispatch table at SysBase-$36 — null code pointer in the `Wait()` path

> Follow-up to `consulta-ks13-trap-vertb-en.md` (same thread). Ask Grok in English. All addresses
> are Kickstart 1.3, cycle-exact A500 (WinUAE-DBG fork). New evidence since the first answer.

## What we verified from your first answer

- `$FC08E6` is the "run the code at (a5) in supervisor mode" trampoline (`ORSR #$2000; PEA; MVSR2;
  JMP (a5)`) — matches the AHRM's `Supervisor()` pattern (`lea GoAway(pc),a5` +
  `jsr _LVOSupervisor(a6)` "trap to code at (a5)", AHRM 3rd ed. lines 6689-6690). Confirmed.
- `*(4)` = `$00C00276` = **SysBase (ExecBase in RAM)**.
- `SysBase-$36` = `$00C00240` = a **RAM dispatch table of 6-byte `JMP` entries**. We disassembled
  its targets:

```text
$C00240: JMP $00FC0F1C   ; entry #0
$C00246: JMP $00FC1F74   ; entry #1
$C0024C: JMP $00FC0EC2   ; entry #2
$C00252: JMP $00FC0E9C   ; entry #3
$C00258: JMP $00FC08E6   ; entry #4 (the trampoline itself)
$C0025E: JMP ...

$FC0F1C: MV2SR.W #$2000          ; enter supervisor
         MOVE.L a5,-(a7)
         MVUSP2R.L a5            ; a5 = USP
         MOVEM.L #$fffe,-(a5)    ; save d0-d7/a0-a6 on the user stack
         MOVEA.L $0004.w,a6      ; a6 = SysBase
         MOVE.W (a6,$0126),d0 / MOVE.W #$ffff,(a6,$0126)
         MOVE.W #$c000,$00dff09a ; INTENA = SETCLR|INTEN   <-- forces INTEN!
         MOVE.L (a7)+,(a5,$0034) / MOVE.W (a7)+,-(a5)
$FC1F74: BSET.B #$7,(a6,$0124) / SNE.B d0 / TST.B (a6,$0127) / TST.B (a6,$0126)
         MOVE.W #$8004,$00dff09c ; INTREQ = SETCLR|SOFT  <-- Disable/Enable bookkeeping
         RTS
$FC0EC2: MOVEM.L #$c0c6,-(a7) / MV2SR.W #$2700 / BCLR.B #$7,(a6,$0124)
         MOVEA.L (a6,$0114),a1   ; ThisTask
         BTST.B #$5,(a1,$000e)   ; tc_Flags bit 5
         LEA (a6,$0196),a0 ...   ; list walk
$FC0E9C: BTST.B #$5,(a7,$0018) / MOVEA.L $0004.w,a6 / TST.B (a6,$0127)
         BTST.B #$7,(a6,$0124) / MV2SR.W #$2000 / ...
```

- The **normal** calls to `$FC08E6` arrive with **`a5=$00C00240`** (set by `LEA (a6,-$36),a5` at
  `$FC1F44`, a6 = SysBase), **user mode** (SR=$0010), returning to `$FC1F4C`; they happen during
  boot (disk operations). The routine at `$FC1F40`:

```text
$FC1F40: MOVE.L a1,(a0)
$FC1F42: MOVEA.L a5,a0            ; takes the incoming a5 as the "code pointer"
$FC1F44: LEA (a6,-$36),a5         ; a5 = dispatch table (SysBase-$36)
$FC1F48: JSR (a6,-$1e)            ; LVO -30
$FC1F4C: MOVEA.L a0,a5
$FC1F4E: MOVEA.L (a6,$0114),a1    ; ThisTask
$FC1F52: MOVE.L (a1,$0016),d0 / MOVE.L (a1,$001a),d1 / AND.L d0,d1   ; tc_SigWait & tc_SigRecvd
```

- **The crash call**: conditional breakpoint on `$FC08E6` with `A5==0` matched after 13 normal
  hits. State: `PC=$FC08E6`, `SR=$2508` (supervisor, IPL 5), `A5=0`, `A6=$00C00276` (SysBase),
  `A7=$00C37420` (SSP). Supervisor stack:

```text
$C37420: $00FC1F4C  <- return address (the routine above)
$C37424: $00C0D936  <- our function `vblank_signal_wait_next` (the Wait() path!)
$C37428: $00FF4128
$C3742C: $00000005
$C37430: $80000000  <- the Wait() argument (signal mask bit 31)
$C37434: $00C19AF0 / $00C0DD68 / $00C19B78 / $00C12762 / $00C11184 / $00C395A8 ...
```

- After `JMP (a5)` with a5=0: CPU executes the vector table as code; the exception vector table
  gets corrupted (observed `$10`=0, `$14`=$00C00276, `$18`=6), `A7` wraps (`$FFFFFFC8`), IPL 7 —
  unrecoverable storm. We also confirmed `ORSR` is legal on the 68000 (WinUAE `cpuemu_0.cpp`) and
  the `$F0FF60` debug trap is a plain `calltrap` JSR (`uaelib.cpp:470`).

## Questions

1. What is the **RAM table at `SysBase-$36`** in KS 1.3, and what are its entries? We read them as:
   #0 `$FC0F1C` (supervisor-call: `MV2SR`, save frame on USP, **`INTENA=0xC000`**),
   #1 `$FC1F74` (Disable/Enable bookkeeping via `INTREQ=0x8004` SOFT),
   #2 `$FC0EC2` (SR=0x2700, `ThisTask` `tc_Flags` bit 5, list at `SysBase+$196`),
   #3 `$FC0E9C` (mode switch, `MV2SR #$2000`), #4 `$FC08E6` (the trampoline).
   Are these the KS 1.3 "Supervisor/SuperState/UserState/Disable-Enable" entries? What is entry #4
   used for (why would anyone jump to the trampoline through the table)?
2. The caller at `$FC1F40-$FC1F4C` takes its **incoming `a5`** as the code pointer
   (`MOVEA.L a5,a0`) and then sets `a5 = SysBase-$36`. Which routine is it (name), and which
   call path reaches it **with `a5=0`**? Is `a5` on entry supposed to be the supervisor stub
   address (table base), a task field, or a caller-supplied value? In our crash the caller frame
   below it is our `Wait()` path (`vblank_signal_wait_next`), so **can `exec`'s `Wait()` end up in
   this routine with a null code pointer** (e.g. from `tc_Flags`/`SysBase` fields `+$124/$126/$127`
   or the task list at `SysBase+$196`)?
3. Given entry #0 forces `INTENA=0xC000` (INTEN on) and entry #1 uses the **SOFT interrupt**
   (`INTREQ=0x8004`) for Disable/Enable: **does exec rewrite `INTENA` from its own saved copy**
   (fields at `SysBase+$124/$126/$127`) on these paths, silently clearing a `VERTB` bit that a
   normal CLI process set by replacing the level-3 autovector? I.e., is the "VERTB cleared by ROM
   code" we captured the **exec Disable/Enable bookkeeping**, not graphics display bookkeeping?
   If so, what is the correct way for a process to keep VERTB enabled (`AddIntServer` only?), and
   is there any supported way to coexist with a raw vector swap?
4. Is `Wait(0x80000000)` (signal bit 31, allocated with `AllocSignal(-1)`) handled as a **signed**
   value anywhere in this dispatch path (`BPL/BMI` on masks)? We tested allocating a low bit (8..15)
   instead and the crash did not change, but we want to know whether bit 31 is reserved/unsafe.
5. When `tc_TrapCode`/`tc_ExceptCode` are null and a task takes a TRAP/exception, KS 1.3's default
   path — does it ever **jump to the trampoline with a5=0** (i.e., is our storm the expected
   signature of a null trap code), or does it take a different route (Alert/guru)?

Already ruled out (please don't re-suggest): code corruption (runtime == ELF), the per-frame
`probe_when_ready` (a plain GDB breakpoint; disabling it doesn't help), `ORSR` illegality on 68000
(legal in WinUAE's 68000 core), the `$F0FF60` trap (calltrap JSR), and the low-signal-bit change
(no effect). A separate gcc 15 m68k codegen bug at `-O2` (garbage pointer in an inlined tail) is
tracked locally and is not this crash.
