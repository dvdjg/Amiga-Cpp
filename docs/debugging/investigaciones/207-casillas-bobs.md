# 207 - Celdas animadas desplazadas y bitmaps incompletos

**Fecha:** 2026-10-27. **Estado:** abierto. Este documento recoge la opinión de Grok sobre el procedimiento, la corrige contra el código real y deja la primera prueba ejecutada.

## Opinión de Grok (resumen)

La propuesta de Grok mejora el método porque separa la cadena mundo → cámara → anillo → framebuffer, exige una celda aislada, un BlitJob y un checksum, y no acepta la descripción del modelo de visión como veredicto. La estructura de la escalera —host, bitmap offscreen, una celda, una capa, tres capas— es correcta.

Sin embargo, la fórmula propuesta no coincide con el contrato real del motor:

- `rx` no es `(wx - mapposx + display_offset) % 352`. `display_offset` es vertical. La referencia real es `rx = wx % bitmap_width`, en `engine/include/eng/field/xlimited_playfield.hpp:643`.
- `ry` no es `dmod2(wy - mapposy)`. La referencia real es `ry = dmod2(wy)`, en `xlimited_playfield.hpp:648`.
- La X de pantalla no es `rx - display_offset`. `display_offset` es la fila Y del anillo; la X visible la fija el compositor/hardware (`planeaddx`, `BPLCON1` y fine scroll).
- La dirección destino simplificada `ry * stride + (rx / 16) * 2` no cubre los 6 planos intercalados; hay que usar `BobTarget` (`row_bytes`, `plane_count`, `plane_step`) y el desplazamiento fino.
- La costura horizontal no es la vertical de `bob_draw_ring`: un tile con `rx + 16 > 352` cruza el final del anillo y necesita partirse o cullarse explícitamente.

**Corrección tras la prueba**: en este pipeline de tile animations, `add_draw` y `draw_group` usan la misma X que el bloque base, que es `sx` (pantalla), no `rx` (anillo de mundo). Cambiar el destino a `rx` produjo una **segunda fogata** en pantalla, con el fuego repetido en otra posición. `add_world_bitmap_masked` usa `rx`/`dmod2(wy)` porque es el contrato de BOBs de mundo; no es el oráculo del parche de tiles. El oráculo correcto para una celda animada es el **mismo camino que su bloque base**, comparando el rectángulo resultante. Culling en pantalla y destino del parche deben compartir `sx` y `ring_base + sy`, pero el destino no debe reinterpretarse como `rx`.

## Contrato real comprobado

> **Corrección posterior (ver «Cierre de la investigación»):** el destino correcto del parche es `wx` (columna de mundo sin envolver); `sx` sólo decide visibilidad. El contrato `target_x = sx` de esta sección describía la instrumentación antigua y desplazaba el overlay `camx` píxeles a la izquierda.

Con una celda de mapa `(tx, ty)`:

```text
wx = tx * 16
wy = ty * 16
sx = (wx - mapposx) mod 640
sy = (wy - mapposy - 16) mod 640
target_x = sx
target_y = (display_offset + sy) mod display_height

# BOBs de mundo / add_world_bitmap_masked (NO el tile animator):
rx = wx % bitmap_width
ry = dmod2(wy)
```

El parche usa `target_x = sx` y `target_y = ring_base + sy`, el mismo camino que el bloque base. La prueba con `target_x = rx` generó una fogata duplicada; esa hipótesis queda refutada. `add_world_bitmap_masked` sigue siendo el oráculo para actores/BOBs de mundo, no para estas celdas.

## Prueba ejecutada: fase 0/1

Comando:

```bash
EXTRA_DEFINES="-DDEMO_BLIT_STATS=1" \
  bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --release
node dist/tools/run/run-demo.js demos/techniques/amiga/playfield/207_tiled_live_layers_animation \
  --config A500_demo_blit_stats1_release --settle-ms 1500
```

El HUD añade contadores por capa (`B0/B1/B2`). Resultado de la primera parada:

```text
F=43  X=112 Y=376  A=0 G=76 J=7 B0=1 B1=0 B2=0
```

