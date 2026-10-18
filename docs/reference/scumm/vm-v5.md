# VM SCUMM V5 (Monkey Island 1/2) — descripción para la conversión a Amiga

Descripción de la máquina de guiones de SCUMM V5 —la de **MI1/MI2 PC** y sus ediciones *Ultimate Talkie*— orientada a la conversión: qué datos son contenido reutilizable, cómo se ejecutan los guiones, qué opcodes usa el juego de verdad y qué se recompila/remodela en `eng::adv`. Fuente primaria: ScummVM (`../SCUMM/scummvm-2026.4.0/engines/scumm/`, líneas citadas) más la extracción real de nuestro pipeline.

Corpus de referencia (workspaces `out/adventure/mi1/` y `mi2/`): **MI1 UE**: 86 salas con datos, **748 guiones** descompilados al 100 %, 150 costumes, 179 sonidos, 7 charsets, 1000 objetos globales; **39 953 opcodes, 182 distintos de 256**, con el top-30 cubriendo el 83.8 % (`reports/opcode-usage.json`, generado por `tools/adventure/analyze/opcode-usage.mjs`). **MI2 UE**: 110 salas, 1006 guiones, 62 682 opcodes, 184 distintos — el par de juegos fija el subconjunto de la VM a soportar.

## 1. Vista general

- La lógica del juego son **scripts de bytecode cooperativo**: cada script avanza al ritmo del bucle principal y cede el control con `breakHere` (0x80) — que en MI1 es el opcode más frecuente (14.76 %). No hay hilos: el "paralelismo" es una cola de scripts que el motor recorre por frame.
- Bucle de juego: `scummLoop` (`scumm.cpp:3147`) → entrada y sentencias → scripts (cond/verb/entry/exit/locales) → procesado de actores (`scummLoop_handleActors`, `scumm.cpp:3330`; `processActors`, `actor.cpp:2314`) → dibujo.
- Tipos de script: **globales** (ids 1–199), **locales de sala** (ids ≥ 200; los usa cada sala para su lógica) y pseudo-scripts de sala **entry/exit** (`ENCD`/`EXCD`; `kScriptNumEXCD = 10001`, `script.h:121`, lanzado al abandonar la sala — `script.cpp:960`, `script_v5.cpp:2847`).
- Los guiones típicos son **planos**: secuencia de réplicas (`print`), esperas (`wait`), movimientos de actor (`walkActorTo`) y cambios de estado (`setState`), con condicionales modestos. Para el port, la máquina de estados cooperativa de `eng::adv` (A2/A5 del roadmap) es el equivalente natural.

## 2. Modelo de datos

