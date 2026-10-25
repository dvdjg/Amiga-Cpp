# Tiled (.tmx/.tsx) — estado del importador y límites

Este documento describe el **subconjunto de Tiled que entiende el importador actual** y sus límites. El modelo runtime objetivo, la política de cocción, los contratos internos y la API de intenciones están en [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md); las fases y gates, en [`ROADMAP_TILED_WORLD_SUPPORT.md`](ROADMAP_TILED_WORLD_SUPPORT.md).

## Qué es Tiled y qué genera

Tiled es un editor de mapas. Emite `.tmx` (XML del mapa) y `.tsx` (definición de tileset externa). El mapa referencia tilesets por `firstgid`; los tiles se referencian por `gid` global (base del tileset + índice del tile + bits de flip). Las imágenes del tileset y los assets de los objetos son ficheros aparte (PNG u otros formatos de imagen).

## Estructura de un `.tmx`

- `<map version tiledversion orientation renderorder width height tilewidth tileheight infinite nextlayerid nextobjectid>`: metadatos del mapa. `orientation` puede ser `orthogonal`, `isometric`, `staggered` o `hexagonal`; el importador actual solo interpreta de forma completa el ortogonal.
- `<tileset firstgid="N" source="x.tsx">` o inline: cada tileset aporta un rango de gids. Un tileset de **atlas** tiene una imagen única con rejilla (`columns`, `margin`, `spacing`, `tilecount`); una **colección de imágenes** (`columns="0"`) tiene una imagen por tile con tamaño propio.
- `<layer name width height>` con `<data encoding="csv">0,1,...</data>`, `<tile gid="N"/>` o base64 (sin compresión, gzip o zlib). Las capas separan material (Ground/Road/Water/…).
- `<objectgroup>` con objetos de rectángulo, polígono, polilínea, punto o *tile object* (`gid`); los tile objects usan la imagen y la alineación del tile referenciado.
- `<imagelayer>` para imágenes de fondo/parallax y `<group>` para anidar capas con visibilidad, opacidad, offsets y paralaje.
- `<properties>` por mapa, tileset, tile, capa u objeto. Tiled 1.9+ admite propiedades tipadas y clases; el importador actual no las procesa.
- Wangsets, terrain y rules son datos de **autoría**: el cooker consume las capas resultantes (p. ej. `RockSlopes_Auto`), no evalúa reglas en runtime.

## GID y bits de flip

`id = gid & 0x1FFFFFFF` es el id global del tile. Los bits 31–29 son transformaciones: bit 31 = flip horizontal (`0x80000000`), bit 30 = flip vertical (`0x40000000`), bit 29 = flip diagonal (`0x20000000`). Combinaciones de H/V/D producen rotaciones. El importador debe limpiar los bits para indexar y **preservar las transformaciones**; no puede descartarlas en silencio. Los mapas hexagonales usan además bits de rotación que quedan fuera de alcance.

## `.tsx` externo

Un `.tsx` contiene `<image source="x.png" width height>` + `tilecount`/`columns` y, a veces, `tileoffset`, `margin`, `spacing`, animaciones por tile (`<animation><frame tileid duration/></animation>`) y `objectgroup` por tile con polígonos de colisión. El orden de los tiles del atlas (row-major) es el orden de los gids dentro de su `firstgid`. En una colección de imágenes no hay orden de rejilla: cada tile declara su propia imagen y tamaño.

## Parser estructural (T1)

`tools/tiled/parse-tmx.mjs` es el parser estructural del pipeline nuevo. Emite `eng.tiled-ir` v1 y resuelve:

