# Juego 400 (RPG SLICE): display "congelado", guion atascado, rendimiento y cuelgue de VBlank

Estado: **display congelado, guion atascado, cuelgue intermitente y rendimiento resueltos** (verificado con secuencia + visión Ollama, cronica completa hasta `OBJ:VICTORY!`). Queda afinar el ultimo tramo del presupuesto (1,11 campos/frame frente al objetivo de 1,00).

## 0. Resumen de causas y fixes

| Sintoma | Causa | Fix |
|---|---|---|
| Display fijo a un buffer temprano (solo parches) | `build.args` con `-DK_SLICE_NO_ROOM=1` (ablacion commiteada) | borrado el archivo |
| Guion atascado en NPCs/arboles y lobos inmortales | paseo ciego por ejes; enfriamiento de ataque (18) > cadencia de fuego (12) | rodeo por el otro eje + waypoints + tres amagos por lobo |
| "Cuelgue" de segundos y ~13 fps | `cook_room` recorria 16 px por palabra (~12,5 M ciclos por coccion) | **tabla ROM por tile** (`kTilePatterns`) → ~1,0 M ciclos (12x) |
| Coste fijo por frame (~140 k ciclos) | copia de la sala entera a la pantalla **cada frame** | **publicacion por cambio** (`m_room_publish`, 2 frames cubren ambos buffers) + **borrado de estelas desde la imagen** (`restore_cell`) |
| Cuelgue total (frame parado, PC en ROM `$FC0F90`) | `Wait()` de Exec muere bajo AmigaDOS vivo (VERTB reescrito) | `backend.set_vblank_sleep(false)` (espera activa; opcion documentada, ver `212-vblank-irq-apagada-por-el-so.md`) |

Medidas (`measure-fps`, A500_debug): antes 13,78 fps / 514 862 ciclos/frame; despues **42,9-45,2 fps / 156 911 ciclos/frame (1,106 campos/frame)** en ventanas de 20 y 40 s. Tick de VBlank: ~2 200 ciclos por latido (1,5 % de CPU). Coccion: 1,0 M ciclos (antes ~12,5 M).

## 1. Display congelado: la ablacion commiteada

`games/400_rpg_slice` se veía **fijo**: solo los parches dinamicos (jugador, pocimas, lobos) sobre negro, sin sala ni HUD, identico frame a frame.

- La sala y el HUD viven en la imagen Chip `m_room_img` (HUD fusionado), que `render` copiaba a la pantalla cada frame (`blit_plane_major`).
- La sonda por canal lateral mostro que la copperlist **si alternaba** `BPL1PT` entre los dos buffers (`0x15060`/`0x1F070`, parcheo de `commit`), pero **ambos buffers estaban a cero** salvo parches: la copia de sala nunca se ejecutaba.
- **Causa raiz**: `games/400_rpg_slice/build.args` contenia `DEMO_FLAGS=-DK_SLICE_NO_ROOM=1` (ablacion de la sesion anterior, commiteada junto al vertical slice). Con ese flag, el blit de la sala queda fuera (`#ifndef K_SLICE_NO_ROOM` en `render`) y el HUD (que va dentro de la imagen) tampoco se publica.
- **Fix**: eliminado `build.args`. La ablacion sigue disponible por `DEMO_FLAGS` en el entorno o en un `build.args` temporal.

Nota de herramienta: la lectura de **registros custom por canal lateral no es fiable** (`gdb_try_read_byte` usa una instantanea de `save_custom` cacheada la primera vez, `WinUAE-DBG/od-win32/barto_gdbserver.cpp:840-870`); la evidencia valida fue el **contenido de la copperlist** y la **memoria**, no `$dff0e0`. Los registros por GDB (`g`) si son fiables para PC/pila.

## 2. Guion atascado: paseo ciego y combate desbalanceado

- El paseo del guion mueve **un eje por paso (X preferido)** y no mira bloqueos: en `(5,10)` → `(7,11)` choca con la anciana `(6,10)`; al volver del mercader (`(11,10)` → `(18,10)`) choca con el mercader; la fila 5 del bosque tiene un arbol en `(11,5)`.
- Los dos pasos de "espadazo" por lobo no matan: `player_attack` tiene **enfriamiento 18** y los flancos de fuego caen cada **12** frames → el segundo golpe se descarta, el lobo sobrevive, persigue y acaba matando al jugador (respawn en la cama) y desviando el guion.
- **Fix** (validado con simuladores host del paseo y del combate antes de tocar el juego):
  - `script_input`: rodeo de obstaculos (si el eje X esta tapado, el paso va por Y); el pathfinding real llegara con la integracion RPG<->sim.
  - waypoints `(0,7,8)`, `(0,11,11)` y `(0,18,11)`.
  - **tres amagos por lobo**: el golpe efectivo cae en el 1.º y 3.º flanco (12 < 18).