- **Variables globales de 16 bits** con índices con nombre en `scumm.h` (enum `ScummVars`): `VAR_EGO` (actor del jugador), `VAR_RESULT`, `VAR_CAMERA_POS_X` (`scumm.h:1838`), `VAR_MACHINE_SPEED` (`:1842`), `VAR_TIMER_NEXT` (`:1856`, temporización de `wait`/`delay`), `VAR_CUTSCENEEXIT_KEY` (`:1861`), `VAR_VERB_SCRIPT` (`:1870`), `VAR_SENTENCE_SCRIPT`. Además: **locales por script** (`Local[n]`), **bits globales** (`Bit[n]`, p. ej. flags de historia) y **arrays** (ops `setVarRange` 0x26 / `arrayOps`).
- **Objetos = `(clase, estado)`**: `obj = class` consulta/`setClass` (0x5d) define pertenencia a clases (`ObjectClass`, `object.h:47`: `kObjectClassPlayer=31`, `Untouchable`, `XFlip`…); `setState` (0x07)/`getState` (0x0f) cambian el estado (0–7 normales, 8–15 "verb states" con lógica en el script del objeto). **Propiedad** (`getObjectOwner` 0x10 / `setOwnerOf` 0x29) decide si un objeto está en una sala, en el inventario de un actor o "desactivado". El nombre por defecto de cada objeto global vive en el bloque `DOBJ` del índice (1000 nombres en MI1).
- **Objetos de sala**: pares `OBIM` (imagen: forma + z-planes) + `OBCD` (definición: x/y, tamaño, flags, padre). Los "hotspots" clásicos son objetos cuyo nombre/estado da el texto de `Look at`.
- **Actores**: `Actor` (`actor.h:91` `int16 x, y` y siguientes: sala, costume, elevación, facing, walkbox, animación). Movimiento: `walkActorTo` (0x1e), `walkActorToActor` (0x0d), `walkActorToObject` (0x36); la ruta la resuelve el motor con las **walkboxes** (`getClosestPtOnBox`, `boxes.cpp`; `adjustActorPos`, `actor.cpp:2090`) y la escala del actor por caja (campo `scale` de la caja; 255 = 100 %). El estado de caminata es una máquina interna (`ActorWalkData`, `actor.h:157`).
- **Sala**: `ROOM` = `RMHD` (ancho/alto en `u16 LE`, `room.cpp:343`) + paletas (`CLUT` 256c / `EPAL` 16c) + `TRNS`/`CYCL` (transparencia y ciclos de paleta) + `BOXD`/`BOXM` (**walkboxes** + adyacencia) + `RMIM` (**imagen comprimida por tiras**) + objetos (`OBIM`/`OBCD`) y, colgando del `LFLF`, los guiones (`SCRP`, `EXCD`, `ENCD`, `LSCR`) y recursos usados (`COST`, `CHAR`, `SOUN`).
- **Costumes**: frames por dirección con paleta local por píxel (0–15 y flags especiales) — es el material del sistema de actores; el port lo convierte a animaciones del engine (**I2**).
- **Sonido**: `SOUN` + motor dinámico **iMUSE**. Decisión del roadmap: **no se emula**; cada tema se marca y se resuelve con módulos propios (música por sala + SFX + stings).

## 3. Sentencias (verbs), entrada y timing

- La interacción clásica es una **sentencia**: `(verbo, objeto[, segundo verbo, segundo objeto])` ejecutada por `doSentence` (0x19; registrado también en las variantes de parámetros 0x39/0x59/0x79/0x99, `script_v5.cpp:113…233`). El resultado se convierte en "ejecuta el script del verbo para ese objeto/clase" (`getVerbEntrypoint`, 0x0b).
- Los 9 verbos por defecto (walk to, look at, pick up…) más los extra de cada sala se definen con `verbOps` (0xfa) y viven en la tabla de verbos de la sala (`verbs.cpp`); sus textos son contenido editable.
- **Entrada**: el motor hace hit-test del ratón contra objetos/actores (`findObject`, 0x35), muestra el nombre y el cursor cambia de estado (`cursorCommand`, 0x2c). Con teclado numérico se elige verbo; clic derecho ejecuta "walk/look".
- **Timing**: los delays son **iteraciones del bucle** (`wait` 0xae detiene el script N frames vía `VAR_TIMER_NEXT`; `delay` 0x2e idéntico para scripts acumulativos). El motor original de PC corría a ~60 Hz; en PAL (50 Hz) **los waits deben reescalarse** al traducir guiones (decisión de I1, documentada por demo).
- **Cutscenes**: el motor apaga la entrada del usuario (`UserputOff`) y usa los mismos opcodes con actores controlados por guion — mismo camino que el director de `eng::adv`.

## 4. Opcodes: el subconjunto que MI1 usa de verdad

Tabla del informe (`out/adventure/mi1/reports/opcode-usage.json`; muestra de los 20 más usados de 182 distintos):