- XML con anidamiento, atributos, entidades, CDATA, comentarios y ubicación (línea/columna) para diagnósticos.
- `.tmx`/`.tsx` externos e inline, atlas y colecciones de imágenes, `firstgid`/`tilecount`/`maxTileId`/`tileIds`, `columns`, `margin`, `spacing`, `tileoffset`, `objectalignment`, `grid`.
- Capas `layer`, `objectgroup`, `imagelayer` y `group` con visibilidad, opacidad, offsets, paralaje, tint, propiedades y clases; los objetos incluyen rectángulo, elipse, punto, polígono, polilínea, texto y *tile objects*.
- Animaciones por tile y `objectgroup` de colisión por tile; propiedades tipadas y clases anidadas.
- Codificaciones CSV, XML `<tile>` y base64 con gzip/zlib; `zstd` se rechaza con diagnóstico.
- Mapas finitos e infinitos por `<chunk>` con coordenadas negativas, conservando el origen lógico.
- GID completos: `id = gid & 0x1FFFFFFF` + flips H/V/D en crudo (`decodeGid()` los separa); no se descartan transformaciones.
- Validación de conteos, índices, rangos de tileset (incluidos huecos de colecciones), GID fuera de rango/no definido y orientaciones hex/staggered.
- Plantillas de objeto (`template`) se rechazan explícitamente hasta su soporte; wangsets/terrain se tratan como datos de autoría y no se evalúan.

El IR, los fixtures y el autotest están documentados en `tools/tiled/README.md`; el test se ejecuta en `tools/run-host-tests.sh`. La integración local con `Beginning Fields` comprueba 18 tilesets, 7 capas de tiles, 2 objectgroups, 169 objetos y 256 GID únicos sin errores ni warnings.

## Extractor legacy

`tools/ehb/parse-tmx.mjs` es un **extractor preliminar basado en expresiones regulares**, no un parser XML estructural. Soporta mapas finitos/infinitos, CSV/XML/base64 gzip/zlib, TSX externos básicos y limpieza de flips, pero no lee grupos, propiedades, animaciones, objetos ni colecciones. `gid-to-bank.mjs`/`pack-world.mjs` siguen convirtiendo una capa y un tileset/banco a `WorldMap`. Se conserva como compatibilidad hasta T9; el pipeline nuevo no depende de él.

## Estrategia de importación canónica

Tiled es **formato de autoría**, no de runtime. La estrategia es un **cooker host**:

```text
.tmx + .tsx + imágenes → parser estructural → eng.tiled-ir → cook-plan → MapBundle (UAF-R) + manifiesto + informe
```

- El Amiga no parsea XML, TSX ni PNG; no cuantiza ni descomprime intercambios de Tiled en runtime.
- El bundle usa `WorldMap` v1 para capas de banco único y `MapLayerCells`/`WorldMap` v2 para capas con catálogo/páginas, y añade catálogo de tiles, páginas, animaciones, objetos y colisión (ver `TILED_MAP_RUNTIME.md` §4).
- Las capas estáticas se cuecen en un fondo base; las vivas se limitan a animación, visibilidad, paralaje u oclusión. El perfil «todas las capas vivas» queda disponible para prototipos, con su coste.
- El preview PNG del mapa (`Beginning Fields.png`) es una **referencia visual**, no un atlas de tiles: el cooker usa los TSX y sus imágenes fuente. `tmxrasterizer` del proyecto Tiled genera golden renders completos y por capa.

## Herramientas del repo

- `tools/tiled/parse-tmx.mjs` — parser estructural → `eng.tiled-ir` v1 (T1, implementado); `xml.mjs` y fixtures/autotest en la misma carpeta.
- `tools/tiled/compose-map.mjs` — compone capas/grupos/objetos/imágenes en una PNG bake (T2/T3, implementado).
- `tools/tiled/cook-map.mjs` — cuantiza perfiles 16c/32c/EHB, emite propuesta y promueve `selected/` (T2/T3 parcial; `--palette`/`--planes` para imponer la paleta final).
- `tools/ehb/quantize-ehb.mjs` y `slice-tiles.mjs` — paleta y corte/dedupe; `slice-tiles --dither checker` implementa el optimal-checker para transiciones.
- `tools/ehb/emit-xlimited-bank.mjs` — banco interleaved prehecho para 1..6 planos.
- `tools/ehb/parse-tmx.mjs` — extractor legacy (regex). Compatibilidad hasta T9; el pipeline nuevo no depende de él.
- `tools/ehb/gid-to-bank.mjs` y `tools/ehb/pack-world.mjs` — conversión de una capa a `WorldMap`; formato en `WORLD_FORMAT.md`.
- `tools/amiga-tiles/amiga-tiles.mjs` — cuantización/corte/dedupe/tilebank (2..255 colores, EHB, dither, layouts); no conoce Tiled.
- Pendiente: `MapBundle` UAF-R, páginas/catálogo runtime y el visor de presupuesto. El flujo actual produce assets C (`selected/tiled_map.h` + banco) para la demo 206.

