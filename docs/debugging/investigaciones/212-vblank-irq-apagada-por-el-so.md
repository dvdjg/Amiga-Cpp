# 212: la IRQ de VBlank muere (VERTB reescrita por el SO) y la tarea acaba en el trampolín de supervisor con `a5=0`

Estado: **cerrado (operativo); migración pendiente**. La demo 212 permanece en el bucle por sondeo
(`run_frames_polling`, commiteado y validado). En el cierre se corrigió un **bug real** del pad CD32
(apartado propio) y se añadió al engine la **variante 2** de espera (latido por IRQ + espera activa,
`set_vblank_sleep(false)`), que evita el camino de `Wait()` donde moría la tarea; la migración queda
pendiente de aislar el fallo a `-O2` (frame 1) y la lentitud a `-O0` (apartados propios).

## Síntoma

La demo llega a READY, dibuja el frame 0 y el contador de frames se detiene. Compilada a `-O2`
(el `build.args` de la demo) muere antes, con errores de dirección/instrucción ilegal en la cola
inlineada de `probe_when_ready`; compilada a `-O0` avanza ~7-13 frames (~1 fps) y la CPU queda
atrapada en ROM.

## Evidencia

### 1. La ROM reescribe INTENA y apaga VERTB

Un watchpoint de hardware sobre `INTENA` (`$DFF09A`, escrituras de CPU) capturó, con el vector de
nivel 3 ya sustituido y `VERTB` habilitada por la demo:

```text
$FCD5AC: MOVE.W #$0020,(a2,$009a)   ; a2=$00DFF000 -> INTENA, bit15=0 => BORRA VERTB
```

Cadena de llamadas (toda ROM, del volcado de pila en el impacto): `$00FC63D4` ← `$00FE0496` ←
`$00FDC7AC`. La última rutina termina rehabilitando el display:

```text
$FDC7B2: MOVE.W #$8100,$00DFF096   ; DMACON: SETCLR|BPLEN
$FDC7BA: MOVE.W #$8020,$00DFF096   ; DMACON: SETCLR|SPREN
```

En el impacto, `D7` = puntero al Task de la demo (`$00C06730`). `INTENA` después: `$606C`
(VERTB borrada) y en otro run `$536C` (VERTB puesta, EXTER borrada).

Interpretación (contrastada con consulta externa, ver más abajo): es el **libro de display de
graphics/intuition** (ruta tipo `LoadView`/`MrgCop`) reescribiendo `INTENA` con un estado conocido;
no es el libro de servidores de `exec`. Un proceso CLI que sustituye el vector de nivel 3 y
escribe `INTENA` **no tiene garantizado** que ese enable sobreviva a la actividad de display del
SO. Las demos que **sí** funcionan con el bucle por señal (216/217) toman el display
(`takeover_display`), con lo que la ruta de display del SO deja de correr.

### 2. El fallo primario: la tarea muere y la CPU queda en el trampolín de supervisor

La CPU se encuentra después en ROM:

```text
$FC08E6: ORSR.W #$2000            ; entra en supervisor
$FC08EA: PEA.L $00FC08F4
$FC08F0: MVSR2.W -(a7)            ; empuja SR
$FC08F2: JMP.L (a5)               ; salta al código en (a5)
$FC08F4: RTS
```

