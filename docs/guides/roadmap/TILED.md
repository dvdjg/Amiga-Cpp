# Tiled (.tmx/.tsx) — estado actual y límites

Este documento conserva el subconjunto Tiled que hoy entiende el extractor y evita confundirlo con
soporte completo del editor. El plan de soporte nativo, incluidos tilesets, mapas multicapa, formatos
isométricos y streaming de páginas, está en [`ROADMAP_TILED_WORLD_SUPPORT.md`](ROADMAP_TILED_WORLD_SUPPORT.md).

## Qué es Tiled y qué genera
Tiled es un editor de mapas. Emite `.tmx` (XML del mapa) y `.tsx` (definición de tileset
externa). El mapa referencia tilesets por `firstgid`; los tiles se referencian por `gid`
global (base del tileset + índice del tile + bits de flip).

## Estructura de un .tmx
- `<map width height tilewidth tileheight orientation>`: tamaño en tiles + tamaño de tile.
- `<tileset firstgid="N" source="x.tsx">` o inline: cada tileset aporta un rango de gids.
- `<layer name width height>` con `<data encoding="csv">0,1,...</data>` (o `<tile gid="N"/>`):
  los gids por celda; capas como Ground/Road/Water (separan material).
- Opcional `<objectgroup>` (colisiones).

## gid y bits de flip
`gid & 0x1FFFFFFF` = tile; bits 29-31 = flip horizontal (`0x80000000`), vertical
(`0x40000000`), diagonal (`0x20000000`). Limpiar antes de indexar.

## .tsx externo
Un `.tsx` contiene `<image source="x.png" width height>` + `tilecount`/`columns` y, a veces,
particularidades por tile. El orden de los tiles del sheet (row-major) es el orden de los
gids dentro de su `firstgid`.

## Herramientas del repo
- `tools/ehb/parse-tmx.mjs <mapa.tmx>`: extrae un subconjunto de mapa/tilesets/capas soportando las
  codificaciones de Tiled (CSV, XML `<tile>` y base64 con gzip/zlib), mapas finitos e
  infinitos por `<chunk>` (coordenadas negativas incluidas) y limpia los flips;
  con `--resolve-tsx` lee los `.tsx` para resolver la imagen de cada tileset.
  Los bits de flip se limpian para obtener el id, pero las transformaciones aún no se preservan.
  Esto habilita: gid → (imagen, índice) → índice en el banco EHB (si se corta el sheet con
  `slice-tiles.mjs`).
- `gid-to-bank.mjs` → `pack-world.mjs` (**implementado**): empaqueta el mapa en el **formato de
  mundo incrustable** (chunks binarios, `gid` resuelto en el host) que consume el motor de scroll.
  Formato y mapeo runtime: `docs/engine/architecture/WORLD_FORMAT.md`.
- El extractor "bitmap en crudo" (`quantize-ehb.mjs` + `slice-tiles.mjs`) NO necesita Tiled.

## Límites actuales y soporte completo

`parse-tmx.mjs` es un extractor preliminar basado en expresiones regulares, no un parser XML
estructural. Conserva orientación como campo pero el packer actual materializa una capa seleccionada
y un tileset/banco indicado, limpiando flags GID en lugar de preservarlos como transformaciones. No
cocina por sí solo TSX/image collections, propiedades por tile, animaciones, capas de objetos ni
metadata de colisión a runtime. `WorldMap` sirve de base sparse, pero su descriptor actual referencia
un solo banco/paleta y no resuelve residency de tileset en Chip RAM.

El extractor "bitmap en crudo" (`quantize-ehb.mjs` + `slice-tiles.mjs`) no necesita Tiled; el
pipeline completo debe combinarlo con el parser/cooker y emitir assets Amiga listos para el driver.
Las fases, gates y contratos están en `ROADMAP_TILED_WORLD_SUPPORT.md`.

### Conversor externo
`https://github.com/tinic/png2amiga` tiene muchas rutinas de conversión (interleaved,
paletas, etc.) que de momento no necesitamos; se evaluará más adelante (clonar en
`programa/AI/Amiga/png2amiga`). El conocimiento de Tiled y las rutinas crudo del repo se
mantienen como base propia verificada.