- `B0=1`: se copió al menos una celda de flores, no todas estaban diferidas.
- `B1=0/B2=0`: en esa cámara no había celdas visibles de agua/fogata en el turno, o no cambiaron.
- Un grupo de 4 celdas necesita 24 jobs; el límite de 18 lo difería siempre. El límite se subió a 24.
- Medida release normal: ~29,4 fps de media con mínimo ~23,8; no alcanza el suelo de 25 de forma estable.
- Prueba de celda única: `DEMO_DEBUG_ONE_CELL` con `set_test_cell(5)` (Flowers, celda 5), cámara congelada en `(0,0)`; el HUD da `B0=1` y se ve la flor en pantalla. El filtro y el grupo de 4 celdas funcionan en escena mínima. Queda pendiente el checksum del rectángulo y la lectura del BlitJob por GDB.
- Caché de entrada: bits por celda; no se repinta cada frame. Al cambiar la cámara se marcan/dibujan solo las celdas sin bit. Mejora la cobertura, pero el coste baja el mínimo a ~23,8 fps; todavía no cumple el suelo.

La prueba **no demuestra todavía** que todas las celdas estén completas: hay que aislar una celda de flores y comprobar el rectángulo de anillo con un checksum, como pide Grok.

## Recortes para Ollama

Se añadió `tools/analyze/ollama-crop.mjs`. Envía únicamente el recorte, sin HUD ni resto de escena:

```bash
node tools/analyze/ollama-crop.mjs captura.png \
  --crop 300,120,220,180 --scale 4 \
  --prompt "Describe únicamente este recorte de flores/fogata. No hables de otras zonas."
```

La prueba con un recorte de fogata/flores devolvió una descripción limitada a ese recorte. No se usa como veredicto: el veredicto debe ser checksum/diff de píxeles.

## Siguiente experimento

1. Aislar una celda de `Flowers` (grupo 2, `(4,1)` o una celda visible) con fondo sólido.
2. Calcular a mano `rx/ry` y leer por GDB el destino del BlitJob antes/después.
3. Repetir con cámara sin costura y con `rx` cerca de 352.
4. Solo entonces aplicar la corrección y probar una capa completa.

## Deuda

- El parche actual conserva `sx` en `add_draw/draw_group`; la corrección a `rx/ry` y la costura horizontal siguen pendientes.
- La caché de celdas que entran/salen está sin resolver; el límite de 24 evita el diferido sistemático de grupos de 4, pero no garantiza la entrada.
- La captura secuencial no es fiable para animación; usar pausa y lectura de memoria/checksum.

## Sonda corregida: checksum por plano y costura (2026-10-27)

La lectura anterior solo guardaba el **último** BlitJob y sumaba palabras contiguas. El destino es interleaved: cada fila de un plano dista `row_bytes * planes = 264` bytes, no 2. La sonda de `207/src/main.cpp` ahora:

- captura todos los jobs enmascarados de la celda de prueba antes de `execute_frame_plan`;
- guarda una copia de las 16 filas de cada plano;
- calcula `expected = (source & mask) | (before & ~mask)` para cookie-cut `$CA`;
- verifica que `after == expected` fila a fila y expone `M` (jobs con mismatch) y `N` (jobs capturados) en el HUD;
- conserva la última captura verificada para poder leerla aunque el frame actual no tenga jobs.

La lectura se hizo por el canal lateral y una búsqueda del patrón `ANIM` en RAM, sin depender de la resolución de símbolos por `.map`.

### Celda 5, cámara (0,0)

Cabecera: `target=0x0005BFA0`, `row_bytes=44`, `planes=6`, `jobs=6`, `cell=5`, `sx=64`, `ry=16`, `mismatch_count=0`.

| Plano | destino | source | mask | before | expected | after |
|---|---|---|---|---|---|---|
| 0 | `0x0005D028` | `0x0004D170` | `0x00058180` | 255176 | 255368 | 255368 |
| 1 | `0x0005D054` | `0x0004D190` | `0x00058180` | 781019 | 781771 | 781771 |
| 2 | `0x0005D080` | `0x0004D1B0` | `0x00058180` | 506861 | 506285 | 506285 |
| 3 | `0x0005D0AC` | `0x0004D1D0` | `0x00058180` | 510812 | 511692 | 511692 |
| 4 | `0x0005D0D8` | `0x0004D1F0` | `0x00058180` | 40333 | 39709 | 39709 |
| 5 | `0x0005D104` | `0x0004D210` | `0x00058180` | 129025 | 129025 | 129025 |

Los seis planos cumplen `after == expected`; `M=0`, `N=6`. El destino del plano 5 coincide con el cálculo manual `0x5BFA0 + 16*6*44 + 5*44 + 8 = 0x5D104`.

### Celda 116 en el borde, cámara (48,144)

