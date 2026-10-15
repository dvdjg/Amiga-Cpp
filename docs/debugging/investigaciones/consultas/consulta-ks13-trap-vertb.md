# Consulta: ROM de Kickstart 1.3 — VERTB de INTENA borrada bajo un vector de nivel 3 propio, y CPU atrapada en ROM en $FC08E6 con a5=0

> Versión en castellano (registro local). La versión que se envía a Grok es la inglesa:
> [`consulta-ks13-trap-vertb-en.md`](consulta-ks13-trap-vertb-en.md) (`AGENTS.md` §1.3).
> Demo relacionada: `demos/techniques/amiga/os/212_message_loop`.
> Emulador: WinUAE-DBG (fork BartmanAbyss), A500 cycle-exact (`quickstart=a500,1`,
> `cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`),
> ROM Kickstart 1.3, 512K+512K.

## Contexto

Nuestro programa corre como **proceso AmigaDOS normal** (startup-sequence del runner:
`stack 131072 / cd dh1: / :a.exe`), así que el SO (exec/intuition/graphics/dos) sigue vivo.

Qué hace el programa:

- Sustituye el **autovector de nivel 3** (vector `$6C`) por un handler propio en RAM,
  guardando el vector ROM anterior (observados `$00FC0E40` / `$00FC0D14` en runs distintos).
- Habilita `INTENA = SETCLR|INTEN|VERTB` (`$C020`).
- Reserva una señal Exec (`AllocSignal`/`FindTask`) y el bucle principal duerme en `Wait()`;
  el handler de nivel 3 incrementa un contador y hace `Signal()` en cada VBlank.
- **No** toma el control del display: es una demo de overlay que dibuja por el trap del
  depurador de WinUAE en `$F0FF60` (`jsr $f0ff60`). No usa DMACON ni bitplanes.

Síntoma: el programa llega a "ready", dibuja el frame 0 y el contador de frames se para.
Compilado a `-O0` avanza ~7-13 frames (~1 fps) y luego la CPU aparece atrapada en ROM
(abajo). A `-O2` muere antes (problema aparte de compilador).

## Observación 1 — la ROM borra VERTB en INTENA con nuestro vector instalado

Un watchpoint de hardware sobre INTENA (`$DFF09A`, escrituras de CPU) capturó esta
instrucción ejecutándose:

```text
$FCD5AC: MOVE.W #$0020,(a2,$009a)   ; a2=$00DFF000 -> INTENA, bit15=0 => BORRA VERTB
```

Contexto (ROM):

```text
$FCD5A0: AND.L #$ff,d0
$FCD5A6: MOVE.L #$a,d1
$FCD5A8: CMP.L d0,d1
$FCD5AA: BGT.B ...
$FCD5AC: MOVE.W #$0020,(a2,$009a)   ; borra VERTB
$FCD5B2: MOVE.L a1,(a3,$0022)
$FCD5B6: BEQ.B ...
$FCD5B8: MOVE.W (a1,$0010),(a3,$009e)
$FCD5BE: MOVEA.L (a1,$0004),a0
$FCD5C2: MOVE.L (a0,$0004),(a3,$0032)
...
$FCD5D2: JSR.L $00FCD4CC
```

En la parada, la pila tenía esta cadena de llamadas (toda ROM): **`$00FC63D4` ← `$00FE0496`
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

En el impacto del watchpoint: `D7` = puntero al Task de nuestro programa (`$00C06730`),
`A6` = `$00FF4128` (SysBase); el resto de registros parecían restos del contexto de la demo.

Lectura de INTENA después: `$606C` (INTEN|EXTER|BLIT|PORTS|SOFT — **VERTB borrada**); en otro
run: `$536C` (VERTB puesta, **EXTER borrada**). INTREQR = `$1000` (DSKSYN) sin VERTB pendiente.

## Observación 2 — CPU atrapada en un trampolín ROM en $FC08E6 con a5=0

Estado en pausa: `PC=$00FC08E6`, `SR=$2508` (supervisor, **IPL=5**), `A5=0`, `A6=$00C00276`,
`A7=$00C37420` (SSP).

