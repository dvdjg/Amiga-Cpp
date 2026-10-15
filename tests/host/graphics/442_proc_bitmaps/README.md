# HOST-442 — proc_bitmaps

Test host de la librería de **generación procedural de bitmaps** (`engine/include/eng/graphics/proc/`):
buffer de índices, dither Bayer, perfiles 1D, evaluadores de píxel, tiles Wang con
transformaciones, texturas orgánicas tileables, atlas de materiales con variantes, capas
de material por altura y el generador de terreno modular (piezas + pintor + catálogos).

## Qué cubre

- **Buffer y dither**: `BitmapBuffer::at` (indexación `y*W+x`), matrices de Bayer 2/4/8
  como permutaciones, `threshold8 ≤ 251`, `quantize_plain`/`quantize_dither` con extremos
  estables (`0 → 0`, `255 → levels-1`), rango `0..levels-1` y dither ±1 en el nivel medio.
- **Perfiles**: `Profile1D::hill`/`valley` (pico/valle en el centro, simetría, monotonía).
- **Evaluadores**: `CircleFill` (distancia²), `fill`/`fill_masked` (transparente = 0),
  `Hill2D` (pico = `levels-1`), `Layer` (máscara elige A/B).
- **FbmFill y texturas con DOS escalares** (negativo de §1.11): `MiniFloat16` (el válido
  en Amiga para ruido) y `float`. Determinismo, rango `[0,1)`, **tileabilidad** (px=0 y
  px=`tile_px` dan el mismo valor: `Clouds`, `Marble`, `Wood`, `Stone`, `Granite`,
  `Foam`, `Rust`, `Moss`, `WeatheredStone` con `scale = k/32`, `period = k`).
- **Tiles Wang**: `edges_match` (casa/no casa por lado), `apply_transform` (Rot90×4 y
  FlipX×2 = identidad), `transform_edges` (giro coherente con el píxel), `BorderRamp`
  (0..255 monótona) y `blend_index` (alpha 0/255 estables).
- **Shapes**: lago/cauce (centro dentro, esquina fuera), máscara suave (interior opaco,
  exterior transparente), muralla/torre/casa/cueva (dentro/fuera).
- **Atlas**: cota de la tabla (`base + levels-1 ≤ 15` para los 22 materiales), todos los
  píxeles remapeados dentro de la paleta de 16 colores, variantes deterministas,
  materiales distintos ⇒ texturas distintas, y **determinismo por seed** (dos atlas
  idénticos bit a bit).
- **MaterialLayer**: altura 0 ⇒ banda de hierba (4..5), altura 1 ⇒ nieve (15); campo
  fBm interno en rango de paleta.
- **Terreno**: set completo (42 piezas = 11 castillo + 6 río + 13 naturaleza + 12 extra),
  `can_place` para muralla vertical consigo misma y puerta↔muralla horizontal, **el agua
  entra por el centro exacto del borde** (comparación con el píxel del material del
  atlas en dos tramos consecutivos) y la puerta abre el hueco transparente (índice 0).
- **Variantes y colocador**: `add_variant` (píxeles con `apply_transform` + bordes con
  `transform_edges`; `Identity` y falta de capacidad devuelven la base), `place_world`
  (mapa 10×7 conectado con cueva, puerta y torre ancladas, río de borde a borde, puente en
  la fila del camino; determinista; todas las piezas en paleta) y el encaje con
  `eng::playfield::TileLayerMap` (`cells`/`width`/`height`, `tile_at` y `edge_tile`).

## Cómo está implementado

Cabeceras genéricas sobre el escalar `S` (§1.11); el test las instancia con `MiniFloat16`
y `float` donde hay ruido. Las texturas reutilizan `eng/core/math/noise.hpp`
(`fbm2`/`turbulence2`/`ridged2`/`worley2_sq`, **no** se reimplementa ruido). Todo el coste
es de generación (carga): los bucles por píxel están documentados con su coste y nunca
corren en el bucle de frame.

Evidencia visual opcional (PPM con la paleta del atlas):

```bash
tests/host/graphics/442_proc_bitmaps/.../binario --dump out/playground/proc-bitmaps
```

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/442_proc_bitmaps
```

## Estado

Respaldo host de la librería. En hardware la ejercita la demo
`demos/techniques/amiga/effects/119_proc_world` (colocación + composición + display,
`VERIFIED` en su alcance; ver su `VALIDATION.md`); la generación runtime del atlas/set en
68000 está **SIN VERIFICAR** (medida y precocinada en host, `PROCEDURAL_BITMAPS.md`).
