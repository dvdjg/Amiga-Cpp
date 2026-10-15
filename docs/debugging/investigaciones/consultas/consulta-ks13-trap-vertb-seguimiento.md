# Seguimiento: tabla de despacho de supervisor de exec (KS 1.3) en `SysBase-$36` — puntero de código nulo en la ruta de `Wait()`

> Seguimiento de [`consulta-ks13-trap-vertb-en.md`](consulta-ks13-trap-vertb-en.md) (mismo hilo).
> La versión que se envía a Grok es la inglesa:
> [`consulta-ks13-trap-vertb-seguimiento-en.md`](consulta-ks13-trap-vertb-seguimiento-en.md).
> Direcciones de Kickstart 1.3, A500 cycle-exact (fork WinUAE-DBG).

## Verificado de la primera respuesta

- `$FC08E6` es el trampolín "ejecuta el código de (a5) en supervisor" (`ORSR #$2000; PEA; MVSR2;
  JMP (a5)`) — coincide con el patrón de `Supervisor()` del AHRM (`lea GoAway(pc),a5` +
  `jsr _LVOSupervisor(a6)` "trap to code at (a5)", AHRM 3.ª, líneas 6689-6690). Confirmado.
- `*(4)` = `$00C00276` = **SysBase (ExecBase en RAM)**.
- `SysBase-$36` = `$00C00240` = **tabla de despacho en RAM con entradas `JMP` de 6 bytes**.
  Desensambladas sus entradas:

```text
$C00240: JMP $00FC0F1C   ; entrada #0
$C00246: JMP $00FC1F74   ; entrada #1
$C0024C: JMP $00FC0EC2   ; entrada #2
$C00252: JMP $00FC0E9C   ; entrada #3
$C00258: JMP $00FC08E6   ; entrada #4 (el propio trampolín)
$C0025E: JMP ...

$FC0F1C: MV2SR.W #$2000          ; entra en supervisor
         MOVE.L a5,-(a7)
         MVUSP2R.L a5            ; a5 = USP
         MOVEM.L #$fffe,-(a5)    ; guarda d0-d7/a0-a6 en la pila de usuario
         MOVEA.L $0004.w,a6      ; a6 = SysBase
         MOVE.W (a6,$0126),d0 / MOVE.W #$ffff,(a6,$0126)
         MOVE.W #$c000,$00dff09a ; INTENA = SETCLR|INTEN   <-- fuerza INTEN
         MOVE.L (a7)+,(a5,$0034) / MOVE.W (a7)+,-(a5)
$FC1F74: BSET.B #$7,(a6,$0124) / SNE.B d0 / TST.B (a6,$0127) / TST.B (a6,$0126)
         MOVE.W #$8004,$00dff09c ; INTREQ = SETCLR|SOFT  <-- libro de Disable/Enable
         RTS
$FC0EC2: MOVEM.L #$c0c6,-(a7) / MV2SR.W #$2700 / BCLR.B #$7,(a6,$0124)
         MOVEA.L (a6,$0114),a1   ; ThisTask
         BTST.B #$5,(a1,$000e)   ; tc_Flags bit 5
         LEA (a6,$0196),a0 ...   ; recorrido de lista
$FC0E9C: BTST.B #$5,(a7,$0018) / MOVEA.L $0004.w,a6 / TST.B (a6,$0127)
         BTST.B #$7,(a6,$0124) / MV2SR.W #$2000 / ...
```

- Las llamadas **normales** a `$FC08E6` llegan con **`a5=$00C00240`** (fijado por
  `LEA (a6,-$36),a5` en `$FC1F44`, a6 = SysBase), en **modo usuario** (SR=$0010), retornando a
  `$FC1F4C`; ocurren durante el arranque (operaciones de disco). La rutina llamante
  (`$FC1F40-0xFC1F4C`):

```text
$FC1F40: MOVE.L a1,(a0)
$FC1F42: MOVEA.L a5,a0            ; toma el a5 entrante como "puntero de código"
$FC1F44: LEA (a6,-$36),a5         ; a5 = tabla de despacho (SysBase-$36)
$FC1F48: JSR (a6,-$1e)            ; LVO -30
$FC1F4C: MOVEA.L a0,a5
$FC1F4E: MOVEA.L (a6,$0114),a1    ; ThisTask
$FC1F52: MOVE.L (a1,$0016),d0 / MOVE.L (a1,$001a),d1 / AND.L d0,d1   ; tc_SigWait & tc_SigRecvd
```

- **La llamada que rompe**: breakpoint condicional en `$FC08E6` con `A5==0` (coincidió tras 13
  impactos normales). Estado: `PC=$FC08E6`, `SR=$2508` (supervisor, IPL 5), `A5=0`, `A6=$00C00276`
  (SysBase), `A7=$00C37420` (SSP). Pila de supervisor:

