# Demo 202 — DPF 3+3 (dual playfield) con mapa real a 8 colores + plaquettes transparentes

## Qué hace

Una escena de **dual playfield (DPF 3+3)** sobre el corkscrew X-Limited del engine, con dos capas con su propio scroll y un **recorrido que visualiza todo el mapa**:

- **BG (PF2, planos de hardware 2,4,6)** — el MISMO mundo real de la demo 201 («Beginning Fields», 40×40 tiles) pero **cuantizado a 8 colores** (3 planos por playfield, 8 registros de la paleta DPF).
- **FG (PF1, planos de hardware 1,3,5)** — plaquettes sintéticas (7 colores + índice 0 transparente, deja ver el BG). **Desacoplado del BG**: su X oscila con un oscilador propio e independiente (centro/radio propios), y su Y es compartida con el BG (el compositor DPF usa un único split de Copper) → en conjunto recorre en 8-way (ambos sentidos de X + el movimiento vertical/diagonal compartido).
- **Recorrido por fases** (`TourDriver` en `main.cpp`): fases **lineales** con **offset 1 px/frame** — H derecha (320 px), V abajo (432 px) — luego diagonal a (0,0) y hacia el centro; después **Lissajous con la amplitud COMPLETA del mundo** (x∈[0,320], y∈[0,432]) que desplaza todo el mapa de un lado a otro. Los índices del seno avanzan **lentos** (sub-muestreo) para que el **salto por frame sea ≤ 2 px** (si variaran 1 índice/frame el salto sería ~4-6 px y la cámara se quedaría rezagada sin llegar a los extremos).
- **Mapas SIEMPRE toroidales (wrap)**: el scroll es un único algoritmo de bucle
  (sin modos de borde ni recortes); el recorrido se limita a un primer paso del
  mundo (320/432 px), así la costura del toro nunca se ve.
- **Sin linear_display**: viewport **recortado a 320×208** (13 filas) → el split vertical del corkscrew cae en raster ≤ 248 (comparador de 8 bits) → split canónico.
- Todo el scroll fino es hardware (BPLCON1 + BPLxPT vía Copper); el Blitter solo pinta la columna/fila entrante en cada cruce de 16 px → CPU mínima a 50 fps.

Verificación automática: `analyze-sequence.sh` captura una secuencia y `verify-parallax.mjs` comprueba que ambas capas están en movimiento continuo.

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
- Geometría: viewport **recortado** `320×208`; anillo corkscrew `display_height = 208 + 2·16 = 240`.
  La 201 (§7) exige que el anillo se dimensione para el viewport que el algoritmo
  recorre (ahí, 256 + HUD); en 202 el viewport ES 208 y el anillo 240, ambos
  consistentes, y el split queda en raster ≤ 248 (canónico, sin linear_display).
  `visible_tile_bias_x/y = 1` para que `map[0][0]` quede arriba-izquierda.
- `g_eng_frame_telemetry` se publica por frame y se lee con normalidad: el símbolo
  se declara con inicializador no-cero para que viva en `.data` (no `.bss`) y el
  runner resuelva su dirección igual que `g_eng_run_status`.
