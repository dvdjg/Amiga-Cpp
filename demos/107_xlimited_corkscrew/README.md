# 107_xlimited_corkscrew

Demo del algoritmo **XYLimited / corkscrew** de Georg Steger (ScrollingTricks):
scroll **8-way** con el truco X-Limited extendido a vertical, sin el ruido del
controlador circular de `TileFieldController`.

## Qué demuestra

- Un campo **interleaved** (`BMF_INTERLEAVED`) de 352×304 (ó 384 con fetch
  ancho) que hace scroll **horizontal y vertical infinito** con un mapa de
  320×256 tiles de 16×16 sin tearing.
- **Bucle vertical de display** `display_height = viewport_h + 2*tile_height`
  (288 para 320×256): el display envuelve al llegar al final y un **split de
  Copper** vuelve a la fila 0. La fila/columna entrante se **pre-pinta en la
  banda de staging** de 2 bloques (`block_videoposy`, `y = block_videoposy*planes`)
  que el display alcanza al dar la vuelta — el invariante "corkscrew" del original.
- Cada píxel es como máximo 1-2 blits de `BLOCKPLANELINES` líneas
  (`16*planes = 64` para 4 planos). El `x` de la columna entrante es
  plane-shifted a la derecha (`BITMAPWIDTH + ROUND2BLOCKWIDTH(videoposx)`) y el
  `y` de la fila entrante usa `(block_videoposy + mapy*tile_height) % display_height`.
- La guarda de 1 word (`saveword`/`savewordpointer`) evita el artefacto de
  2 bytes al invertir la dirección, igual que en `xlimited.c`/`XYLimited`.
- `TWOBLOCKSTEP = bitmap_blocks_per_row - tile_height` (22-16=6 para 352 px):
  2 bloques por paso mientras `stepy < TWOBLOCKSTEP`, 1 después, para cubrir la
  fila completa en 16 píxeles de scroll vertical.

La demo es deliberadamente **single playfield de 4 planos** (16 colores) para ver
el corkscrew puro. No hay `surface_origin` ni bandas del modelo circular: el
posicionamiento físico es `frontbuffer + y*BITMAPBYTESPERROW + x` con `y` en
planelíneas.

## Ciclo de fases (seleccionable con `K_EFFECT` / `K_START_PHASE`)

El scroll avanza 1 px/frame en un ciclo de **8 fases** (las dos direcciones de
cada eje, para verificar la composición de tiles en ambos sentidos). Cada fase
dura `K_PHASE_FRAMES` frames (defecto 1000 ≈ 20 s a 50 fps):

| Fase | Movimiento |
|---|---|
| 0 | Horizontal derecha |
| 1 | Horizontal izquierda (pre-scroll derecho en init) |
| 2 | Vertical abajo |
| 3 | Vertical arriba (pre-scroll abajo en init) |
| 4 | HV alternando (derecha/abajo por frame) |
| 5 | HV alternando (izquierda/arriba por frame, pre-scroll ambos) |
| 6 | Diagonal Lissajous (sin(x), cos(0.7x)) |
| 7 | Diagonal Lissajous inverso (desfase +32) |

Las fases con componente negativa (1, 3, 5, 6, 7) hacen un **pre-scroll** de
`K_PRE_SCROLL` px en init para que `scroll_left/up` tengan recorrido antes de
chocar con el borde 0 (el original devuelve `false` en 0).

Todas usan el port corkscrew (XYLimited) de `xlimited.hpp` (§ Scroll corkscrew),
verificado bloque a bloque contra `Scroller_XYLimited/main.c`.

Verificado en WinUAE-DBG (2026-08-31): V abajo y V arriba (0 % negro, contenido
moviéndose ±1 px nativo/frame), H izquierda (columna entrante por el borde
izquierdo compuesta sin huecos, qwen3-vl), y el ciclo completo arrancando en
cualquier fase (`K_START_PHASE`).

## Parámetros de compilación