```text
$C37420: $00FC1F4C  <- dirección de retorno (la rutina de arriba)
$C37424: $00C0D936  <- nuestra función `vblank_signal_wait_next` (¡la ruta de Wait()!)
$C37428: $00FF4128
$C3742C: $00000005
$C37430: $80000000  <- el argumento de Wait() (máscara con el bit 31)
$C37434: $00C19AF0 / $00C0DD68 / $00C19B78 / $00C12762 / $00C11184 / $00C395A8 ...
```

- Tras `JMP (a5)` con a5=0: el CPU ejecuta la tabla de vectores como código; la tabla de vectores
  de excepción queda corrompida (observado `$10`=0, `$14`=$00C00276, `$18`=6), `A7` da la vuelta
  (`$FFFFFFC8`), IPL 7 — tormenta irrecuperable. También confirmamos que `ORSR` es legal en 68000
  (core `cpuemu_0.cpp` de WinUAE) y que el trap `$F0FF60` es un `calltrap` JSR normal
  (`uaelib.cpp:470`).

## Preguntas

1. ¿Qué es la **tabla RAM en `SysBase-$36`** de KS 1.3 y qué entradas tiene? Las leemos como:
   #0 `$FC0F1C` (supervisor-call: `MV2SR`, guarda marco en USP, **`INTENA=0xC000`**),
   #1 `$FC1F74` (libro de `Disable`/`Enable` vía `INTREQ=0x8004` SOFT),
   #2 `$FC0EC2` (SR=0x2700, `ThisTask` `tc_Flags` bit 5, lista en `SysBase+$196`),
   #3 `$FC0E9C` (cambio de modo, `MV2SR #$2000`), #4 `$FC08E6` (el trampolín).
   ¿Son las entradas de `Supervisor`/`SuperState`/`UserState`/`Disable-Enable` de KS 1.3? ¿Para
   qué se usa la entrada #4 (por qué se saltaría al trampolín a través de la tabla)?
2. La rutina llamante en `$FC1F40-$FC1F4C` toma su **`a5` entrante** como puntero de código
   (`MOVEA.L a5,a0`) y luego fija `a5 = SysBase-$36`. ¿Qué rutina es (nombre) y qué camino de
   llamada llega a ella **con `a5=0`**? ¿Qué debería contener `a5` al entrar (dirección del stub
   de supervisor = base de la tabla, un campo de la tarea, o un valor del llamante)? En nuestro
   fallo, el marco inferior es nuestra ruta de `Wait()` (`vblank_signal_wait_next`): **¿puede
   `Wait()` de exec acabar en esta rutina con puntero de código nulo** (p. ej. por los campos
   `+$124/$126/$127` de `SysBase` o la lista de tareas en `SysBase+$196`)?
3. Dado que la entrada #0 fuerza `INTENA=0xC000` (INTEN on) y la #1 usa la **interrupción SOFT**
   (`INTREQ=0x8004`) para `Disable`/`Enable`: **¿reescribe exec `INTENA` desde su propia copia
   guardada** (campos `SysBase+$124/$126/$127`) en esos caminos, borrando en silencio un bit
   `VERTB` que un proceso CLI normal puso sustituyendo el autovector de nivel 3? Es decir, ¿el
   "VERTB borrada por código ROM" que capturamos es el **libro de `Disable`/`Enable` de exec** y
   no el libro de display de graphics? Si es así, ¿cuál es la forma correcta de que un proceso
   mantenga VERTB habilitada (`AddIntServer` ¿solo?), y hay alguna forma soportada de convivir con
   un reemplazo crudo del vector?
4. ¿Se trata `Wait(0x80000000)` (bit de señal 31, reservado con `AllocSignal(-1)`) como valor **con
   signo** en algún punto de este camino de despacho (`BPL/BMI` sobre máscaras)? Probamos reservar
   un bit bajo (8..15) y el fallo no cambió, pero queremos saber si el bit 31 está reservado.
5. Cuando `tc_TrapCode`/`tc_ExceptCode` son nulos y una tarea toma un TRAP/excepción, el camino por
   defecto de KS 1.3 — ¿salta alguna vez **al trampolín con `a5=0`** (es decir, nuestra tormenta es
   la firma esperada de un trap code nulo), o sigue otra ruta (Alert/guru)?

Ya descartado (no volver a sugerirlo): corrupción de código (runtime == ELF), el `probe_when_ready`
por frame (breakpoint GDB normal; desactivarlo no ayuda), ilegalidad de `ORSR` en 68000 (legal en el
core de 68000 de WinUAE), el trap `$F0FF60` (`calltrap` JSR) y el cambio de bit de señal bajo (sin
efecto). Un bug aparte de codegen de gcc 15 m68k a `-O2` (puntero basura en una cola inlineada) se
sigue localmente y no es este fallo.