## 3. Rendimiento: coccion con tabla ROM y publicacion por cambio

- El "48,8 fps" de la sesion anterior se midio con la **ablacion activa** y el guion atascado: el juego no dibujaba la sala (4 blits/frame). Con el juego real, `measure-fps` daba **~12-14 fps** y las ablaciones mostraban que el coste no estaba en los parches.
- La instrumentacion por ciclos (debug-peripheral `$B7E928`) y el muestreo de PC por GDB localizaron dos focos:
  - **`cook_room`**: el cocinado recorria 16 px por palabra (`tile_pixel` en bucle) = ~12,5 M ciclos por sala (segundos de CPU: el "cuelgue" de los cruces de puerta). Ahora indexa la **tabla ROM `kTilePatterns`** (palabra por fila/plano precalculada `constexpr`): ~1,0 M ciclos por coccion.
  - **Copia de la sala por frame**: 4 blits de 20x224 palabras (≈140 k ciclos, mas de un campo). Ahora la imagen se **publica solo al cambiar** (`m_room_publish = 2u` en `cook_room`, en el HUD sucio y en los eventos de inventario/muerte/puerta): dos renders cubren los dos buffers; las estelas de actores se borran restaurando la casilla desde la imagen (`restore_cell`/`erase_cells`, con historial de 2 frames como la demo 001).
- Tras los fixes: **42,9-45,2 fps** con el bucle durmiendo parte del frame (44 % en `wait`); el tramo que queda es el render (~80 k ciclos/frame medidos con la instrumentacion del worktree, incluido `present`) → siguiente paso natural: agrupar los blits de parches en una sola racha y recortar el HUD.

## 4. Cuelgue intermitente de VBlank (resuelto en el juego con el bucle de sondeo)

Sintoma: el juego se congelaba (frames parados en 0, 19, 21, 71, 90, 176, 298, 349 en distintos arranques; ~50 % de boots) mostrando un frame **correctamente renderizado** con `state=3`; con la ultima build el cuelgue llego a ser **determinista** (misma imagen congelada en arranques distintos = mismo momento de juego).

Evidencia:

- Traza por single-step: el bucle principal esta sano (`BackgroundQueue::run_slice -> main -> wait_vblank_run`) y se queda en el sondeo de `VPOSR`; el haz esta vivo (los valores que el invitado lee avanzan). `hb.frames` solo lo incrementa `VBlankHeartbeat::run` (IRQ VERTB) ⇒ **la IRQ del latido no llega**.
- El momento del cuelgue (~1,5-2 s de juego) coincide con el fin del arranque del AmigaDOS (la ROM reescribe INTENA y apaga VERTB: `212-vblank-irq-apagada-por-el-so.md`). Con el rearme condicional (leyendo INTENA, que no es fiable) el latido no se recupera; el **rearme incondicional** probado tampoco (intento revertido), y un **watchdog** de latido sintetico convertia el cuelgue en un modo degradado a 0,17 fps (tambien revertido).
- **Fix en el juego**: `app.run_polling(...)` (`App::run_polling` → `Engine::run_frames_polling`): `update -> wait_vblank -> render` en el bucle principal con el hook del mini-SO llamado desde ahi, **sin depender de la IRQ de VBlank**. Verificado: tres arranques consecutivos identicos (44,72 fps / 158 6xx ciclos/frame, cronica completa) y secuencia de 14 frames con 14 hashes distintos hasta `OBJ:VICTORY`.
- Queda como **deuda del engine** que el bucle por IRQ (`run_frames`) bajo un SO vivo pueda perder VERTB; receta de diagnostico en la ficha 212.

## 5. Segunda pasada de rendimiento (precocina, banda de HUD, navegacion A* y driver de frames)

