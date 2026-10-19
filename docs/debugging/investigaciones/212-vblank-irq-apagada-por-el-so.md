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
- Consultas externas: [`consultas/consulta-ks13-trap-vertb-en.md`](consultas/consulta-ks13-trap-vertb-en.md) y
  [`consultas/consulta-ks13-trap-vertb-seguimiento-en.md`](consultas/consulta-ks13-trap-vertb-seguimiento-en.md) (con
  sus versiones en castellano), con la identificación de las rutinas ROM por dirección.
- Bug de codegen previo: [`pump-timer-o1-codegen.md`](pump-timer-o1-codegen.md).

## Por qué el vector de nivel 3 es problemático (modelo)

El 68000 tiene autovectores por **nivel** de interrupción, no por fuente: la Amiga conecta
en el **nivel 3** tres fuentes (VERTB, BLIT, COPER) que por tanto comparten **un solo**
vector ($6C). Ese vector es un **recurso propiedad del SO**: exec instala ahí su despacho y
reparte a sus propios handlers/servidores. El engine, para el latido por IRQ,
**sustituye el vector** y gestiona `INTENA` a mano (`level3_sync`). El setup **sí lo
controla** (instala el vector, enciende VERTB/BLIT y los re-arma si el SO los borra:
`wait_vblank_run`), pero **no puede impedir que su dueño lo use**: el libro de
display/DOS del SO reescribe `INTENA` con estados que exec considera suyos, y rutinas ROM
como el `Wait()` de exec hacen transiciones a supervisor con su tabla de despacho en RAM
(`SysBase-$36`) esperando que el despacho de nivel 3 sea el suyo. Con el vector en manos
ajenas, esa interacción degenera en el fallo `a5=0` (la CPU acaba ejecutando la tabla de
vectores). Moraleja de diseño: **no secuestrar nivel 3 bajo un SO vivo**; o se sondea
(`run_frames_polling`, lo adoptado por la 119), o se registra un **interrupt server**
(`AddIntServer`, integrado con exec).

## Reconfirmación con la demo 119 (build de carga rápida) y modo de sondeo

La demo 119 (mundo de biomas generado en compilación, carga ≈1,1 s) **revive el fallo**
al arrancar el bucle por IRQ antes de la actividad tardía del SO: `state=Ready` con el
`frame` congelado a los pocos frames (6-69 según el camino de lanzamiento), el emulador
sigue corriendo (ciclos avanzan) y el **PC muestreado alterna entre `0x0` y direcciones de
código** (el CPU ejecutando la tabla de vectores: la tormenta del fallo `a5=0`). Reproduce
con y sin `takeover_system()` y con espera activa (`set_vblank_sleep` false, variante 2):
confirma que **no** es el clobber de `INTENA` sino la causa raíz (apartado 2), aún abierta
para el bucle por IRQ/`Wait` bajo SO vivo.

**Modo de sondeo (el adoptado por la 119)**: `run_frames_polling` no instala el servicio de
VBlank (no secuestra el vector de nivel 3 ni depende de IRQs ni de `Wait()`; sondea `VPOSR`
en `wait_vblank`) + `takeover_system()` para el display. Con eso la 119 queda estable a
49,85 fps (1,003 campos/frame; `measure-fps`/`record-fps`) con regresión y flicker en
verde. El coste es CPU ocupada sondeando; a cambio desaparece la tormenta de vectores.

**Variante 3 (`AddIntServer`) probada y descartada**: se implementó el latido por
**interrupt server de exec** (INTB_VERTB/BLIT; shims que preservan registros y hacen RTS;
`level3_sync` no toca `$6C`) y se probó en la 119 en tres configuraciones:
(a) servidor + `Wait()` + `Forbid` (takeover) → **deadlock**: bloquear la tarea en `Wait`
con `Forbid` congela el planificador (ciclos detenidos en el `STOP` de exec);
(b) servidor + espera activa + takeover → corazón a 0 (el servidor no despacha);
(c) servidor + espera activa sin takeover → avanza ~47 frames y **muere igualmente** con
el PC en ROM. Conclusión: la causa raíz no es *quién* posee el vector, sino la interacción
de las IRQ con el estado de exec bajo el SO vivo (los tres caminos de IRQ medidos mueren;
el sondeo es el único estable). La implementación experimental se retiró del engine; este
apartado guarda el resultado y los números para no repetir la prueba.

