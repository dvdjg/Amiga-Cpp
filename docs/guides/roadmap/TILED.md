# Tiled (.tmx/.tsx) — conocimiento preservado

Para no perder nada del formato de mapas de Tiled (se usará cuando haya metadatos; el
extractor de tiles de bitmap en crudo no los necesita).

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
- `tools/ehb/parse-tmx.mjs <mapa.tmx>`: extrae mapa/tilesets/capas (CSV o `<tile>`),
  limpia flips; con `--resolve-tsx` lee los `.tsx` para resolver la imagen de cada tileset.
  Esto habilita: gid → (imagen, índice) → índice en el banco EHB (si se corta el sheet con
  `slice-tiles.mjs`).
- El extractor "bitmap en crudo" (`quantize-ehb.mjs` + `slice-tiles.mjs`) NO necesita Tiled.

## Futuro conversor completo
`https://github.com/tinic/png2amiga` tiene muchas rutinas de conversión (interleaved,
paletas, etc.) que de momento no necesitamos; se evaluará más adelante (clonar en
`programa/AI/Amiga/png2amiga`). El conocimiento de Tiled y las rutinas crudo del repo se
mantienen como base propia verificada.