- **Pre-cocina de las tres salas en `init`** (`cook_room_into` por sala; `enter_room` solo conmuta `m_room_imgs[m_room]`): elimina el tiron de ~1 M ciclos que cada puerta pagaba incluso con la tabla ROM. El cofre abierto ya no re-cocina: el parche de cofre abierto cubre el cerrado horneado (misma huella 14x10).
- **Banda de HUD** (`publish_hud_band`, 16 filas x 20 palabras x 4 planos): los cambios de HUD (contador ~2/s y eventos) republican solo la banda, no la imagen completa.
- **Navegacion del guion con `eng::util::astar`** (rejilla 32x16 con relleno no transitable, coste 1, scratch fijo sin heap): el guion indica destino y el camino rodea NPCs/arboles/lobos en movimiento; la ruta se replanifica al cambiar de paso/sala o si el siguiente tramo se tapa. Validado en host (simulador: cronica completa en 1.305 frames) y en vivo.
- Medidas finales (`measure-fps`, A500_debug): **44,7-45,7 fps / 155 332-158 625 ciclos/frame (1,095-1,118 campos)** con la cronica completa y cadencia determinista entre arranques; los frames sin transicion ya son 1,00 campo.

## 6. Flicker del contador del HUD (resuelto) y cierre del runner

Sintoma: en la captura por pasos de frame (1 frame entre capturas), el contador `F` del HUD alternaba en pantalla **dos instantaneas** (41/49) en frames alternos; el detector determinista marcaba 14 candidatos sobre esa zona (score 64). La serie de hashes de la zona y el volcado de glifos lo confirmaron (sin oscilacion en el resto de la pantalla: 50/59 pares congelados).

Mecanismo: la republica de la banda del HUD repartia la version nueva con un **conteo a ciegas de 2 renders** (`m_hud_band = 2u`); si un evento de sala (`m_room_publish`) u otro recompose corta el conteo, uno de los dos buffers se queda con una instantanea vieja y el doble buffer alterna las dos versiones en pantalla.

Fix:

- **Version por buffer** en el juego: `m_hud_ver` sube en cada recomposicion y `m_hud_shown[2]` guarda la ultima version publicada en cada buffer; cada buffer repone la banda la primera vez que vuelve a ser trasero. `App::screen_buffer_index()` expone el indice del buffer trasero en la fachada (`eng/api/game.hpp`). Verificado por serie de hashes: el toggle desaparece (valores estables por estado, sin A-B-A) sin coste medible (45,66 fps / 155 369 ciclos antes y despues).
- **Herramienta de flicker**: una escena **estatica** (`no_global_translation`) es analizable sin compensar (antes se marcaba `analysis-unavailable` cuando existia baseline); corregido en `flicker-check.mjs` y en el chequeo gemelo de `test-regression.sh`.
- **Cierre del runner** (familia DT-023): el step-capture quita el breakpoint con la CPU **parada** (`pause -> clear -> continue`; antes era una carrera de GDB y podia quedar armado) y el screenshot final cae al monitor GDB si el canal lateral no sirve su cola tras la sesion de breakpoints. `run.args` de la demo fija la captura por pasos (60 frames) para el gate.

Resultado: `flicker-baseline.json` (`max_candidates: 7` = 5 saltos de casilla + 2 del pintado inicial del HUD; `blocks_low` sin acotar: es la metrica estructural de la banda) y `tools/test-regression.sh --flicker --require-flicker-ok --demo games/400_rpg_slice` en **verde** (build/run/analyze/flicker ok).

## 7. Cierre del margen 1,10 a 1,00 campos/frame (preinscripcion)

Objetivo: que cada frame de juego vuelva a caber en un campo PAL (141 876 ciclos) en el estado estacionario; hoy la media del periodo es 155 369 ciclos (1,095 campos; 45,66 fps) aunque los frames "sin transicion" se midieron a 1,00.

Modelo de partida: el bucle de sondeo se sincroniza a la linea 311 (una vez por campo de 50 Hz); un frame doblea cuando `update + render + esperas de Blitter` rebasa el campo y la espera cae al siguiente. Prediccion cuantitativa: con periodos de 1 campo (141 876) y de 2 campos (283 752), una media de 155 369 exige ~9,5 % de frames dobles (~170 de los ~1 800 de la cronica); se esperan en los frames con evento (paso del guion cada 8 frames, paso de lobo cada 10, entrada de sala, eventos de HUD/quest).

Experimento (variable unica = instrumentacion):

```text
Variable unica:      secciones ENG_PROF en el juego (0 update, 1 render, 2 periodo completo,
                     3 nav/A*, 4 lobos, 5 publicacion+HUD, 6 present)
Metrica:             ciclos por seccion (media, min, max, llamadas) via profile.mjs + total via measure-fps
Baseline:            total 155 369 ciclos/frame (7d94a06f)
Delta esperado:      n/a (medicion); section 2 max ~= 283 752 si los frames dobles existen
Criterio de falsacion: si max(seccion 2) < 141 876 en la ventana, la hipotesis de frames
                     dobles por trabajo del juego es falsa y el exceso viene del motor/espera
Reversion:           la instrumentacion es de bajo coste (~14 lecturas del periferico por
                     frame, ~0,3 %) y queda activa (como la 116); no se revierte
```