### Sonda sin GDB (canal lateral) y hallazgos del intento 2026-10-16

Al instrumentar la captura del primer momento mortal (build de la 119 con la puerta
`K119_PROBE_IRQ`: `set_vblank_sleep(false)` + `run_frames`) aparecieron tres hechos que
condicionan cualquier intento futuro:

1. **El stub GDB muere EN la tormenta**: la conexión se resetea en el momento del fallo
   (el CPU ejecutando la tabla de vectores corrompe también los hooks del depurador). Es
   la explicación de por qué «attach después de la muerte» fallaba siempre: no hay stub
   que atender. **El canal lateral SÍ sobrevive** (`regs` y `mem` siguen respondiendo) y
   es la vía de observación viable.
2. **Método sin GDB** (scripts temporales `out/tmp/a5probe.mjs`, `a5find.mjs`,
   `a5delta.mjs`): muestreo de `regs` en anillo + lecturas `mem` por canal lateral, y
   **delta de carga validado por BYTES del ELF** — comparar el `.text` del `.elf` en disco
   contra la RAM viva (el literal `0x454e4752` del magic aparece también en el código, así
   que validar por *patrón* da falsos positivos; por *bytes* 3/4 probes exactos + el
   longword relocalizado distinto a propósito). Delta verificado de este build:
   `0xc0c790` (p. ej. `main` linked `0x9bac` ↔ runtime `0xc1631c`).
3. **A `-O1` el bucle IRQ cuelga en el arranque, DENTRO de `memset`** (PC fijo en
   `memset+0x1a` durante miles de muestras): es el bug de codegen de `-O1` ya documentado
   en [`pump-timer-o1-codegen.md`](pump-timer-o1-codegen.md) — **tapa** la reproducción de
   la tormenta, no es la tormenta. Dato colateral: el estado del run-status quedó
   `Booted, frame=0` (murió antes de `init`), coherente con el cuelgue de arranque.

**Receta para el siguiente intento** (captura de la tormenta): elegir primero el perfil
donde el bucle IRQ *llega a morir* con el código actual (a `-O0` + espera activa el doc
describe que AVANZA sin el fallo; el `-O2` tiene su propio bug de arranque documentado);
una vez elegido, muestrear por canal lateral con el anillo y, al detectar la anomalía
(`pc < 0x500` o `a5 == 0`), volcar pila y tabla de vectores *por el canal lateral* (el
GDB ya no estará).

### Intento 2026-10-17 (juego 400): el latido no llega y la espera VPOSR también se atasca

El juego 400 (proceso AmigaDOS sin takeover, `set_vblank_sleep(false)`) congela el frame en ~50 % de los arranques (frames parados observados: 0, 19, 21, 90, 176, 298, 349) **mostrando un frame correctamente renderizado** y con `state=3`. Evidencia:

- Traza por single-step (`vCont;s` + `g`) del estado parado: el bucle principal está sano (`BackgroundQueue::run_slice -> main -> wait_vblank_run`) y se queda en el **sondeo de VPOSR**; los valores que el invitado lee en el bucle **avanzan** (D0 = `0xa0000195 .. 0xa000056d`, +1 hpos por instrucción del step): el haz está vivo.
- `hb.frames` solo lo incrementa `VBlankHeartbeat::run` (IRQ VERTB): si el frame del juego no avanza pese a la espera, **la IRQ del latido no llega** (o llega a un ritmo ínfimo).
- **Experimento A** (rearme incondicional de `level3_sync` en cada espera, en lugar de decidir leyendo INTENA): sin mejora; el registro es de escritura y la lectura no es fiable, pero el rearme extra no recupera el latido.
- **Experimento B** (watchdog en `Engine::run_frames`: `irq_beats` + latido sintético desde el bucle con el hook del mini-SO, patrón del modo polling): evita el cuelgue total, pero el arranque degradado corre a **0,17 fps deterministas** — la propia espera VPOSR retorna una vez cada ~5,5 s, así que el watchdog apenas sintetiza. Es decir: en el estado malo **se atascan a la vez la entrega de VERTB y la condición de salida del sondeo VPOSR**; el denominador común está en la interacción emulador/SO alrededor de la ventana de VBlank.
- Ambos experimentos se revirtieron (el watchdog convertiría un cuelgue claro en un modo degradado inutilizable). Estado del engine: el del commit.

