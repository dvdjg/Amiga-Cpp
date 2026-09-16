# Limpieza y unificación de las demos de scroll (Fase 0)

Plan para alinear las demos de scroll (implementadas en distintas fases del engine) en **una demo
de referencia por algoritmo**, verificar las técnicas que faltan y retirar el material duplicado sin
perder lo útil. El modelo objetivo de superficies/estrategias es
`docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`; este documento es el **inventario y la
hoja de ruta de la limpieza**.

## 1. Dos familias hoy

| Familia | Cómo se mueve | Demos | Estado |
|---|---|---|---|
| **XYLimited / corkscrew** (canónica) | anillo + staging + split; `XLimitedPlayfield` + `ScrollEngine`, `AxisPolicy` (Ring/Finite/Off), `DirectionPolicy`, `ScrollProfile`, `linear_display` | 107, 110, 111, 112, 201, 202 | alineada con el modelo objetivo |
| **`TileScrollScene`** (driver de laboratorio) | superficie lineal grande o anillo reciclado con `CopyRect`+scratch | 101, 103, 104 | **modelo paralelo** al objetivo |
| **`TileFieldController`** (legacy) | framebuffer de páginas (page-backed) | 105 | único consumidor vivo; a decidir |
| **CPU didáctica** | redibuja el viewport por CPU sobre EHB estático | 100 | no es scroll de hardware |

## 2. Inventario por demo

| Demo | Algoritmo | API del engine | Geometría | Validación |
|---|---|---|---|---|
| `100_virtual_tile_scene_scroll` | «virtual playfield» CPU didáctico (EHB + zonas de paleta) | `StaticEhbScene` + `scene::VirtualScene`/`Camera2D`/`TileLayer` | 320×256 | solo screenshot |
| `101_ehb_tile_scroll_driver` | lineal EHB + prefetch bidireccional | `TileScrollScene<ehb>` + `BidirectionalRingPrefetch`, `RouteCamera` | superficie 640×512 | fuerte: `analyze-sequence`/`analyze-fine-scroll`/`pixel-contract` |
| `103_tile_scroll_ring` | anillo EHB con `CopyRect`+scratch | `TileScrollScene<ehb(),2,2>` | 352×288 | **sin README ni secuencia** |
| `104_tile_scroll_ring_dualpf` | anillo DPF 3+3 con scratch | `TileScrollScene<dual(3,3),2,2>` | 352×288 | `README_104` + `dpf-algorithm.md` |
| `105_tile_scroll_xyunlimited_dualpf` | page-backed de 4 páginas (**no es XYUnlimited**) | `TileScrollScene<dual,24,20>` + `TileFieldController` | 704×576 (páginas) | `README_105` + `analyze_105_tile_visibility.mjs` |
| `107_xlimited_corkscrew` | XYLimited 8-way (single/DPF) | `XlimitedScene`/`XLimitedPlayfield`/`XlimitedDualComposer` | 352×(288) | fuerte: `verify-xlimited`/`verify-corkscrew`/secuencia |
| `110_ylimited_shooter` | YLimited: X `Finite` + Y `Ring` one-way | `XlimitedScene` | 320×208; anillo 288 | `analyze-sequence` |
| `111_xlimited_sidescroller` | XLimited: X `Ring`, Y `Off` | `XlimitedScene` + `TileMapView<StreamingWorldMap>` | 320×256 | `analyze-sequence` |
| `112_xlimited_robocod` | XYLimited 5 planos + soft DPF (parallax por plano) | `XlimitedScene` + `PlaneView`/`SoftDpfComposition` | 320×208; anillo 288 | `analyze-sequence` |
| `201_ehb_map` | XYLimited 8-way EHB + HUD | `XlimitedScene` + `ModeSwitchZone` | 320×256; anillo 288 | `src/README` + `verify-201-*` |
| `202_xlimited_dpf` | XYLimited DPF 3+3 mixto (Y por campo) | `XlimitedScene` con `dual_linear_field` | 320×208; anillo 288 | `analyze-sequence` + `verify-parallax` |