**Resultados (2026-10-17, A500_debug):**

- **Mecanismo confirmado por los checkpoints del periferico** (slot 0 estampado por frame; `winuae_debugperiph checkpoints` da deltas exactos dentro del emulador, sin latencia de transporte): el bucle de sondeo se sincroniza a la linea 311 (una vez por campo de 50 Hz); el periodo medio va de 149,5 a 157,4 k ciclos segun la ventana, con **~5-9 % de iteraciones que doblan** (saltan un campo). El campo `scan` (linea del sello) baila 16..310: el trabajo por frame varia y, cuando rebasa el campo, la espera cae al siguiente.
- **Reparto por secciones** (`ENG_PROF` en el juego; `node tools/debug/profile.mjs 400_rpg_slice A500_debug 40 --names update,render,frame,nav,wolves,pubs,present`): update medio 7,4 k; render medio 57,6 k (max 1,27 M en frames de evento); publicacion+HUD medio 14,9 k (max 745 k: la publicacion completa de sala son 4 blits sincronos de 224 filas); present medio 4,6 k; el resto (~89 k de media) es la espera de VBlank. Los que doblan frames: publicaciones de evento (entrada de sala, lobo muerto, cofre, puerta) y los replan A* de los pasos (~40 k cada uno).
- **Fix 1 aplicado y medido** (cache de las dos ultimas rutas por paso+sala con realineado a la casilla actual, y replan por tramo tapado como maximo cada 4 frames): skips de patrulla **5,1 % -> 3,5 %** (checkpoint diferencial 1434/1486 hits por campo; baseline 0,946-0,952 en tres instancias previas); cronica **45,66 -> 45,97 fps** (154 324 ciclos/frame). La cache acierta en la patrulla (dos destinos alternos); en la cronica solo en pasos repetidos.
- **Fix 2 aplicado y medido** (lote de casillas: el borrado de estelas y los estampados pasan de N x `copy_now` —begin/end por celda y plano, decenas de esperas— a **dos rachas** `eng::amiga::BlobBatch` de una espera por objeto, `D = C` con cmod 38/6; mismo patron que la 213): el fin de render retrocede ~70 lineas de raster en patrulla, la cronica sube a **46,32 fps** (153 153 ciclos/frame) y el skip de patrulla baja a **2,71 %**.
- **Suelo ambiental medido (no es trabajo del juego)**: con la ablacion total de estelas/estampado (`-DK_SLICE_NO_DYN=1`, build temporal revertido) el skip de patrulla es **2,70 %** — identico al 2,71 % con el lote: el juego ya no contribuye dobles frames en patrulla; el residual (~2,7 % = ~49,3 fps efectivos) viene del entorno de ejecucion (AmigaDOS vivo + re-arme de VERTB del bucle de sondeo; terreno de la ficha 212 y del bucle del engine, no de la logica del juego). Los frames de evento de la cronica (publicacion sincrona de ~745 k, tiempo de bus bajo arbitraje) siguen ahi y son el resto del margen.
- **A/B por hot-patch, bloqueado por estabilidad del fork**: tres instancias murieron a los 10-25 s de un `memory_write` + `continue` por MCP (sin escritura duran minutos); el A/B se hizo por rebuild con el runner, que es estable. Nota para el fork/DT-023: las direcciones runtime de los hunks son estables entre arranques (qOffsets identico), asi que el problema es del ciclo poke/pausa, no de la resolucion.

## 8. Evidencia y herramientas

- Sondas (en `out/tmp`, ignorado): `rpg400_probe{1..10}.mjs` (copperlist/buffers/poke/screenshots), `rpg400_launch_long.mjs`, `rpg400_resume.mjs`, `rpg400_pcsample.mjs` (muestreo de PC + resolución por `nm`/addr2line), `rpg400_irqrate.mjs` (tasa de VBlank vs frame del juego), `rpg400_hang.mjs` (detección de cuelgue + pila), `rpg400_scriptsim.mjs` y `rpg400_combatsim.mjs` (simuladores host del guion y del combate).
- Medidas: `node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json`; perfil `node tools/debug/winuae-profile.mjs 400_rpg_slice A500_debug 25`.
