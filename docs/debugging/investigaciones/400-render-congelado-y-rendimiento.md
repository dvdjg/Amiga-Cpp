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

## 4. Cuelgue intermitente de VBlank (parcialmente mitigado; queda el latido muerto bajo SO vivo)

Con el juego ya rapido aparecio un cuelgue total reproducible: el frame se para (observados 0, 19, 21, 90, 176, 298, 349 en distintos arranques, ~50 % de las veces) mientras el display muestra un frame **correctamente renderizado**; `state=3`.

Evidencia recogida (GDB + canal lateral):

- Traza por single-step en el estado parado: el bucle principal esta sano (`run_slice -> main -> wait_vblank_run`) y se queda en el **sondeo de `VPOSR`** de `wait_vblank_run` (`amiga.cpp:234-264`).
- Los valores que el invitado lee en ese bucle **avanzan** (D0 = `0xa0000195 .. 0xa000056d`): el haz esta vivo. La mascara del engine (`& 0x1ff00` sobre el long de `$DFF004`) decodifica, segun la fuente del emulador (`WinUAE-DBG/custom.cpp:2429-2463`), **hpos (VPOSR bit0 = vpos bit 8)**: la condicion de salida es `hpos==0x37 && vpos>=256`, que ocurre en cada linea de la mitad baja del campo.
- Pese a ello el frame del juego no avanza durante decenas de segundos: el bucle principal espera `hb.frames != seen` y ese contador solo lo incrementa el **latido de VBlank** (`VBlankHeartbeat::run`); es decir, **la IRQ VERTB no llega** mientras el CPU sigue ejecutando (lo que excluye un cuelgue del bucle del juego).
- Es la misma clase de fallo de `212-vblank-irq-apagada-por-el-so.md` (la ROM de AmigaDOS reescribe INTENA y el rearme `level3_sync` del engine no siempre lo recupera). El cambio a espera activa (`set_vblank_sleep(false)`) elimino una manifestacion previa (PC en la ROM de dispatch, `$FC0F90`) pero **no** este modo (latido perdido).
- No lo introducen los cambios de este turno: ocurre igual con las builds previas a las optimizaciones y ya estaba abierto en la sesion anterior ("cuelgue state=1 intermitente").

**Siguiente paso (engine)**: hacer que el rearme del latido sea verificable (contador de rearmes + comprobar INTREQ/INTENA en el bucle de espera activa) y, si la IRQ no se puede recuperar bajo AmigaDOS, que el bucle principal detecte el latido perdido (p. ej. por VPOSR) y lo cubra, en vez de esperar un contador que no llega. Ficha: `212-vblank-irq-apagada-por-el-so.md`.

## 5. Segunda pasada de rendimiento (precocina de salas y banda de HUD)

- **Pre-cocina de las tres salas en `init`** (`cook_room_into` por sala; `enter_room` solo conmuta `m_room_imgs[m_room]`): elimina el tiron de ~1 M ciclos que cada puerta pagaba incluso con la tabla ROM. El cofre abierto ya no re-cocina: el parche de cofre abierto cubre el cerrado horneado (misma huella 14x10).
- **Banda de HUD** (`publish_hud_band`, 16 filas x 20 palabras x 4 planos): los cambios de HUD (contador ~2/s y eventos) republican solo la banda, no la imagen completa.
- Medidas (`measure-fps`, A500_debug, ventanas de 40 s): **45,7 fps / 155 105 ciclos/frame (1,093 campos)** con la cronica completa; los frames sin transicion ya son 1,00 campo — el residual son las republicaciones completas al entrar en sala y el jitter del SO. La media depende de la ventana (con varias puertas en 20 s baja a ~37-43).

## Evidencia y herramientas

- Sondas (en `out/tmp`, ignorado): `rpg400_probe{1..10}.mjs` (copperlist/buffers/poke/screenshots), `rpg400_launch_long.mjs`, `rpg400_resume.mjs`, `rpg400_pcsample.mjs` (muestreo de PC + resolución por `nm`/addr2line), `rpg400_irqrate.mjs` (tasa de VBlank vs frame del juego), `rpg400_hang.mjs` (detección de cuelgue + pila), `rpg400_scriptsim.mjs` y `rpg400_combatsim.mjs` (simuladores host del guion y del combate).
- Medidas: `node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json`; perfil `node tools/debug/winuae-profile.mjs 400_rpg_slice A500_debug 25`.