Cabecera: `cell=116`, `sx=304`, `ry=224`, `jobs=6`, `mismatch_count=0`. Los destinos `0x0006A6C6`, `0x0006A6F2`, `0x0006A71E`, `0x0006A74A`, `0x0006A776` y `0x0006A7A2` cumplen `after == expected`; `M=0`, `N=6`. La celda ocupa la última zona visible a la derecha (`sx=304`, 16 px) sin cruzar la fila de 352 px del anillo.

### Caché de visibilidad

`m_anim_cell_visible` era un bit pegajoso: una celda que ya se había pintado no se repintaba aunque la cámara la moviera a otra posición del anillo. Ahora la caché guarda también `sx` y `ring_y`; solo se salta el repintado si la celda sigue en la misma posición y su grupo no cambió. Las celdas culled limpian su bit.

### Test host

`tests/host/field/500_tile_animation_edge` valida sin emulador:

- el tile en `x=336` ocupa la última palabra de la fila de 352 px;
- el cruce `ring_y=280 + 16` se parte en dos segmentos de 8 filas;
- los segmentos conservan fuente, máscara, minterm `$CA` y el stride interleaved.

Comando: `bash tools/run-host-tests.sh tests/host/field/500_tile_animation_edge` → `HOST-500: borde de anillo OK`.

### Estadísticas del HUD

`B0/B1/B2` se resetean cada frame. Cuando la captura muestra el último checksum de un frame anterior, `M/N` podía ser distinto de `B0/B1/B2`. El HUD usa ahora `m_last_patch_stats`, copiado solo cuando el parche de ese frame contiene jobs; así el contador de capa y el checksum corresponden al mismo frame de trabajo.

### Secuencia y fps

- Secuencia de 4 frames cada 250 ms: el modelo de visión detecta desplazamiento horizontal del contenido; no se usa como prueba de píxeles.
- `measure-fps` con `A500_release`: media 29,01 fps, mínimo 21,39, máximo 38,91; **cadencia inestable**, no cumple el mínimo de 25 fps.

### Genericidad de `TileAnimator`

La sonda de la demo 207 usa 6 planos y tiles de 16 px, pero el camino común ya no fija esos valores:

- `TileAnimator::plane_count()` y `TileAnimator::tile()` exponen la configuración.
- `draw_group` calcula `words_per_row = (tile + 15) / 16`, el offset de plano de fuente y el byte de fila; no usa `32` fijo.
- `configured()` valida frame/máscara contra `planes` y `tile`; el tile debe ser múltiplo de 16 para quedar alineado a palabra.
- `patch_animations` deriva `jobs_per_cell` de `plane_count()` y usa la capacidad real de `FramePlan`, no el 6 de la demo.
- HOST-500 cubre 6 planos/16 px, 3 planos/16 px, 32×32, y el cruce de anillo.
- La sonda de checksum de la demo sigue siendo específica de 16 px/6 planos; es instrumentación de diagnóstico, no forma parte de `TileAnimator`.

### Alcance

El checksum valida los **seis planos** de la celda 5 y de la celda 116 en las cámaras indicadas, y el test host valida la geometría del borde. La capa completa se ha movido en una secuencia, pero sigue mostrando celdas rojas/oscurecidas en la zona inferior derecha, **SIN VERIFICAR**; el mínimo de fps tampoco se cumple.

## Contrato de fondo de las celdas vivas

Esta decisión pertenece al runtime Tiled/surface, no al documento de estilo:

- el mapa cocinado está pensado para acelerar el scroll; no es la fuente de fondo de una celda viva;
- cada celda animada debe capturar su fondo real mediante el cache/save-under del surface sprite map antes de pintar el overlay;
- la captura se restaura en la misma posición del anillo;
- no se debe usar `add_draw` ni el tile del bake para reconstruir ese fondo;
- las celdas de un objeto compuesto comparten sincronización; el reparto por paridad no puede separar partes de una misma fogata.
- En Beginning Fields, la fogata son las celdas `0`, `9`, `17` y `25`, repartidas en los grupos `0`, `3`, `6` y `9`; las cuatro deben actualizarse en el mismo pase aunque la paridad reparta otros grupos independientes.

## Capa completa / escena

- 207 release tras corregir la visibilidad y el límite: una sola fogata en la primera parada (la duplicación de la prueba `rx` desapareció). Persisten celdas rojas/oscurecidas en la zona inferior-derecha; no se declaran corregidas.
- 208 release: actor y cartel se ven; el cartel usa overlay trasero y el actor no deja estela. No se ha medido aquí el mínimo de fps con la caché nueva.
- La caché de entrada mantiene bits por celda y dibuja solo las que no tienen overlay; no repinta toda la capa en cada frame. El coste aún baja el mínimo de 207 a ~23,8 fps.