```text
$FC08E6: ORSR.W #$2000            ; entra en supervisor
$FC08EA: PEA.L $00FC08F4
$FC08F0: MVSR2.W -(a7)            ; empuja SR
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

La pila de supervisor en esa parada contenía `$00FC1F4C`, `$00C0D936`, `$00FF4128` (SysBase),
`$00000005`, `$80000000`, `$00C19AF0`, `$00C0DD68`, ... Desensamblado del marco ROM
`$FC1F4C`:

```text
$FC1F4C: MOVEA.L a0,a5
$FC1F4E: MOVEA.L (a6,$0114),a1      ; ExecBase+$114 = ThisTask -> $00C06730 (nuestro Task)
$FC1F52: MOVE.L (a1,$0016),d0
$FC1F56: MOVE.L (a1,$001a),d1
$FC1F5A: AND.L d0,d1                ; tc_SigWait & tc_SigRecvd
$FC1F5C: BEQ.B ...
```

Los vectores de TRAP #0..#15 (`$80`..`$BC`) apuntan todos a `$00FC0836`..`$00FC0854`
(entradas cada 2 bytes).

## Preguntas

1. ¿Puedes identificar estas rutinas de la ROM de KS 1.3 (del desensamblado/fuente público
   de Kickstart 1.3), con nombre:
   - `$FCD564` (la rutina que contiene el borrado de VERTB `INTENA=#$0020` en `$FCD5AC`);
   - `$FC63CC` (stub de 2 instrucciones que hace `JSR` a `$FCD564`);
   - `$FE0488` (stub que hace `JSR (a6,-$DE)` a través de una base de librería);
   - `$FDC780`..`$FDC7AC` (que termina re-habilitando DMACON `$8100`/`$8020`);
   - `$FC08E6` (el trampolín `ORSR #$2000; PEA; MVSR2; JMP (a5)`) y `$FC1F4C`
     (lee ThisTask y luego `tc_SigWait & tc_SigRecvd`);
   - ¿qué hay en los vectores de TRAP `$FC0836`..`$FC0854` (entradas cada 2 bytes)?
2. Con (1): ¿cuál de ellas es el **libro de habilitación de interrupciones** que recalcula
   INTENA y por tanto borra VERTB (p. ej. `SetIntVector`/`AddIntServer`/`RemIntServer` de
   exec, o una ruta de cierre de display/ventana del SO)? ¿Es esperable que a un proceso CLI
   normal que sustituyó el vector `$6C` y habilitó VERTB se le borre el enable luego?
3. ¿Qué mecanismo es `$FC08E6` en KS 1.3? ¿Qué debería contener `a5` (tc_TrapCode /
   tc_ExceptCode)? ¿Qué hace KS 1.3 cuando una **tarea de usuario ejecuta un TRAP #n con
   tc_TrapCode = 0** — guru, matar la tarea, o un bucle? ¿Puede el estado observado
   (`PC=$FC08E6`, `a5=0`, `SR` IPL=5) ser una **tormenta de excepciones** (p. ej.
   `JMP (a5)` a 0 -> address error -> handler ROM -> vuelta al dispatcher)?
4. Secuencia más probable: (i) nuestro proceso toma una excepción/TRAP por algún motivo, KS
   lo mata / gira en el dispatcher, y el libro del SO borra VERTB después (observación 1 =
   consecuencia); o (ii) el libro de exec borra VERTB primero (matando nuestra IRQ de
   VBlank) y el estado de trap ROM es una pista falsa aparte?

## Ya descartado (no volver a sugerirlo)

- Semántica de INTENA/INTREQ en WinUAE: `custom.cpp:3377` (`INTENA()` setclr),
  `custom.cpp:2308` (`INTENAR()` máscara de lectura), `custom.cpp:3214-3242` (`intlev()`
  exige `intreq & intena`), `cia.cpp:287-288` (CIA-B -> EXTER), `cia.cpp:378,587`
  (Timer B INMODE=00 cuenta reloj E).
- Corrupción de código: los bytes en runtime en los PCs de fallo coinciden byte a byte con
  el ELF y son instrucciones legales.
- El `probe_when_ready` / `eng_debug_ready_probe` por frame (el "ready probe"): es un simple
  breakpoint GDB del runner; desactivar la llamada no evita el atasco.
- Artefacto puro de optimización: el atasco persiste con todo compilado a `-O0`. (A `-O2`
  hay además un fallo temprano con un puntero basura en la cola inlineada de
  `probe_when_ready` — problema aparte de gcc 15 m68k que seguimos localmente.)
- Nuestros re-armados: vigilante por CIA-B Timer B (nivel 6/EXTER) y chequeo de re-armado de
  VERTB; las reescrituras de INTENA del SO también borran EXTER, así que el vigilante se
  para; VERTB a veces la re-habilita la propia reescritura del SO.

Si puedes, cita el listado del desensamblado de Kickstart 1.3 (o el fuente de exec 1.3) que
uses para las direcciones.
