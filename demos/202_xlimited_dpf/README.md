# Demo 202 — DPF 3+3 (dual playfield) con mapa real a 8 colores + plaquettes transparentes

## Qué hace

Una escena de **dual playfield (DPF 3+3)** sobre el corkscrew X-Limited del engine, con dos capas que scrollean a velocidades distintas (parallax X 2:1) con intervención mínima de CPU:

- **BG (PF2, planos de hardware 2,4,6)** — el MISMO mundo real de la demo 201 («Beginning Fields», 40×40 tiles) pero **cuantizado a 8 colores** (3 planos por playfield, 8 registros de la paleta DPF). Es la capa de fondo, opaca.
- **FG (PF1, planos de hardware 1,3,5)** — un set de **plaquettes sintéticas** (placas decorativas) con **7 colores + índice 0 transparente**: donde el FG no pinta, se ve el BG a través. Capa delantera, mapa propio de 48×40 (toroidal).
- Cada campo tiene su propio `XLimitedPlayfield` (banco, mapa, paleta-base y paso de scroll): el BG avanza `2 px/frame` y el FG `1 px/frame` (`set_scroll_step` + `update_scroll` por campo). El scroll fino lo hace el hardware (BPLCON1 + BPLxPT vía Copper); el Blitter solo pinta la columna entrante en cada cruce de 16 px → CPU/Blitter mínimos a 50 fps.

Verificación automática del parallax: `analyze-sequence.sh` captura una secuencia y `verify-parallax.mjs` mide que el BG se mueve ~2× el FG (máscaras de color estrictas de las dos paletas DPF).

## Cómo se mapea DPF en OCS (por qué PF1 y PF2 así)

En dual playfield el compositor reparte los 6 planos: PF1 = planos 1,3,5 y PF2 = 2,4,6. La paleta es de 16 registros: **PF1 → registros 0..7, PF2 → registros 8..15**. Con `BPLCON2` sin `PF2PRI`, PF1 es la capa delantera; su **color 0 es el «transparente»** y deja ver PF2. Por eso:

```
reg 0..7   = FG (plaquettes)  -> índice 0 transparente, 1..7 colores de placa
reg 8..15  = BG (mapa 8c)     -> 8 colores del mundo real
```

## Montaje de la escena (patrón reutilizable del engine)

Se usa `XlimitedScene` en modo DPF homogéneo con **dos bancos reales distintos por campo** (`blocks_prebuilt` para PF1 y `blocks_prebuilt2` para PF2). Este soporte se añadió al engine en `XlimitedSceneConfig`/`XlimitedScene::begin` (antes los bancos pre-construidos solo podían ser de 6 planos EHB y se reutilizaban para ambos campos). La demo configura:

- `scene_cfg.map` = mapa del FG (plaquettes) · `scene_cfg.map2` = mapa del BG (mundo real).
- `blocks_prebuilt`/`blocks_prebuilt2` = bancos incbin `.MEMF_CHIP` (alias sin copia).
- `scene.fg()` (PF2 = BG visual) paso 2 · `scene.bg()` (PF1 = FG visual) paso 1.
- `scene.fill()` pinta los dos anillos en lotes; por frame `update_scroll(plan, dx, 0)` por campo + `execute_frame_plan` + `compose()`/`install()`.

## Pipeline de assets (reproducible)

```
# BG: "Beginning Fields" a 8 colores (3 bits/px) + banco X-Limited 3 planos
node tools/amiga-tiles/amiga-tiles.mjs "tools/amiga-tiles/assets/Beginning Fields.png" \
     --colors 8 --xlimited --palette adaptive --dither floyd --tile 16 --out out/demo202/bg

# Constantes + FG (plaquettes) + mapa del FG: lee el JSON del BG, sintetiza las
# plaquettes (7+alpha), emite out/demo202/const_202.h y out/demo202/fg/tilebank_xlimited.bin
node tools/demo202/emit-202.mjs
```

`const_202.h` contiene: paletas 12-bit (`kFgPalette`, `kBgPalette`), mapas (`kFgMap`/`kBgMap` con sus dimensiones) y `kFgTiles`/`kBgTiles`. Los dos bancos se incrustan por `incbin` en sección `.MEMF_CHIP` (`main.cpp`). Layout del banco: X-Limited 320 px, `planelínea = fila*planes+plano`, tile `t` en `(t%20, t/20)`, words big-endian (igual que el banco EHB de la 201, pero a `planes` planos).

## Build / run / verify

```
bash ./tools/build/build-demo.sh demos/202_xlimited_dpf --debug --clean
bash ./tools/run/run-demo.sh demos/202_xlimited_dpf            # screenshot
bash ./demos/202_xlimited_dpf/analyze-sequence.sh [--warp]      # parallax 2:1
bash ./demos/202_xlimited_dpf/analyze-sequence.sh --release
```

Notas:
- Geometría: **viewport TOTAL `320×256`** y anillo corkscrew `display_height = 256 + 2·16 = 288`
  (18 bloques), 3 planos por playfield. Igual que en la 201 (§7), el anillo NO se
  dimensiona para un visible menor (p. ej. `208+32=240`): con 240 el `mapy` 16/17 del
  walk plane-shift colisiona con 1/2 y aparecen arriba filas que deben ir abajo.
  `visible_tile_bias_x/y = 1` para que `map[0][0]` quede arriba-izquierda.
- Esta demo solo scrollea en X (`dy = 0`), así que no se activa el split vertical y el
  viewport 256 no choca con el comparador de 8 bits del Copper.
- `g_eng_frame_telemetry` (jobs/words/copper) se publica por frame; la lectura por el runner/harness está degradada en esta rama (devuelve `0xffff`) para todas las demos, no es específica de la 202.