| Macro | Valores | Efecto |
|---|---|---|
| `K_TILE_WIDTH` | `16`, `32` | Anchura de tile en píxeles (múltiplo de 16). 32 usa 2 words por fila. `bitmap_width` se deriva de `viewport + EXTRAWIDTH` (352 normal / 384 con fetch ancho); con 32 y fetch normal es 352. |
| `K_FETCH_MODE` | `0`, `1`, `3` | Modo de fetch del Agnus. `0`=16 px normal (352 px, `DDFSTRT=$30`/`$D0`, `bitmapoffset 0`, `modulo 2`, scroll 16). `1`=BPL32 32 px (384 px, `DDFSTRT=$28`/`$C8`, `bitmapoffset 16`, `modulo 4`, scroll 32). `3`=BPL32+BPAGEM 64 px (384 px, `DDFSTRT=$18`/`$B8`, `bitmapoffset 48`, `modulo 8`, scroll 64). Cuando `K_FETCH_MODE !=0` la demo fuerza `BITMAPWIDTH=384` y `fetch_mode=K_FETCH_MODE` independientemente de `K_TILE_WIDTH`. |
| `K_PLANES` | `4` (defecto) | Profundidad. 4 es el caso canónico de X-Limited; otros valores sólo para pruebas de altura extra. |
| `K_EFFECT` | `0` (defecto) | Efecto/dirección a mostrar. `0`=todas en ciclo; `1`=H derecha, `2`=H izquierda, `3`=V abajo, `4`=V arriba, `5`=HV der/abj, `6`=HV izq/arr, `7`=diagonal, `8`=diagonal inverso. Las fases con componente negativa (izquierda/arriba) pre-scrollan `K_PRE_SCROLL` px en init para tener recorrido. |
| `K_START_PHASE` | `0` | Fase inicial del ciclo `K_EFFECT=0` (0..7). Útil para saltar a una dirección concreta dentro del ciclo. |
| `K_PHASE_FRAMES` | `1000` | Frames por fase en el ciclo (~20 s a 50 fps). |
| `K_PRE_SCROLL` | `1024` | Píxeles de pre-scroll hacia delante en init cuando la fase inicial es reversa/diagonal, para que `scroll_left/up` tengan recorrido antes de chocar con el borde 0. |
| `K_DUAL` | `0` (defecto), `1` | DPF 3+3: dos `XlimitedField` (PF1 planos 1,3,5 / PF2 2,4,6) unidos por `XlimitedDualComposer`. Cada playfield usa 3 planos; PF1 con fondo transparente tramado para que se vea PF2. Ambos scrollean en la misma dirección (`K_EFFECT`) y comparten `videoposy` (mismo split). |
| `K_PARALLAX` | `0` (defecto), `1` | En `K_DUAL=1`, PF2 (fondo) scrollea a media velocidad en X (parallax); en Y siempre igual para mantener el split. |
| `K_LINEAR` | `1` (defecto) | **Display lineal sin split** (espejo del bucle): el bitmap duplica el bucle vertical y el wrap se lee de forma contigua. Elimina por completo el artefacto del split en raster 256..296 (el comparador de WAIT del Copper es de 8 bits y no puede esperar esas líneas), a costa de 2× blits (dibujo + espejo). Con `0` se usa el corkscrew clásico con split (misma limitación que el original `XYLimited`). |

Nota: los `-D...` se pasan siempre por la variable `EXTRA_DEFINES` (los argumentos CLI `-DK_*` no los recoge `build-demo.sh`).

`BITMAPWIDTH` es `352` (`BLOCKSPERROW=22`, `44 bytes` por planelínea) en modo normal y `384` (`BLOCKSPERROW=24`, `48 bytes`) con tiles de `32` o con fetch ancho. `BITMAPBYTESPERROW` es `44` ó `48` y `BPL1MOD/BPL2MOD = BITMAPBYTESPERROW*planes - 40 - moduloOffset` (2, 4 ú 8 según el modo).

`BPLCON1` replica el desplazamiento fino en ambos nibbles (`(fine &15)*0x11`) y añade los bits de fetch ancho: `fine &16 → 0x4400`, `fine &32 → 0x8800` (ver `xlimited.c:304-311` y `engine/include/eng/field/xlimited.hpp §4`).

