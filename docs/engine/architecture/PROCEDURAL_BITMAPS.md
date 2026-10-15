# Bitmaps procedurales (`eng::gfx::proc`)

Librería del anillo 0 para **generar contenido gráfico por CPU** — texturas de material, piezas de terreno y mapas — sin depender del chipset ni del backend. Es un pipeline de **carga**: genera los tiles una vez (init/loading) y el camino de frame solo consume el resultado (índices de paleta ya cuantizados). El volcado a bitplanes Amiga lo hace `eng::graphics::c2p` (`engine/include/eng/graphics/c2p.hpp`), que ya existía y **no** se duplica aquí.

Código: `engine/include/eng/graphics/proc/`. Test host: `HOST-442` (`tests/host/graphics/442_proc_bitmaps`).

## 1. Modelo por capas

```text
  ┌────────────────────────────────────────────────────────────────────────┐
  │ terrain/  terreno modular (Edge/Feature/TileSpec + catálogos)          │
  │   tiles:  muralla, torre, puerta, patio, río, lago, puente, bosque,    │
  │           montaña, aldea, cueva…   →  TerrainTileSet                │
  ├────────────────────────────────────────────────────────────────────────┤
  │ atlas:    TextureAtlas (22 materiales × 3 variantes + paleta 16)       │
  │           MaterialLayer (bandas por altura: hierba→piedra→nieve)       │
  ├────────────────────────────────────────────────────────────────────────┤
  │ tex:      texturas orgánicas tileables (Clouds, Marble, Wood, Stone,   │
  │           Granite, Foam, Rust, Moss, WeatheredStone) + to_index        │
  │ shapes:   primitivas (RiverStroke, LakeBlob, TreeCanopy, RockBlob,     │
  │           CropRows, WallSegment, TowerTop, HouseFootprint, CaveMouth,  │
  │           SoftCircleMask)                                              │
  ├────────────────────────────────────────────────────────────────────────┤
  │ evaluadores:  BitmapBuffer · Bayer/quantize · Profile1D · FbmFill ·    │
  │               CircleFill · Hill2D · Layer · fill/fill_masked           │
  │ tiles(Wang):  EdgeSides/edges_match · Transform/apply_transform ·      │
  │               BorderRamp/blend_index                                   │
  └────────────────────────────────────────────────────────────────────────┘
```

## 2. Cabeceras

| Cabecera | Contenido |
|---|---|
| `proc/bitmap_buffer.hpp` | `BitmapBuffer<W,H>`: buffer de índices chunky (1 B/píxel, geometría fija en compilación). |
| `proc/dither.hpp` | `Bayer` (2/4/8) y cuantización de intensidad 0..255 a `levels` índices (`quantize_plain`/`quantize_dither`). |
| `proc/profile.hpp` | `Profile1D<N>`: perfiles 1D `constexpr` (colina/valle) para siluetas separables. |
| `proc/evaluators.hpp` | `FbmFill<S>`, `CircleFill`, `Hill2D`, `Layer` y `fill`/`fill_masked`; `tile_scale<S>`. |
| `proc/shapes.hpp` | Primitivas orgánicas (río, lago, árbol, roca, cultivos, muralla, torre, casa, cueva, máscara suave). |
| `proc/tiles.hpp` | Wang/Carcassonne: `EdgeSides<E>`, `edges_match`, `Transform`/`apply_transform`/`transform_edges`, `BorderRamp`/`blend_index`. |
| `proc/textures.hpp` | Texturas tileables y `domain_warp`; `to_index`/`fill_texture`. Reutiliza el ruido de `eng/core/math/noise.hpp`. |
| `proc/atlas.hpp` | `MaterialId`/`MaterialDesc`, `TextureAtlas<TW,TH,S>` (materiales × variantes + paleta) y `MaterialLayer`. |
| `proc/terrain_types.hpp` | `Edge`, `Feature`, `TileSpec` (bordes tipados + rasgos del interior). |
| `proc/terrain_paint.hpp` | El pintor de piezas (orden de capas, continuidad de bordes) y `paint_terrain_tile`. |
| `proc/terrain.hpp` | Paraguas: tipos + pintor + catálogos (`kCastlePieces`/`kRiverPieces`/`kNaturePieces`) + `TerrainTileSet`. |