## Qué ayudó / qué no

- Ayudó: limitar el grupo de 4 celdas a 24 jobs, el filtro de celda única, la cámara congelada, el recorte aislado y el contador por capa.
- No ayudó: usar `rx` como destino del parche (duplicó la fogata); el modelo de visión con la escena completa; la secuencia de frames para medir animación.
- Sigue abierto: checksum del rectángulo, lectura GDB del BlitJob, costura horizontal y el mínimo de 25 fps.

## Intento GDB (2026-10-27)

- Se consiguió arrancar WinUAE con `runner.uae` y GDB quedó conectado; el `winuae_print` resolvió mal `g_debug_anim_blit` (devolvió una dirección de `.rodata`/texto), así que la lectura directa no es fiable con la resolución por mapa actual.
- Se localizó el símbolo en `.bss`: enlazado `0x6b592`, sección `.bss` en `0x6b4b0`, offset `0xe2`. La base `bss` de runtime fue `0x00c2b1e0`, luego la dirección manual sería `0x00c2b2c2`; la lectura allí no mostró un `dest` válido, por lo que hay que revisar la resolución de bases/secciones antes de usarla como prueba.
- Siguiente intento: poner un valor mágico al inicio de la estructura, romper en `patch_animations_impl` después del `draw_group`, y leer el global por GDB en ese stop; no usar `winuae_print` como oráculo hasta corregir la base.

Esta parte no cierra 1–4: falta el valor real de `dest`, el checksum y la costura horizontal.

## Resultado GDB de la celda 5

- La resolución por mapa siguió dando una dirección incorrecta; el global se localizó buscando el patrón `ANIM` en memoria. Dirección real: `0x00c30f7a`.
- Lectura: `magic=414E494D`, `dest=0x0005D104`, `source=0x0004D150`, `mask=0x00058160`, `words_per_row=1`, `height=16`, `cell=5`, `screen_x=64`, `ring_y=16`, `display_offset=16`.
- BPL1PT en ese frame: `0x0005BFA0`; con `row_bytes=44`, 6 planos, plano 5 y `xbyte=screen_x/8=8`, la dirección esperada es `0x5BFA0 + 16*6*44 + 5*44 + 8 = 0x5D104`, exactamente el `dest` del BlitJob. La coordenada del parche coincide.
- Pendiente: checksum antes/después del blit y repetición en la costura horizontal. El hallazgo valida el destino, no aún los píxeles copiados.
- Añadidos `magic`, `target_base`, `row_bytes`, `plane_stride`, `planes` y checksums antes/después al global; la build normal no los incluye.
- El segundo intento de búsqueda por patrón en caliente agotó el tiempo del MCP; no se obtuvo el checksum en esta sesión. El destino ya está verificado, pero el checksum y la costura siguen pendientes.
- Comando estable: arrancar `runner.uae`, reset, `continue`, pausar tras unos segundos, buscar el patrón `414E494D` en RAM y leer `dest/source/mask/checksum_before/checksum_after`.

## Cierre de la investigación (2026-10-28)

### Causa de la ejecución inestable (Kickstart / frame 0)

La demo llamaba a `scene.patch_animations(plan, 0u, patch_stats_out)` con `patch_stats_out = nullptr` en la build normal. La sobrecarga con estadísticas instanciaba `Collect=true` y hacía `*stats = TilePatchStats{}` sin comprobar el puntero: en el 68000 la escritura caía en la dirección 0 (tabla de vectores / `ExecBase`), corrompía memoria baja y dejaba la máquina colgada o en Kickstart. El `run-status` seguía leyendo READY porque se había escrito antes del parche.

Corrección: la demo usa la sobrecarga sin estadísticas en la build normal, y la sobrecarga con `TilePatchStats*` del playfield cae a `Collect=false` si el puntero es nulo (`xlimited_playfield.hpp`, `patch_animations(..., stats)`). Con esto la demo avanza frames y muestra el mapa.

### Posición correcta de una celda viva

El contrato del parche es el **mismo que el bloque base**, en coordenadas de mundo:

```text
sx = (wx - camx) mod map_width          # sólo visibilidad (ventana)
sy = (wy - camy - tile_h) mod map_height
ring_y = (display_offset + sy) mod display_height
target_x = wx                            # columna de mundo SIN envolver
```