```bash
# 352 píxeles, tiles 16, 4 planos (modo por defecto, fetch 16 px, ciclo completo)
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Solo vertical abajo (K_EFFECT=3) — para verificar la composición vertical
EXTRA_DEFINES="-DK_EFFECT=3" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Solo vertical arriba (K_EFFECT=4) — composición inversa (columna/fila entrante por arriba)
EXTRA_DEFINES="-DK_EFFECT=4" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Solo horizontal izquierda (K_EFFECT=2) — pre-scroll derecho y composición inversa
EXTRA_DEFINES="-DK_EFFECT=2" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# DPF 3+3 (dos capas): PF1 transparente sobre PF2, ambos scrollean
EXTRA_DEFINES="-DK_DUAL=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# DPF 3+3 con parallax en X (PF2 a media velocidad)
EXTRA_DEFINES="-DK_DUAL=1 -DK_PARALLAX=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Ciclo completo empezando por V arriba (fase 3) y 500 frames por fase
EXTRA_DEFINES="-DK_START_PHASE=3 -DK_PHASE_FRAMES=500" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Tiles de 32 píxeles (misma altura, doble anchura, fuerza 384)
EXTRA_DEFINES="-DK_TILE_WIDTH=32" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Fetch ancho 32 px (BPL32, 384 px, DDF $28/$C8, offset 16, scroll 32)
EXTRA_DEFINES="-DK_FETCH_MODE=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Fetch ancho 32 px + tiles 32 px (384 px, 24 bloques por fila)
EXTRA_DEFINES="-DK_TILE_WIDTH=32 -DK_FETCH_MODE=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Fetch ancho 64 px (BPL32+BPAGEM, 384 px, DDF $18/$B8, offset 48, scroll 64)
EXTRA_DEFINES="-DK_FETCH_MODE=3" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
```

## Verificación

- **Test host**: `node tools/analyze/verify-xlimited.mjs` modela direcciones
  lineales, fetch contiguo, altura extra `viewport_h+EXTRAHEIGHT? + (map_width/blocksPerRow/planes)+1+3`,
  guarda de 1 word y la geometría del corkscrew (§11: `display_height`,
  `BITMAPBLOCKSPERCOL`, `TWOBLOCKSTEP`, `block_videoposy`, split).
- **Port vs original**: `node tools/analyze/verify-corkscrew.mjs` (test host)
  compara **bloque a bloque** los 4 scrolls del port (`XlimitedField`) contra una
  réplica fiel de `Scroller_XYLimited/main.c` para el config de la demo
  (secuencias D/U/R/L, XY mezclado y 5000 pasos aleatorios). Debe imprimir `OK`.
- **Análisis visual**: `bash ./tools/analyze/analyze-demo.sh demos/107_xlimited_corkscrew`
  o `analyze-sequence.sh` si existe.
- **Secuencia corkscrew** (`demos/107_xlimited_corkscrew/analyze-sequence.sh`):
  valida 1) scroll continuo a la derecha 100 frames, 2) inversión brusca a la
  izquierda 1 frame sin hueco de 2 bytes (`saveword`/`savewordpointer`), 3)
  fila/columna entrante en la banda de staging y split vertical.

  ```bash
  # Secuencia completa (requiere WinUAE-DBG; no asume toolchain en PATH salvo AMIGA_BIN_PATH)
  bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
  bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
  # Sin warp (tiempo real, para evaluar suavidad)
  bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh
  ```

  El flag `--warp` propaga `warp=true` a WinUAE vía `tools/run/run-demo.sh`
  (usa `AMIGA_BIN_PATH` como fallback del toolchain, igual que `build-demo.sh`).
  En `tools/test-regression.sh` la regresión invoca automáticamente
  `analyze-sequence.sh --warp` cuando existe.

  **Validación por dirección (`EFFECT` env)**: compila la demo con `K_EFFECT=$EFFECT`,
  captura una secuencia y valida la dirección del contenido (cross-correlación en
  el eje esperado) + ausencia de bandas negras, reconstruyendo el defecto al final:

  ```bash
  # Scroll vertical arriba (dirección inversa, la más crítica)
  EFFECT=4 bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
  # Scroll vertical abajo / horizontal izquierda
  EFFECT=3 bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
  EFFECT=2 bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
  ```

  Ejes/signo esperados (sobre el contenido en pantalla): `1`→x−, `2`→x+, `3`→y−,
  `4`→y+, `5..8`→solo movimiento + no-negro (mixto).