## Conversor externo (`png2amiga`)

`https://github.com/tinic/png2amiga` contiene rutinas de conversión (interleaved, paletas, layouts) que pueden servir como **referencia host**. No se integra como runtime ni se mantiene una segunda tubería: el repo ya tiene `tools/amiga-tiles` para cuantización y `tools/ehb` para mundo. Si una rutina de `png2amiga` aporta una mejora demostrable, se porta al pipeline host con test de equivalencia y atribución, no se enlaza desde el Amiga.

## Límites actuales y soporte completo

El importador actual no es un cooker de proyecto Tiled: materializa una capa y un banco, limpia flips y no lee objetos, animaciones ni propiedades. `WorldMap` sirve de base sparse, pero su descriptor actual referencia un solo banco/paleta y no resuelve residencia de páginas de tileset en Chip. El plan para cerrar estos huecos, con niveles N0–N4, demos 206–210 y gates de presupuesto/visión, está en [`ROADMAP_TILED_WORLD_SUPPORT.md`](ROADMAP_TILED_WORLD_SUPPORT.md) y [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md).

## Fixture `Beginning Fields`

`C:\Users\dvdjg\Documents\programa\Assets\2D\The Fan-tasy Tileset (Free) 1.5.9\Tiled\Tilemaps\Beginning Fields.tmx` (Tiled 1.11.2, TMX 1.10) es el fixture principal de N1/N2:

- Ortogonal, finito, 40×40 celdas de 16×16 (640×640 px).
- 18 tilesets: atlas y colecciones de imágenes; 7 capas de tiles (`Ground`, `Flowers`, `Road`, `Shadows` dentro de un grupo con opacidad 0.25, `RockSlopes` oculta, `RockSlopes_Auto`, `Water`) y 2 objectgroups (`Object Shadows`, `Object Layer 1`).
- 169 objetos de tile, 58 imágenes de objeto únicas; animaciones de agua (50 tiles base, 4 frames de 300 ms), flores (7+7 tiles base, 8 frames de 150/100 ms) y campfire (4 tiles base, 8 frames de 100 ms).
- Polígonos de colisión por tile en varios TSX (agua, rocas, edificios, props).
- El PNG `Beginning Fields.png` de 640×640 es el render plano del mapa; el banco `out/assets/beginning-fields/8c` se extrajo de ese PNG y no representa el proyecto Tiled de origen.

La licencia del tileset debe revisarse antes de copiar arte al repo; la fuente puede leerse de su ruta local y la trazabilidad se añade en `assets/README.md` si se versiona.

## Referencias

- [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md) — modelo runtime, contratos y API.
- [`ROADMAP_TILED_WORLD_SUPPORT.md`](ROADMAP_TILED_WORLD_SUPPORT.md) — fases T0–T9 y gates.
- [`WORLD_FORMAT.md`](../../engine/architecture/WORLD_FORMAT.md) — `WorldMap`.
- [`CONTENT_AND_TILEMAP.md`](../../engine/architecture/CONTENT_AND_TILEMAP.md) — contenido y mundo disperso.
- Proyecto Tiled local: `C:\Users\dvdjg\Documents\programa\tiled`; especificación `docs/reference/tmx-map-format.rst`, `docs/reference/tmx-changelog.rst`, `docs/reference/support-for-tmx-maps.rst`; render `src/tmxrasterizer`.