## 3. Glosario de nombres (política)

Taxonomía de Steger, que este repo adopta:

- **`XLimited`** = horizontal puro: banda entrante *plane-shifted*, **sin split**, sin anillo Y.
- **`XYLimited`** = 8-way / corkscrew: X + anillo Y + **split** de Copper (anillo `viewport_h + 2·tile`).

Regla: al documentar o comentar, usar **XYLimited** cuando `y_mode = Ring` (107, 110, 201, 202) y
**XLimited** cuando el eje Y está `Off`/`Finite` (111). El nombre de la **familia de clases**
(`XLimitedPlayfield`, `XlimitedScene`, `XlimitedConfig`, `XlimitedDisplayComposer`,
`XlimitedDualComposer`, `XlimitedOverlayConfig`, `XlimitedDualConfig`, `XlimitedPathConfig`) se
mantiene por ahora; su renombrado inequívoco (`Playfield<N>` + `RingScroll`) entra en la **Fase 5**
de `REFACTOR_PLAYFIELD_SCROLL.md` para no pagar el churn dos veces.

Mapa de renombrado (para la Fase 5, no aplicar antes):

| Actual | Objetivo |
|---|---|
| `XLimitedPlayfield` | `Playfield<N>` (Layout = Ring) + `RingScroll` |
| `XlimitedScene` | `DisplayComposition` (escena de capas con rol) |
| `XlimitedDisplayComposer` / `XlimitedDualComposer` | `SingleComposition` / `DpfComposition` |
| `XlimitedConfig`/`…Config` (sub-configs) | configs de la superficie/estrategia, sin prefijo de familia |
| demo `107_xlimited_corkscrew` | `107_xylimited_corkscrew` (8-way) |
| demo `105_tile_scroll_xyunlimited_dualpf` | nombre acorde al algoritmo real (page-backed) |

## 4. Demos de algoritmo que faltan

Una demo **mínima por algoritmo**, mismo contenido y mismo gate; las demos de escena (107/110/111/
112/201/202) quedan como muestrario.

| Algoritmo | Superficie | Estrategia | Display | Demo | Test host |
|---|---|---|---|---|---|
| Ring / corkscrew | anillo+staging | `ScrollEngine` | split Copper | 107 (referencia) | HOST-023/032-036 |
| BigBuffer / virtual playfield | bitmap grande contiguo | `BigBufferScroll` | mover `BPLxPT` | **`120_virtual_playfield`** (hecha) | HOST-044 (ya existe la estrategia) |
| Espejo vertical | bitmap con bucle duplicado | `MirrorScrollPlayfield` | lectura contigua, sin split | **`121_mirror_scroll`** (hecha, 256 px) | demo + HOST-056 (reusa el mapper flat) |
| Doble buffer + swap | 2 bitmaps completos | offset de cámara | swap `COP1LC`/`BPLxPT` | **nueva** | nuevo host de `swap()` |
| Page-backed | páginas | `TileFieldController` | `CopyRect`+scratch | **nueva** (de 105) | host de páginas |

### Estado del mapper (Fase 3, primera rebanada)

`eng::field::map_flat_scroll` (`engine/include/eng/field/amiga_display_mapper.hpp`) traduce la cámara del virtual playfield a `planeaddx`/`planeaddy`/`BPLCON1`/`BPLMOD`; es una función pura con test host (**HOST-056**). La superficie `eng::field::FlatScrollPlayfield` (`flat_playfield.hpp`) compone bitmap flat + `BigBufferScroll` + esa vista, de modo que la demo no conoce registros. Verificado por la demo 120. Falta generalizarlo al resto de estrategias (una `ScrollView` neutral común y el mapper para anillo/split/doble buffer).

## 5. Lista de salvamento (antes de retirar demos)