`wx` es la columna física que pinta el bloque base (puede caer en la zona "unrolled" del anillo); no hay que restarle la cámara. Con `target_x = sx` el overlay quedaba desplazado `camx` píxeles a la izquierda: en la primera parada (cam 112,376) la fogata aparecía en pantalla x=32 en vez de x=144. La prueba antigua con `rx = wx % bitmap_width` duplicaba la fogata porque sólo cambiaba el parche y la pasada inicial (`draw_animation_stack`) seguía usando `sx`; el destino del checksum GDB de la celda 116 también usaba `sx`. El oráculo no es el valor que se escribió, sino la columna que el display lee para esa celda.

### Save-under real del surface

`MapCellSaveCache<Capacity>` (`engine/include/eng/graphics/map_sprite.hpp`) gestiona el fondo real de cada celda:

- **un único bloque Chip** de `Capacity × cell_bytes` (96 × 192 B = 18 KB en 207), no una reserva por celda (el `BlockPool` tiene 64 huecos y fragmentaba);
- **una captura por celda y posición** (`CopyRect` de todos los planos en un solo job, `bitplane_count = planes`);
- el fondo se **conserva** mientras la celda está visible: al cambiar el frame se hace `restore` (`RestoreRect`), no se recaptura; si la celda sale de la ventana, `release` libera la entrada sin restaurar (el scroll repinta la columna al volver a entrar);
- nunca se reconstruye con `add_draw` ni con el tile del `Baked`.

El overlay y el save-under comparten el direccionamiento del anillo (`x = wx`, `y = ring_y`, paso entre planos `row_bytes`, módulo `row_bytes × planes - palabras × 2`). El parche no necesita el mapa cocinado para el fondo.

### Fogata completa y reparto

El parche recorre las capas de **primer plano a fondo** (`TileAnimator::group_layer`) antes de repartir el presupuesto. La fogata 2×2 se fusiona automáticamente en un grupo de 32×32 (1 celda) en la capa superior; con un tope de **24 jobs** (3 por celda: restore + captura + frame) cabe en el mismo pase y nunca se queda media fogata con el frame anterior. Las capas de agua/flores siguen repartiéndose con el cursor round-robin; el tope se toma de `plan.blit_budget_limits().max_jobs` (24 en la demo).

### Medidas finales

- Release completa, visita con fusión automática: **29,12 fps de media, mínimo 22,58**, 243 603 ciclos/frame (1,717 campos/frame). La media supera el suelo de 25; el mínimo de ventana sigue por debajo y la cadencia no es 50 Hz (deuda abierta). Frente a los 27,13/21,17/261 468 previos, la fusión de la fogata en un sprite redujo el coste.
- Parada de agua congelada (`DEMO_TOUR_STOP=1`): **24,71 fps**, 287 046 ciclos/frame; sin parche (ablación): **49,90 fps**, 142 150 ciclos/frame. El coste es el parche, no el scroll/HUD.
- El `%` por grupo visitado (`__umodsi3`) era el coste dominante: sustituido por avance circular (`next_group`), la parada subió de 18,18 a 24,69 fps.
- Emitir **un job por celda** con todos los planos (en vez de uno por plano) redujo el coste de CPU del parche sin cambiar el trabajo del Blitter.

### Verificación visual

- Secuencia de la parada de la fogata (8 frames): la fogata cambia de silueta con el anillo de piedras y la hierba estables; las flores blancas/rojas y el agua cambian sus frames. Diff de región estática = 0; los recortes 4× de fogata/flores muestran el cambio sin restos ni bloques negros/rojos.
- La posición de la fogata coincide con su celda base (pantalla 144 en la primera parada), no con la posición desplazada de `sx`.
- HOST-500 actualizado a la geometría de un job por segmento; HOST-496 añadido para el ciclo de vida y la geometría de `MapCellSaveCache`.
- La capa de objetos (fogata) se cuece automáticamente como **un grupo de 32×32** cuando el cluster rectangular de celdas contiguas comparte cadencia y sus gids son de un solo uso; no es una regla específica de la fogata. El análisis de objetos libres, bounding boxes y caché precompuesta está en [`TILED_MAP_RUNTIME.md` §5.4.1](../../engine/architecture/TILED_MAP_RUNTIME.md#541-objetos-libres-y-caché-precompuesta-qué-aplica-y-qué-no).