**Siguiente receta**: instrumentar el backend para que **publique a globals** (leídos por canal lateral, sin `mem` sobre custom, que devuelve la instantánea cacheada) el long de `$DFF004` crudo, `INTREQR`/lectura de `INTENA` y `irq_beats` en cada vuelta de `wait_vblank_run`; reproducir el cuelgue y decidir si (a) el long de VPOSR deja de cumplir la condición (¿`lol`/`LOF`? ¿modo de vídeo?), o (b) VERTB no llega a `INTREQ`. Con eso se sabrá si el fallo es del lado Agnus (VPOSR) o del lado interrupción (INTENA/dispatch).

**Salida práctica (juego 400)**: la variante robusta para un juego bajo AmigaDOS es **no depender de la IRQ**: `App::run_polling` (`Engine::run_frames_polling`) corre `update -> wait_vblank -> render` en el bucle principal con el hook del mini-SO llamado desde ahí. Con ese driver, tres arranques consecutivos del juego 400 dieron la **misma** medida (44,72 fps, crónica completa) sin cuelgues, frente a los parones deterministas del bucle por IRQ en el momento en que la ROM apaga VERTB. La deuda queda para `run_frames` bajo SO vivo.



## Nuevo hallazgo (2026-10): el SO tambien **re-toma el vector 0x6C**

Repro determinista: demo 214 con `color_depth = 2` (escena de 2 planos). Llega a READY y el contador se congela en el **frame 41**; la CPU queda en el `STOP` del task idle del SO (bloqueada en `Wait()`, esperando una señal de VBlank que ya no llega). Evidencia con el MCP adjunto al runner (patron de depuracion en `debug-winuae-v2-guide.md`):

- Vector 0x6C en el cuelgue: `$00FC0CE2` (**dispatcher de la ROM**); en el mismo binario con 4 planos (que avanza miles de frames): `$00C25CC2` (handler del engine en Chip). El engine cree que su vector sigue instalado (`g_level3_installed`) y solo re-arma INTENA, que **conserva VERTB** (`INTENAR=$8022`): por eso no se recupera.
- No es un trap: un stub en los vectores de bus/address/illegal/div0 no se dispara.

**Intento de arreglo (revertido)**: registrar el handler como **servidor Exec** (`AddIntServer`) en vez de sustituir el vector (un nodo por fuente, VERTB/BLIT, con un stub ASM `level3_server` que llama al mismo `level3_dispatch`). Con el servidor el vector se queda en la ROM, pero el cuelgue pasó al frame 23 con `INTREQ=$1040` (**BLIT pendiente sin despachar**) y la CPU en bucle de ROM: falta afinar el camino de servidor (orden de alta del servidor de BLIT, limpieza de INTREQ en contexto de servidor). Cambios revertidos; la receta de sondeo de este documento sigue siendo la salida operativa.

**Siguiente paso**: reintentar los servidores Exec con la instrumentacion de la receta (publicar `INTREQR`/`INTENAR` y el vector 0x6C en globals leídos por canal lateral) para ver por qué el servidor de BLIT no despacha.

## Aplicación a demos con blits síncronos y HUD (2026-10-19)

La demo 001_adv_walk (capa de aventura, A0) reprodujo la misma familia de fallo en su forma más simple: con el bucle por señal puro (`run_frames`) el despertar del VBlank se **perdía en ciertas fases del frame** y la app quedaba dormida en `Wait()` (frames y ciclos congelados, CPU en STOP; evidencia por canal lateral). La tasa de pérdidas dependía del **trabajo por frame**, así que contaminaba las medidas A/B (se vieron ratios de 2,4× entre variantes equivalentes) y producía parones intermitentes en builds aparentemente idénticos.