## Evidencia runtime (WinUAE-DBG, 2026-08-31)

Verificado el **scroll vertical** (fase V, cámara bajando) con captura de 60
frames a 20 ms y análisis determinista + visión local (qwen3-vl):

- **Sin banda negra al pie**: 0 % de negro en la fila inferior e interna del
  viewport (el bug original dejaba la fila entrante en negro/basura).
- **Contenido desplazándose arriba ~1 px/frame** (mediana −1 px nativo), que es
  el signo correcto de una cámara bajando (`scroll_down`).
- **Tiles bien formados, sin tearing** (qwen3-vl: "no hay filas negras ni
  tearing, los tiles se ven bien formados").
- La fase H (derecha) pasa `analyze-sequence.sh` completa (100 frames,
  `ChangedPairs=99`, `DuplicatePairs=0`, telemetría mapposx/videoposx/BPLCON1).

**Bug de la banda de staging (corregido 2026-08-31)**: `block_videoposy` se
envolvía en `bitmap_height` (304) como el original; cada 304 px de scroll
vertical la fila entrante caía en las filas extra (288..303) que el *planeaddx
walk* horizontal sí muestra, dejando un tile obsoleto en el área visible y la
banda de staging sin refrescar. Se reprodujo en mapposy=900 con videoposx=400
(demo pausada) y la comparación antes/después confirmó el artefacto y su
eliminación al envolver en `display_height` (288): "mitad izquierda con banda de
tiles obsoletos, derecha corregida" (qwen3-vl).

## Limitación OCS conocida (inherente al chipset)

El comparador de WAIT del Copper (OCS y WinUAE, `custom.cpp coppercomp`) usa
**8 bits con semántica `>=`**: `vp = vpos & 0xFF >= vcmp`. No hay bit V8 en la
comparación, por lo que no se puede esperar a una línea 256..296 (PAL). En el
**corkscrew clásico con split** (`K_LINEAR=0`, como el `XYLimited` original),
eso hace que durante `display_offset ∈ [33,73]` el split dispare en la línea 255
y la banda de staging quede visible (franja inferior detenida + tiles escritos en
el área visible). **Resuelto por defecto con `K_LINEAR=1`** (display lineal con
espejo): sin split, el wrap se lee de forma contigua del espejo y la banda de
staging nunca se muestra. Verificado en WinUAE: captura V de 120 frames
(mapposy 0..225) con **0/119 pares desincronizados** (antes 12/69).

## Modo canónico para juegos (viewport corto + split)

El modo que usan los juegos clásicos es el **corkscrew con split** en un
viewport de **224 px** (defecto de la demo), dejando abajo una franja de 32 px
de **borde negro** (o un HUD dibujado aparte). La franja inferior **no se envía
al DAC como scroll**: el compositor deriva `DIWSTOP` del viewport
(`xlimited_detail::diwstop_for_viewport`, codificación OCS:
`vstop = (diwstop>>8) | 0x100 si bit 7 claro`), así que las líneas bajo el
viewport muestran el color de borde (COLOR00 = negro) y no llegan al display
como bitplanes.

El engine expone `split_always_waitable()` (`XlimitedScene`):
`viewport_h <= 215` ⇒ el split cae siempre en raster 41..255 y es 100 % fiable.
Múltiplos de 16 limpios: **208** (13 filas de tile) y **192** (12 filas).
**224** (14 filas) es la frontera: en ~9 frames por ciclo el split caería en
raster 256..264, pero en la práctica es inapreciable (verificado 0/119) y la
franja de 32 px de borde/HUD la cubre. Para viewports mayores (256) el split
cae en 256..296 y hace falta `linear_display`.

```bash
# Modo canónico de juego (defecto): 320×224, split, 1 blit por operación, sin espejo
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
# Garantizado 100% limpio: 208 (13 filas)
EXTRA_DEFINES="-DK_VIEWPORT_H=208" bash ...
# Pantalla completa sin split (espejo, 2× blits): 256
EXTRA_DEFINES="-DK_VIEWPORT_H=256 -DK_LINEAR=1" bash ...
```

**Altura de tiles para el scroll horizontal**: con `viewport_h=224`, el scroll
horizontal muestra **14 filas de tiles** (224/16), no 16; con 208, 13. El mapa
se deriva de `K_SCREENS_Y * (K_VIEWPORT_H / K_TILE_H)`.

## Capa de dibujo: abstracción Playfield

Toda operación de dibujo pasa por un **`Playfield`** (`engine/include/eng/field/
playfield.hpp`): la capa es un objeto de primera clase que posee su framebuffer
(Chip RAM), su geometría y sus **primitivas de dibujo**. El scroll es una
**especialización** del playfield: `XLimitedPlayfield` (corkscrew) o
`CanvasPlayfield` (lienzo plano sin tiles ni scroll). La `XlimitedScene` compone
los playfields y expone **roles tipados** — `bg()`/`fg()` — nunca un índice
crudo. Las primitivas se llaman sobre el playfield:

```cpp
scene.bg().set_pixel(wx, wy, color);            // CPU, coordenadas de mundo
scene.bg().fill_rect(wx, wy, w, h, color);      // CPU
scene.bg().draw_line(x0, y0, x1, y1, color);    // CPU (Bresenham)
scene.bg().add_world_bitmap(plan, src, wx, wy, ...);        // Blitter, mundo
scene.bg().add_world_bitmap_masked(plan, src, mask, wx, wy, ...); // BOB con transparencia
```

Todas devuelven `bool` y **validan límites** (fuera de rango → `false`).

**Reglas del layout** (las aplica cada playfield vía hooks de mapeo):

- **`linear_display` (espejo)**: cada escritura en el bucle (filas
  0..display_height-1) se **duplica al espejo** (filas +display_height), o el
  framebuffer queda incoherente. Coste: 2× operaciones para TODO el dibujo.
- **split (bucle)**: una sola operación (1×); el destino se envuelve con
  `planeline_for(wy)` (`(wy % display_height) * planes`) y, si un blit cruza la
  costura, se parte en dos (`add_world_bitmap` lo gestiona).
- **Walk horizontal**: el byte físico de un píxel de mundo es `(planelínea)*row
  + wx/8`, que cruza a la siguiente planelínea cuando `wx/8 >= row_bytes`. Las
  primitivas CPU lo soportan acotando contra el tamaño total; los blits requieren
  `wx` **múltiplo de 16** (word-aligned) y origen en **Chip RAM**.

**`CanvasPlayfield`**: lienzo plano (interleaved) sin scroll, con las mismas
primitivas en coordenadas de lienzo. Es la base de un HUD, un fondo estático o
una capa de actores. El escenario de un HUD en una franja inferior con su propia
configuración (bitplanes/paleta) requiere un **split de Copper** entre zonas
(split-screen); está documentado como trabajo futuro (ver AMIGA_8WAY_SCROLLING).

**Objetos fijos en pantalla (HUD)**: se dibujan cada frame en la posición de
mundo que la cámara muestra, con `XLimitedPlayfield::screen_to_world_x(sx)` (=
`mapposx()+sx`) y `screen_to_world_y(sy)` (equivale a
`(mapposy + tile_height + sy) % display_height`). **IMPORTANTE**: NO es
`mapposy()+sy` — la ventana visible del corkscrew empieza un bloque por debajo
de `videoposy` (`display_offset = (mapposy+tile_height) % display_height`), así
que usar `mapposy()+sy` deja el objeto en la banda de staging, fuera de
pantalla.

**Limitación de bus del Amiga (medida)**: las escrituras CPU al chip RAM durante
el frame visible roban ciclos al DMA de bitplanes. Con pocas (1-2 `set_pixel`)
no hay efecto; con decenas–centenas de RMW (`fill_rect`/`draw_line` grandes) el
emulador muestra scanlines negros periódicos (inanición de bus). Por eso el
dibujo masivo va por **Blitter** (`add_world_bitmap[_masked]`) y las primitivas
CPU se reservan para marcas pequeñas o para init (boot, sin competencia). La
demo usa un HUD mínimo por CPU + un BOB enmascarado por Blitter.

**Toda rutina de dibujo futura debe pasar por estas primitivas**; nunca escribir
a `frontbuffer()` a ciegas, porque la planelínea/byte del píxel depende de
`display_offset` y del modo (split vs espejo). Verificación host:
`node tools/analyze/verify-draw-primitives.mjs` (corkscrew + CanvasPlayfield, y
con `K_LINEAR=1`).

## Monitorización de carga (frame a frame)

Para supervisar la homogeneidad de CPU/Blitter/bus y detectar picos, la demo
escribe cada frame un bloque de telemetría en `g_eng_frame_telemetry`
(`engine/include/eng/debug/run_status.hpp`, struct `FrameTelemetry`):
`frame`, `blit_jobs`, `blit_words`, `copper_words`, `fillup_extra`. Se lee por el
canal lateral de WinUAE-DBG:

```bash
# con la demo viva (run-demo --keep-running), leer 20 muestras:
node tools/analyze/read-frame-telemetry.mjs demos/107_xlimited_corkscrew 20
```

El `blit_jobs` debe ser ~constante (2× el scroll en `K_LINEAR=1` por el espejo);
los picos de `fillup_extra` aparecen solo en los cruces de tile (cada 16 px) y
son pequeños (2-4 blits) frente al presupuesto de Blitter del frame.

## Arquitectura (reutilizable como librería)

- `engine/include/eng/field/playfield.hpp` — **abstracción de capa**: `Playfield`
  (base abstracta: framebuffer + geometría + primitivas de dibujo CPU validadas
  vía hooks de mapeo + blits virtuales + `update_scroll` + `hardware_view`) y
  `CanvasPlayfield` (lienzo plano interleaved sin scroll).
- `engine/include/eng/field/xlimited.hpp` — `XLimitedPlayfield : Playfield`
  (corkscrew/XYLimited, especialización del scroll: `scroll_right/left/up/down`
  fieles a `Scroller_XYLimited/main.c`, banda de staging `block_videoposy`, split
  vertical, walk horizontal, espejo del modo lineal) + `XlimitedDisplayComposer`
  + `XlimitedDualComposer`. El dual (DPF 3+3) intercala PF1 (planos 1,3,5) y PF2
  (2,4,6) con su split compartido.
- `engine/include/eng/field/xlimited_scene.hpp` — **abstracción de escena**:
  `XlimitedScene` + `XlimitedSceneConfig` + `xlimited_build_blocks_bitmap`.
  Compone uno/dos `XLimitedPlayfield` con **roles** (`bg()`/`fg()`), el compositor
  single/dual, el relleno, el pre-scroll, el camino de direcciones
  (`effect`/`phase`) y la composición, con API declarativa (config) y por-frame
  (`update_auto`/`update`/`compose`/`install`).
- `demos/107_xlimited_corkscrew/src/main.cpp` — **consumidor fino**: define el
  mapa y dos generadores de filas (`fg_row`/`bg_row`), configura `XlimitedSceneConfig`
  desde las macros `K_*` y delega en `XlimitedScene`. Usa `scene.bg()` para el
  HUD mínimo por CPU y el BOB enmascarado por Blitter. Sin lógica circular ni
  glue de hardware.

Referencias: `ScrollingTricks/Docs/xlimited-uk.html`,
`amiga-stuff/scrolling_tricks/xlimited.c`,
`amiga-stuff/scrolling_tricks/xylimited.c` (corkscrew),
`docs/architecture/AMIGA_8WAY_SCROLLING.md` (§3 errata, §7.4, §11, §13).