| Demo a retirar | Qué conservar | Dónde |
|---|---|---|
| `100` | idea de **zonas de paleta por región** (cielo/jungla/subsuelo) para riqueza visual | nota en `docs/demos/`/demos nuevas |
| `101` | **infra de validación**: `analyze-fine-scroll.sh`, `pixel-contract.json`, `verify-scroll-directions.mjs`; `RouteCamera`; prefetch bidireccional | `tools/analyze/` + engine |
| `103` | técnica de **anillo con `CopyRect`+scratch** (sin saveword) | ficha en `docs/reference/amiga/techniques/` |
| `104` | **fusión de jobs DPF** (`destination_plane_stride`) y `dpf-algorithm.md` | doc de rendimiento + ficha |
| `105` | estrategia **page-backed** + `analyze_105_tile_visibility.mjs` | engine (si se promueve) + `tools/analyze/` |

Regla de cierre (AGENTS): al retirar una demo, degradar a **NO VERIFICADA** la API que cubría
(`TileScrollScene`, `tilemap/tile_scroll.hpp`, `TileFieldController`, `scene/virtual_scene.hpp`) si
queda sin consumidor.

## 6. Tileset compartido (32 colores) — Beginning Fields re-cuantizado

Fuente: `assets/amiga/tiles-reference/Beginning Fields.png` (640×640, export de Tiled de un mapa
renderizado: la mayoría de celdas 16×16 son distintas). Salida:
`out/assets/beginning-fields/<depth>/`.

```
node tools/amiga-tiles/amiga-tiles.mjs "assets/amiga/tiles-reference/Beginning Fields.png" \
  --colors 32 --palette kmeans --dither none --tile 16 --xlimited \
  --out out/assets/beginning-fields/32c
```

| Depth | Paleta/dither | Tiles únicos | Banco indexado | bits/px | Uso |
|---|---|---|---|---|---|
| 8c | mediancut / none | 1180 / 1600 | 110,6 KB | 3 | 202 DPF (3 planos) |
| 16c | kmeans / none | 1147 / 1600 | 143,4 KB | 4 | single 4 planos |
| 32c | kmeans / none | 1202 / 1600 | 300,5 KB | 5 | single 5 planos (matriz) |
| 32c X-Limited | — | 1202 | 190,6 KB (4880 planelíneas) | 5 | scroll de anillo |

- El **dedupe exacto está activo siempre** (`--merge` por defecto vale `1` = exacto); solo elimina
  celdas idénticas. `--merge <1` fusiona *similares* y rompe el assert de reconstrucción al 100 %.
- Las diferencias de recuento entre profundidades (~5 %) vienen de la **paleta** (mediancut vs
  kmeans; cada K tiene su propia paleta) y de qué celdas colisionan al cuantizar; no son un fallo.
- EHB no se trabaja: el origen no tiene colores suficientes para aprovecharlo.

## 7. Criterios de aceptación

- Cada algoritmo con **una demo** que pasa `build -> run -> analyze` + gate visual (secuencia) + su
  test host; fps medidos con `K_DIAG_*`.
- Ninguna demo de escena (107/110/111/112/201/202) regresiona.
- Tabla de **presupuesto de Chip RAM** por algoritmo/geometría (el banco de 32c es ~300 KB).
- Documentación: una sola referencia por concepto; el resto enlaza.

## 8. Referencias

- Modelo objetivo: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`.
- Plan del refactor: `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.
- Corkscrew: `docs/engine/architecture/AMIGA_8WAY_SCROLLING.md`, `XYLIMITED_ALGORITMO_GENERICO.md`.
- Split y límite de 8 bits: `docs/guides/roadmap/CONSULTA-SPLIT-208.md`.
- DPF y Y por campo: `docs/engine/architecture/DPF_MIXTO_SPLIT_LINEAL.md`.
- Pipeline de tiles: `tools/amiga-tiles/README.md`, `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md`.