**Receta operativa confirmada**: para demos que combinan `run_frames` con trabajo por frame variable (HUD de texto, blits con `blitter_submit(j, true)`) usar la **espera activa** de la variante 2: `backend.set_vblank_sleep(false)` antes de construir `Engine`/`App` (`run_frames` se mantiene; el latido por IRQ no cambia, solo la espera sondea VPOS en vez de dormir en `Wait()`). Con ello la 001 pasa de 10-17 fps con parones a **48-49 fps estables** (`VALIDATION.md` de la demo; detalle de la investigación en `001-adv-walk-pacing.md`).

**Cruce con la 306 / DT-035 (2026-10-19)**: el estancamiento determinista de la 306 (`App::run` dormido sin consumir el latido, frame clavado) es la misma familia; la nota de deuda (DT-035/DT-042) y el plan de instrumentar `Signal`/`Wait` en el emulador siguen vigentes como arreglo de raíz del camino de señal.

**Nota de método**: toda medida de cadencia debe declarar el modo de espera (señal o activa); un guest dormido falsea cualquier comparación.

## Migración de demos al modo seguro (2026-10-19)

Con el guard `0xE212` (commit `cae4f4d2`), las demos con espera por señal fallan ruidosamente bajo el runner. Estado tras la pasada de migración (ventanas de 6-12 s, avance comprobado por `vbl`/frames/contadores):

| Modo | Demos | Evidencia |
|---|---|---|
| **Sondeo** (`VblankWait::Polling`) | 302, 303 | la 302 moría a los ~2,5 s con señal **y** con activa (`vbl=124` congelado, determinista ×2); la 303 perdía ~11,8% de latidos con señal y moría a los ~209 frames con activa. Con sondeo: 302 con `vbl=584` avanzando en 12 s y 303 al 99,8-99,9% de latidos. |
| **Activa** (`VblankWait::Active`) | 001, 122, 214, 215, 304, 305, 306, 010, 011, 060 | avance comprobado: 304/214/215 por secuencias (pares con cambio), 122 por sonda de frame (563), 305/306 por gates con memoria; 010/011/060 son pantallas estáticas por diseño (el guard no las rechaza). |

Regla práctica: empezar con la espera **activa**; si la demo muere (parón determinista con `vbl`/`frame` congelados en dos corridas), pasar a **sondeo** — la única variante estable para la clase de muerte por IRQ bajo SO vivo. El sondeo cuesta CPU ocupada (sondea `VPOSR`) a cambio de no depender de la IRQ.

## Estado en la muerte con espera activa (2026-10-19, demo 302)

La 302 murió en dos corridas consecutivas con espera activa (`vbl=124` y `176`, bucle congelado) y **sobrevivió 3/3** tras un cambio de layout del engine (parche temporal de diagnóstico, retirado): es un **Heisenbug sensible al código/tiempo**, como el efecto observador de DT-036. La sonda en el momento de la muerte (contadores del periférico escritos por el bucle principal y leídos tras el cuelgue) dio un estado **sin anomalías de registro**:

- Vector de nivel 3: **el del engine** (sin re-toma; el mecanismo de la 214 **no** aplica aquí).
- `INTENA` (INTENAR): **0x77FF/0x77E0 — VERTB habilitada** (sin clobber).
- `INTREQ` (INTREQR): **0** — nada pendiente (el latido no se entrega, o se ackea y no llega al bucle).
- `os::frame_count` (latido): **congelado** con el bucle (la IRQ dejó de alimentarlo).
- `VPOSR` del último frame vivo: posición normal (el haz vivo en las lecturas).

Conclusión: la clase de muerte de la 302/303 **no** es el clobber de INTENA ni la re-toma del vector; el bucle queda atascado con la IRQ aparentemente sana — consistente con el hallazgo del juego 400 («se atascan a la vez la entrega de VERTB y la condición de salida del sondeo VPOSR»). Método listo para el siguiente intento: **marcador de fase** en `wait_vblank_run` (contador: `0x100|vpos` = atascado en `vpos==311`, `0x200|vpos` = esperando llegar a 311, `0x300` = salió) — con un build que muera, la lectura post-mortem dice en qué bucle quedó; el reto es un build que muera **y** conserve la instrumentación.