| Byte | Handler (oráculo) | Uso | Semántica para el port |
|---|---|---|---|
| 0x80 | `o5_breakHere` | 14.76 % | ceder el turno (paso de máquina de estados) |
| 0x14 | `o5_print` | 8.01 % | réplica/subtítulo (cola de `say`) |
| 0xae | `o5_wait` | 7.50 % | espera N frames (re-escalar a 50 Hz) |
| 0x1a | `o5_move` | 6.83 % | asignación de variable |
| 0x48 | `o5_isEqual` | 5.35 % | comparación (cond) |
| 0x18 | `o5_jumpRelative` | 4.36 % | salto relativo (control de flujo) |
| 0x0a | `o5_startScript` | 2.94 % | arranca otro guion (cola) |
| 0xac | `o5_expression` | 2.55 % | expresión de parámetros |
| 0x11 | `o5_animateActor` | 2.27 % | animación de actor |
| 0x0c | `o5_resourceRoutines` | 2.14 % | carga/descarga de recursos (streaming) |
| 0x46 | `o5_increment` | 2.10 % | incremento |
| 0xa8 | `o5_notEqualZero` | 2.06 % | condición |
| 0x5a | `o5_add` | 1.96 % | suma |
| 0x91 | `o5_animateActor` | 1.74 % | variante con parámetros |
| 0x28 | `o5_equalZero` | 1.72 % | condición |
| 0xa0 | `o5_stopObjectCode` | 1.68 % | fin de script |
| 0xfa | `o5_verbOps` | 1.58 % | tabla de verbos de la sala |
| 0x3a | `o5_subtract` | 1.55 % | resta |
| 0x2e | `o5_delay` | 1.51 % | espera (variante) |
| 0x05 | `o5_drawObject` | 1.35 % | alta/baja de objeto |

Consecuencia práctica: **implementar el top-30 cubre ~84 %** de la ejecución; el resto son opcodes de nicho (sonido, cámara, depuración, variantes SEGA/NES) que se pueden stubear o marcar `REVISAR` en la traducción (mecánica de I1).

## 5. Qué se conserva, qué se recompila y qué se remodela

- **Datos que se conservan** (a SIM/engine): salas (walkboxes, objetos/hotspots, paletas, dimensiones de mundo), textos (los `print` embebidos → tabla de strings), verbos y sus etiquetas, inventario (objetos con nombre/clase).
- **Se recompila a C++ de `eng::adv`** (I1): los guiones — `breakHere/wait` → pasos de máquina de estados; `walkActorTo` → pathfinding sobre walkboxes del engine; `print` → cola de say; sentencia → `VerbSet`/resolución de acciones; `setState`/clases/propiedad → kernel `eng::rpg`.
- **Se remodela** (A3/A5 del roadmap): el render no decodifica `RMIM` en runtime — el contenido se **cocina** a capas del engine (fondos por capas de parallax con scroll por hardware, transparencias por color/DPF, franjas EHB) y los efectos entran como **copper score** (atardeceres, agua, rayos) en lugar de los `CYCL`/`TRNS` originales. Los 60 Hz → 50 Hz se absorben en la traducción de esperas y en el tick de actores.
- **No se porta**: iMUSE (música dinámica), el compresor `RMIM` como tal, ni las rarezas por plataforma (variantes SEGA/NES/Apple del archivo de opcodes).

## 6. Herramientas y verificaciones

- Extracción y decodificación: `tools/adventure/frontends/scumm_v5/extract.mjs` (`dump-game`, `decode-boxes`), `tools/adventure/decode/scumm_v5-decode.mjs` (**SIM v2**) y `tools/adventure/codegen/emit-content.mjs` (SIM v2 → tablas C++ de `eng::adv`), con el workspace canónico en `out/adventure/<juego>/` ([`WORKSPACE.md`](../../../tools/adventure/WORKSPACE.md)).
- Uso de opcodes: `node tools/adventure/analyze/opcode-usage.mjs <juego>` → `reports/opcode-usage.json` (el oráculo aporta los nombres de handler parseando `script_v5.cpp`).
- Oráculo de comportamiento: ejecutar el juego original en ScummVM y contrastar (salas, textos, transiciones) contra la extracción — criterio de aceptación de I0/I1.
