# Juego 400 (RPG SLICE): display "congelado", guion atascado y rendimiento abierto

Estado: **causa del display congelado y del guion atascado resueltas** (verificado visualmente); **rendimiento (~12-21 fps, CPU 100 % ocupada) y cuelgue intermitente del frame ABIERTOS** con la evidencia recogida aquí.

## 1. Sintoma: el display quedaba fijo a un buffer temprano

`games/400_rpg_slice` se veía **fijo**: solo los parches dinámicos (jugador, pócimas, lobos) sobre negro, sin sala ni HUD, idéntico frame a frame en toda la secuencia de capturas.

- La sala y el HUD viven en la imagen Chip `m_room_img` (HUD fusionado), que `render` copia a la pantalla cada frame (`blit_plane_major`).
- La sonda por canal lateral mostró que la copperlist **sí alternaba** `BPL1PT` entre los dos buffers (`0x15060`/`0x1F070`, parcheo de `commit`), pero **ambos buffers estaban a cero** salvo parches: la copia de sala nunca se ejecutaba.
- **Causa raíz**: `games/400_rpg_slice/build.args` contenía `DEMO_FLAGS=-DK_SLICE_NO_ROOM=1` (ablación de la sesión anterior, commiteada junto al vertical slice). Con ese flag, el blit de la sala queda fuera (`#ifndef K_SLICE_NO_ROOM` en `render`) y el HUD (que va dentro de la imagen) tampoco se publica.
- **Fix**: eliminado `build.args` (la ablación sigue disponible por `DEMO_FLAGS` en el entorno o en `build.args` temporal, como en la demo 001). Verificado: la aldea, el bosque y la torre renderizan con HUD (`RPG SLICE v1.0.4269 F ... HP.. ATK..`, `G.. PELT.. POT.. OBJ:...`).

Nota de herramienta: la lectura de **registros custom por canal lateral no es fiable** (`gdb_try_read_byte` usa una instantánea de `save_custom` cacheada la primera vez, `WinUAE-DBG/od-win32/barto_gdbserver.cpp:840-870`); la evidencia válida fue el **contenido de la copperlist** y la **memoria**, no `$dff0e0`.

## 2. Sintoma: el guion determinista se atascaba

Con el render visible, la crónica se quedaba en la aldea (`OBJ:HUNT 3 WOLVES`, jugador parado):

- El paseo del guion mueve **un eje por frame de paso (X preferido)** y no mira bloqueos: en `(5,10)` → `(7,11)` choca con la anciana `(6,10)` y se queda empujando indefinidamente; igual al volver del mercader (`(11,10)` → `(18,10)`), y al cruzar la fila 5 del bosque (árbol en `(11,5)`).
- Los dos pasos de "espadazo" por lobo no matan: `player_attack` tiene **enfriamiento 18** (`main.cpp`) y los flancos de fuego del guion caen cada **12** frames → el segundo golpe se descarta, el lobo sobrevive, persigue y acaba matando al jugador (respawn en la cama) y desviando el guion (sin pieles → sin llave → puerta del torreón).

- **Fix** (validado con un simulador host del paseo/combate antes de tocar el juego):
  - `script_input`: rodeo de obstáculos (si la casilla del eje X está bloqueada, el paso va por Y) y comentario de por qué (el pathfinding real llegará con la integración RPG↔sim).
  - Waypoints `(0,7,8)` (evita el bolsillo del anciano) y `(0,11,11)+(0,18,11)` (rodea al mercader por la fila 11).
  - **Tres amagos por lobo**: el golpe efectivo cae en el 1.º y 3.º flanco (12 < 18); dos pasos dejaban al lobo vivo.
- Verificación: secuencia visual con la crónica completa hasta la torre (`POT2` recogidas, `PELT3`, venta y llave, `OBJ:SELL PELTS GET KEY` → `OBJ:RELIC IN THE TOWER`).

## 3. ABIERTO: rendimiento (~12-21 fps) y cuelgue intermitente

Contexto honesto: el "48,8 fps" de la sesión anterior se midió con la **ablación activa** y con el guion atascado (el juego dibujaba 4 blits/frame y nada más). Con el juego real:

- `measure-fps` da **~12-14 fps** con la copia de sala y **~19 fps** con las dos ablaciones (sin copia de sala y sin parches): **el cuello no está en el render**.
- Perfil (`winuae-profile.mjs`): **CPU 100 % ocupada en cada campo** (142 102 ciclos ocupada, 0 libres); el DMA de blitter por campo es ~0 y el de bitplane 20,5k.
- Muestreo de PC por canal lateral: ~68 % del tiempo en un bucle diminuto del **camino de entrada/latido** (`tick_body`/productores de entrada inlined; `amiga_os.cpp:172-236`, `input.hpp:103-129`) y ~19 % en `cook_room` (cocido por CPU de la imagen de sala: `main.cpp:636-657`, ~5-6 M ciclos por cocción).
- **Cuelgue intermitente**: en varias corridas el frame del juego se queda **congelado** (p. ej. frame 176, `state=3`) mientras el contador de VBlank del mini-SO (`g_frame`) sigue a ~50/s; con PC clavado en el mismo bucle del tick y **SR=0x0000** (patrón de CPU parada dentro de la ruta del latido), pila con retornos de `VBlankHeartbeat::run` (`engine.hpp:108-120`). Relacionado con la nota conocida de `212-vblank-irq-apagada-por-el-so.md` (camino de `Wait()` bajo SO vivo).
- Ablaciones usadas (solo diagnóstico): `DEMO_FLAGS=-DK_SLICE_NO_ROOM=1` (sin copia de sala), `-DK_SLICE_NO_DYN=1` (sin parches), ambas juntas.

**Siguiente paso propuesto**: atacar el bucle del tick de entrada como frente propio del engine (comparar `wait_vblank` con `thunk` vs señal, `vblank_signal_armed`, y el sondeo de entrada por VBlank), y de paso el **cocido de sala** (tabla precalculada por tile en vez de `tile_pixel` por píxel, AGENTS §1.15).

## Evidencia y herramientas

- Sondas (en `out/tmp`, ignorado): `rpg400_probe{1..10}.mjs` (copperlist/buffers/poke/screenshots), `rpg400_launch_long.mjs`, `rpg400_resume.mjs`, `rpg400_pcsample.mjs` (muestreo de PC + resolución por `nm`/addr2line), `rpg400_irqrate.mjs` (tasa de VBlank vs frame del juego), `rpg400_hang.mjs` (detección de cuelgue + pila), `rpg400_scriptsim.mjs` y `rpg400_combatsim.mjs` (simuladores host del guion y del combate).
- Medidas: `node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json`; perfil `node tools/debug/winuae-profile.mjs 400_rpg_slice A500_debug 25`.