`$FC08E6` es el trampolín de "ejecuta el código de (a5) en supervisor" (el patrón que el AHRM
documenta para `Supervisor()`: `lea GoAway(pc),a5` + `jsr _LVOSupervisor(a6)` "trap to code at
(a5)", AHRM 3.ª, líneas 6689-6690). Con `a5=0` el `JMP` salta a la dirección 0, el CPU ejecuta la
tabla de vectores como código, y las excepciones encadenadas corrompen la propia tabla (observado:
vector `$10`=0, `$14`=`$00C00276`, `$18`=6) y desbordan la pila (`A7=$FFFFFFC8`, `SR` IPL 7): la
tormenta final.

Datos que sitúan el mecanismo:

- `*(4)` = `$00C00276` = **SysBase** (ExecBase en RAM, no en ROM).
- `SysBase-0x36` = `$00C00240` = **tabla de despacho en RAM de exec**, entradas `JMP` de 6 bytes;
  la 5.ª entrada es `JMP $FC08E6`.
- Las llamadas **normales** al trampolín llegan con `a5=$00C00240` y `SR` de usuario, retornando a
  `$FC1F4C` (rutina ROM que lee `ThisTask` y `tc_SigWait & tc_SigRecvd`); son el despacho normal
  de exec (p. ej. operaciones de disco en el arranque).
- La llamada que rompe llega con `a5=0`, `SR=$2508` (supervisor, IPL 5) y en la pila de supervisor
  hay un marco de **`eng::amiga::detail::vblank_signal_wait_next`** (la ruta de `Wait()`), con
  `$FC1F4C` encima: el fallo ocurre **dentro del camino de `Wait()`**.

Conclusión de evidencia: la tarea muere por una llamada al despacho de supervisor con puntero nulo;
la limpieza de `VERTB` (apartado 1) es **consecuencia** de que el SO siga vivo tras la muerte de la
tarea (su libro de display vuelve a correr), no la causa del cuelgue.

### 3. Bug de codegen a `-O2` (independiente)

A `-O2` la cola inlineada de `probe_when_ready` (`DemoApp::on_render`, demo TU con
`DEMO_OPT=-O2` por el bug de `-O1` ya documentado en `pump-timer-o1-codegen.md`) usa como puntero
`a3` una **dirección de código** en vez de un puntero válido, y se observan paradas de address
error/instrucción ilegal. Es el mismo patrón "`this`/puntero erróneo" que el bug de `-O1`, en otra
instanciación (`run_frames`). Descartado como única causa: a `-O0` también se cuelga (apartado 2).

### 4. Descartes

- **ORSR** (`0x007C` en `$FC08E6`): legal en el core de 68000 de WinUAE (`cpuemu_0.cpp`); se
  verificó ejecutándolo sin falta.
- **Trap de `$F0FF60`** (overlay de depuración): el fork lo implementa como `calltrap` con `JSR`
  (`uaelib.cpp:470`), legal; el `debug_cmd` del soporte lo llama sin excepción.
- **Corrupción de código**: los bytes en runtime en los PCs de fallo coinciden con el ELF.
- **`probe_when_ready`**: desactivar la llamada no evita el cuelgue.
- **Bit de señal 31**: reservar un bit bajo (8..15) en vez de `AllocSignal(-1)` no cambió el
  síntoma (probado y revertido).
- **Semántica de INTENA/INTREQ en WinUAE**: `custom.cpp:3377` (`INTENA()` usa setclr),
  `custom.cpp:2308` (`INTENAR()`), `custom.cpp:3214-3242` (`intlev()` exige `intreq & intena`),
  `cia.cpp:287-288` (CIA-B -> EXTER), `cia.cpp:378,587` (Timer B INMOD=00 cuenta reloj E).
- **Pistas del stub GDB del fork**: `S04` = instrucción ilegal, `S0A` = address error
  (`od-win32/barto_gdbserver.cpp:5186-5195`).

## Bug real corregido: DDRA del pad CD32

`read_cd32_shift_port2` (`engine/src/platform/amiga/amiga_os.cpp`) direccionaba
`ciaa_reg(0x200u)`, pero `ciaa_reg(i)` = `0xBFE001 + i*0x100`: escribía en **`0xC1E001` (RAM
lenta)** en vez de `DDRA` (`0xBFE201`). Con el pad habilitado (`enable_cd32_pad`) esa escritura
(read-modify-write del bit 7) ocurría **en cada tick** sobre RAM lenta — corrupción acumulativa
compatible con los punteros basura observados a `-O0` (memset con `dst` = bytes de texto del
overlay y `len` de 131 KB; despacho de supervisor con `a5=0`). Corregido a `ciaa_reg(0x02u)`.

Verificación del cierre: la demo en modo polling sigue dibujando igual y deja de detectar el "pad
fantasma" (antes, con la dirección errónea, reportaba botones 7); la verificación limpia del bucle
por mensajes a `-O0` con el fix (con y sin `probe_when_ready`) sigue dando `frame=0`, así que el
fix **no** desbloquea la migración y la causa del `a5=0` queda abierta.

## Variante 2 (espera activa): implementada y probada

`AmigaBackend::set_vblank_sleep(false)` (`backend.hpp`) instala el latido por IRQ **sin** armar la
señal Exec: `wait_vblank`/`os::wait` usan sus ramas de sondeo (espera activa) y `wait_vblank_run`
re-arma VERTB por iteración (`amiga.cpp`) — con el bucle siempre despierto, el clobber de `INTENA`
por el SO es recuperable. La demo vuelve a `run_frames` (mismo bucle por mensajes, sin `Wait()`).

Resultados en la 212 (build.args de la demo, `run_frames` + espera activa):

- `-O0`: avanza **sin el fallo de `a5=0`** (54 frames en 20 s; ~2.7 fps, lento — pendiente medir por
  qué). Es la prueba de que la variante 2 evita el camino de `Wait()` donde moría la tarea.
- `-O2` (config de la demo): se para en el frame 1; `__attribute__((noinline))` en los callbacks del
  `App` **no** lo evita (el bloqueo de `-O2` no era solo la cola inlineada de `probe_when_ready`).
- `-O1`: no alcanza READY (bug de codegen ya documentado en `pump-timer-o1-codegen.md`).

Conclusión: la variante 2 es la base correcta para demos bajo SO vivo (elimina el camino frágil de
`Wait`), pero la migración de la 212 sigue **pendiente**: falta aislar el fallo de `-O2` (frame 1) y
la lentitud de `-O0`. La demo permanece en el modo polling (commiteado) y el soporte de la variante 2
queda en el engine (`set_vblank_sleep`) con este documento como referencia.

## Intentos en el engine (revertidos)

Se implementaron y revirtieron, por no resolver el fallo primario y quedar superados por la variante
2 (`set_vblank_sleep`): un vigilante del latido por IRQ de underflow del Timer B de CIA-B (nivel
6/EXTER) que re-armaba VERTB/BLIT/PORTS, auto-reparación de EXTER en el despacho de VBlank y re-arme
tras `takeover_display`. El SO también borra `EXTER` en sus reescrituras, con lo que el vigilante se
apagaba. El re-arme de VERTB se conserva dentro de `wait_vblank_run` (variante 2).

## Estado final y siguientes pasos

- La demo 212 usa `run_frames_polling` (commiteado, validado); el fix del pad CD32 queda aplicado.
- La migración al bucle por mensajes queda **pendiente** con la variante 2 ya soportada en el engine:
  el camino de `Wait()` (donde moría la tarea con `a5=0`) se evita, pero falta (a) aislar el fallo de
  `-O2` (frame 1, no es solo la cola de `probe_when_ready`) y (b) la lentitud de `-O0` (~2.7 fps).
- Vías a explorar si se retoma: (a) el fallo de `-O2` con watchpoint en el primer frame; (b) medir
  por qué `-O0` va a 2.7 fps (¿doble espera del gate `hb.frames` + sondeo?); (c) `AddIntServer` si se
  quiere recuperar el reposo de CPU (variante 3) bajo SO vivo; (d) por qué 216/217 (con
  `takeover_display`) no sufren el fallo.

## Referencias

- WinUAE-DBG: `custom.cpp:2308,3377,3214-3242`; `cia.cpp:287-288,378,587`; `uaelib.cpp:470`;
  `od-win32/barto_gdbserver.cpp:5186-5195`.
- AHRM 3.ª: `Supervisor()` ("trap to code at (a5)"), líneas 6689-6690 del `.cat.md` ingerido.
- Engine: `engine/src/platform/amiga/amiga_os.cpp` (fix DDRA del pad CD32),
  `engine/src/platform/amiga/amiga.cpp` (re-arme de VERTB en `wait_vblank_run` y comentario de
  restricción en `install_vblank_service`), `engine/include/eng/platform/amiga/backend.hpp`
  (`set_vblank_sleep`, variante 2).
- Consultas externas: [`consulta-ks13-trap-vertb-en.md`](consulta-ks13-trap-vertb-en.md) y
  [`consulta-ks13-trap-vertb-seguimiento-en.md`](consulta-ks13-trap-vertb-seguimiento-en.md) (con
  sus versiones en castellano), con la identificación de las rutinas ROM por dirección.
- Bug de codegen previo: [`pump-timer-o1-codegen.md`](pump-timer-o1-codegen.md).