## 3. Escalar (contrato §1.11)

Las cabeceras son **genéricas sobre el escalar** `S` y no incluyen ningún escalar concreto. El ruido (`fbm2`, `turbulence2`, `ridged2`, `worley2_sq` de `engine/include/eng/core/math/noise.hpp`) exige un escalar con rango de rejilla y división:

- **Amiga**: `MiniFloat16` (10 bits, `[2^-14, 65504]`) — el mismo escalar de la demo `083_fbm_noise`. Su división es tabla de recíprocos (`minifloat.hpp`), apta para los cocientes por píxel de las primitivas.
- **Host/tests**: `float`/`double`. Un `Fixed` 4.12 **no** compila las texturas (el diagnóstico `require_grid_range` lo explica al instanciar).

El test host instancia la librería con **MiniFloat16 y float** (test negativo de §1.11).

## 4. Periodicidad (tileable sin costuras)

`period` es el periodo del ruido en unidades de la escala `scale` (0 = no tileable). La regla para un tile de `tile_px` píxeles con `freq` ciclos:

```text
   scale = tile_scale<S>(tile_px, freq) = freq / tile_px
   period = freq

   noise(px + tile_px) == noise(px)   →  dos tiles adyacentes casan sin costura
```

Las llamadas internas a frecuencias derivadas propagan el periodo con multiplicación/división entera de la misma relación (`detail_mul`/`crystal_mul`/`ridge_mul` ×N, `patch_div` ÷N; el divisor exige `period` múltiplo). El atlas genera cada material con `scale = freq/TW` y `period = freq` (y con `freq` par en los materiales con `patch_div = 2`), de modo que **todos** los tiles del set son tileables. La tileabilidad se verifica en `HOST-442` comparando el valor en px=0 y px=tile_px (idénticos bit a bit).

## 5. Atlas de materiales

`TextureAtlas<TW,TH,S>` genera `22 materiales × 3 variantes`. La variante se elige de forma **estable** por coordenadas de tile (`variant_at(tile_x, tile_y, id)`, hash entero), así que el mapa no se ve repetitivo sin guardar estado por celda. La paleta compartida tiene 16 colores RGB444 agrupados **por tono** (el índice 0 = transparente, el 15 = nieve/blanco):

```text
  0-1 cueva (negro)      2-3 agua/espuma          4-5 hierba/hoja/musgo
  6-7 tierra/arena/camino                      8-9 cultivo A
  10-11 piedra/granito/mármol/adoquín          12-13 madera/tejado/óxido
  14 verde profundo      15 nieve (blanco)
```

| Material | Técnica (textura) | Tono de paleta |
|---|---|---|
| Grass | fBm (`Clouds`) 1 ciclo | 4-5 |
| Dirt / Path | piedra suave (`Stone` 2-3 octavas) | 6-7 |
| Stone / Wall | `Stone` (warp + ridged) | 10-11 |
| Granite / Rock | `Granite` (fBm + Worley) | 10-11 |
| Water | fBm 4 octavas | 2-3 |
| Foam / Cobble | `Foam` (Worley + umbral) | 2-3 / 10-11 |
| Sand | fBm 2 octavas | 6-7 |
| Wood / Trunk / Roof | `Wood` (veta direccional) | 12-13 |
| Rust | `Rust` (turbulencia + crestas) | 12-13 |
| Marble | `Marble` (venas plegadas) | 10-11 |
| Foliage / Moss | `Moss` (fBm × parches Worley) | 4-5 |
| CropA / CropB | fBm 2 octavas | 8-9 / 6-7 |
| Cave | `Stone` 2 octavas | 0-1 |
| Snow | fBm fino | 15 (blanco plano) |

`MaterialLayer<TW,TH,S>` compone hasta 6 bandas con umbrales `lo/hi` y fundido `soft`; el fundido es **dither espacial** (sin interpolar color): el campo de altura (fBm interno o un `render_with_height` externo) decide el peso de cada banda. Caso típico (montaña): `Grass 0..35%`, `Stone 30..70%`, `Snow 65..100%`.

