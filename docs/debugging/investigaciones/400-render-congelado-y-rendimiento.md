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

## Evidencia y herramientas

- Sondas (en `out/tmp`, ignorado): `rpg400_probe{1..10}.mjs` (copperlist/buffers/poke/screenshots), `rpg400_launch_long.mjs`, `rpg400_resume.mjs`, `rpg400_pcsample.mjs` (muestreo de PC + resolución por `nm`/addr2line), `rpg400_irqrate.mjs` (tasa de VBlank vs frame del juego), `rpg400_hang.mjs` (detección de cuelgue + pila), `rpg400_scriptsim.mjs` y `rpg400_combatsim.mjs` (simuladores host del guion y del combate).
- Medidas: `node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json`; perfil `node tools/debug/winuae-profile.mjs 400_rpg_slice A500_debug 25`.