## 6. Terreno modular

Cada pieza (`TileSpec`) declara un borde tipado por lado (`Edge`: Grass/Water/Path/Wall/Forest/Ridge/Crop/Cave) y una máscara de rasgos (`Feature`). La continuidad es **estructural**, no afortunada:

- el agua entra/sale por el **centro exacto** de sus bordes (dos tramos apilados casan; verificado en `HOST-442`);
- la muralla se centra en el borde y su orientación se deriva de los `Edge::Wall`;
- los ids casan (`edges_match`) → `TerrainTileSet::can_place` coloca por casación;
- el borde suave (`apply_edge_transition`) usa la **misma rampa** en ambos lados del mismo id + dither Bayer espacial (sin costura).

Orden del pintor (algoritmo del pintor): base por rasgo dominante → cultivos → lago → río (+orilla `Foam`, +puente) → árboles → rocas → montaña (perfil con cima nevada) → casa → cueva → muralla → torre → puerta → musgo del patio → franjas de borde. Todo el azar sale de un `Xoroshiro64pp` sembrado con `spec.seed` (misma pieza = mismo resultado en host y Amiga).

Catálogos: `kCastlePieces` (11), `kRiverPieces` (6), `kNaturePieces` (13: incluye hierba llana y camino); `TerrainTileSet::generate` añade 12 variaciones extra de naturaleza (**42 piezas** con `MaxTiles ≥ 42`). `TerrainTileSet::add_variant` deriva orientaciones baratas (curva de río o camino girados) reutilizando el pintado.

### El colocador (`terrain_place.hpp`)

`place_world` compone un mapa `W×H` (mínimo 8×6) **por casación de bordes**: trazado del río de norte a sur con meandros (las curvas que el catálogo no trae se derivan con `add_variant`), camino horizontal con **puente** donde cruza el cauce, castillo 3×3 (esquinas, muros, `Tower` y `Gate`) y relleno de naturaleza (bosque, rocas, sierra, lago, aldea, cueva). La salida es `TerrainLayout<W,H>` (celdas `u16` row-major) que se envuelve como `eng::playfield::TileLayerMap` **sin conversión** (`cells`/`width`/`height`); `layout_is_connected` verifica que cada unión interna casa (`can_place`). Misma semilla = mismo mapa en host y Amiga (`HOST-442` ancla las estructuras, `HOST-443` el golden).

## 7. Uso típico

```cpp
// 1) Atlas de materiales (una vez, en carga): escalar MiniFloat16 en Amiga.
eng::gfx::proc::TextureAtlas<32, 32, eng::math::MiniFloat16> atlas;
atlas.generate(0x7E44A1u, 16u);

// 2) Set de piezas de terreno (catálogos + variaciones), también en carga.
eng::gfx::proc::terrain::TerrainTileSet<32, 32, 64> set;
set.generate(0xBEE5u, atlas, 6u);

// 3) Colocación por casación: la pieza b entra al lado `side` de a.
if (set.can_place(a, b, eng::gfx::proc::Side::East)) { /* colocar */ }

// 3b) …o un mundo completo con el colocador (río+puente, castillo, aldea):
eng::gfx::proc::terrain::TerrainLayout<10, 7> layout;
if (!eng::gfx::proc::terrain::place_world(layout, set, 0xBEE5u)) { /* fallo visible */ }
// `layout.cells` (row-major) encaja tal cual en `eng::playfield::TileLayerMap`.

// 4) Al mostrarla: los índices del tile son de la paleta del atlas (16 colores);
//    el volcado a planos es el c2p existente.
//    eng::graphics::c2p(chunky, planes, w, h, stride, planes_count);
```

La paleta del atlas se publica como cualquier otra: `atlas.palette` es un `eng::Palette32` (`palette32.hpp`) y se aplica con `Palette`/`FramePlan` (`palette.hpp`).

## 8. Coste y presupuesto

Todo el trabajo es de **generación** (carga): `fill`/`fill_texture` recorren `W·H` píxeles una vez y las texturas pagan `octaves × value_noise2` por píxel; las operaciones por píxel que no son sumas/shifts están documentadas en el código con su coste y su justificación (p. ej. `divu.w` de los surcos de cultivo, divisiones de las elipses ya convertidas a **multiplicación** — `dx²·ry² + dy²·rx² < rx²·ry²` — o las rampas `constexpr` que sustituyen `sin`). **Nada de esto corre por frame**: el camino de frame solo lee el tile ya generado. Método y presupuesto: `docs/guides/optimization/METODO_OPTIMIZACION.md`.

### Coste medido en 68000 (y qué hacer)

Medición con el periférico de depuración (`0xB7E928`) en la demo 119 (release, warp):

| Unidad | Medición | Implicación |
|---|---|---|
| 1 muestra de `value_noise2<MiniFloat16>` | ≈19,7 k ciclos (≈2,8 ms) | el hash son 16 multiplicaciones de 32 bits = `__mulsi3` (~1,1 k ciclos cada una en 68000) |
| 1 textura 32×32 (fBm 3 octavas + dither) | ≈51 k ciclos/píxel | 66 texturas (atlas) ≈ **3,4 G ciclos ≈ 8 min** |
| `fbm2_cached` con rejilla de nodos (`NoiseGrid2`) | evita los hashes pero el perfil MF sigue pesado | ayuda, no basta |

Consecuencias de diseño, ya aplicadas:

- **Los assets de material de un mundo se cocinan en host** (misma librería y semillas:
  determinismo byte a byte anclado por `HOST-443`) y se incrustan; en carga solo se hace lo
  barato: encajar piezas, `place_world` (sin ruido), componer y publicar. Es el patrón que
  sigue la demo 119 (≈1,1 M ciclos de carga) y el que recomienda el roadmap de mundos
  precocinados.
- **Los atajos sin ruido de las primitivas** (`RiverStroke`/`TreeCanopy`/`RockBlob`/
  `LakeBlob` evalúan el jitter ±amp solo en el borde; `fbm2_cached`/`turbulence2_cached`/
  `ridged2_cached` reutilizan la rejilla de nodos por octava) son **exactos bit a bit**:
  `HOST-444` lo verifica contra las funciones libres y `HOST-443` garantiza que no cambian
  ni un píxel del render.
- Mejora futura registrada: un dominio entero (`mulu.w`) para la ruta de rejilla acercaría
  la generación runtime al presupuesto de carga.

## 9. Estado de verificación

- **Host**: `HOST-442` cubre buffer, dither, perfiles, evaluadores, transformaciones Wang, texturas (tileabilidad y rango con dos escalares), atlas (cotas de paleta, determinismo), `MaterialLayer` y terreno (catálogo, casación, agua por el centro del borde, hueco de la puerta).
- **Visual (host)**: los PPM de evidencia (`--dump`, convertidos a hojas de contacto en `out/playground/proc-bitmaps/`) se validaron con el modelo de visión local (Ollama): las muestras de material se describen como texturas orgánicas con estructura (parches de hierba, grietas de roca, espuma, corteza…), los tiles de terreno se reconocen por elemento (muros grises, ríos azules, árboles, casas con tejado rojo, montañas con cima blanca, cueva) y en el mapa de demostración el modelo confirma el **cauce vertical continuo** de la columna (cruzado por el puente), la **estructura lineal gris con hueco oscuro** (muralla con puerta) y que los bordes de las baldosas encajan sin cortes bruscos.
- **On-target (demo 119, `demos/techniques/amiga/effects/119_proc_world`)**: **VERIFICADA en hardware en su alcance**: el mundo de 42 piezas colocado por `place_world` (río continuo con puente, muralla con torre y puerta, cueva, sierra, lago) se compone y publica a 49,9 fps sin parpadeo, con las zonas evaluadas por visión local y el gate de captura de la demo (ver su `VALIDATION.md`). Los tests host `HOST-442`/`HOST-443`/`HOST-444` cubren la generación completa con dos escalares, el golden y la equivalencia de las optimizaciones.
- **SIN VERIFICAR en 68000**: la generación runtime del atlas/set (`atlas.generate` + `set.generate`) — medida (≈3,4 G ciclos) y **no usada** por la demo, que precocina los assets en host (ver §Coste).
