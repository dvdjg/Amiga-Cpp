# tools/tiled — parser estructural de Tiled e IR

Herramientas **host** para convertir proyectos Tiled en el IR canónico `eng.tiled-ir` que alimentará al cooker (`T2`/`T3`). No son runtime ni se enlazan en el Amiga: el motor nunca parsea XML, TSX ni PNG.

El modelo de soporte, el formato `MapBundle` y la API de intenciones están en [`docs/engine/architecture/TILED_MAP_RUNTIME.md`](../../docs/engine/architecture/TILED_MAP_RUNTIME.md); las fases y gates, en [`docs/guides/roadmap/ROADMAP_TILED_WORLD_SUPPORT.md`](../../docs/guides/roadmap/ROADMAP_TILED_WORLD_SUPPORT.md). El subconjunto del importador antiguo está en [`docs/guides/roadmap/TILED.md`](../../docs/guides/roadmap/TILED.md).

## Uso

```bash
# Parsear y resumir (resuelve TSX externos; imprime diagnósticos)
node tools/tiled/parse-tmx.mjs "ruta/Beginning Fields.tmx"

# Escribir el IR JSON (el cooker lo consume; puede ser grande)
node tools/tiled/parse-tmx.mjs "ruta/Beginning Fields.tmx" --out out/tmp/beginning-fields.ir.json

# IR por stdout (para integraciones)
node tools/tiled/parse-tmx.mjs tools/tiled/fixtures/ortho.tmx --json --quiet

# Fallar también con warnings
node tools/tiled/parse-tmx.mjs tools/tiled/fixtures/ortho.tmx --strict
```

Códigos de salida: `0` OK, `1` errores de validación (o warnings con `--strict`), `2` uso o XML inválido.

## Qué hace

- Parsea XML con `xml.mjs` (elementos anidados, atributos, entidades, CDATA, comentarios, DOCTYPE/PI) y conserva línea/columna para diagnósticos.
- Resuelve `.tsx` externos relativos al `.tmx` (o inline) y valida rangos `firstgid`/`tilecount`/`maxTileId`, incluyendo colecciones de imágenes con IDs locales con huecos.
- Lee capas `layer`, `objectgroup`, `imagelayer` y `group`, con visibilidad, opacidad, offsets, paralaje, tint, propiedades y clases.
- Decodifica `data` en CSV, XML `<tile>` y base64 con gzip/zlib; rechaza `zstd` con un diagnóstico explícito.
- Soporta mapas finitos e infinitos por `<chunk>` con coordenadas negativas; conserva el origen lógico.
- Conserva los GID **en crudo** (id + flips H/V/D) en celdas, chunks y objetos; `decodeGid()` los separa. No descarta transformaciones.
- Lee tilesets de atlas y de colección, `tileoffset`, `margin`, `spacing`, `objectalignment`, animaciones por tile y `objectgroup` de colisión por tile.
- Lee objetos rectángulo/elipse/punto/polígono/polilínea/texto, *tile objects* y propiedades; las plantillas (`template`) se rechazan explícitamente hasta su soporte.
- Valida conteos, índices, rangos de tileset y sintaxis; los errores son diagnósticos con `source`, `line`, `column` y ruta lógica.

## IR `eng.tiled-ir` v1

```text
{
  format: "eng.tiled-ir",
  version: 1,
  source: "/ruta/al/mapa.tmx",
  map: {
    version, tiledversion, orientation, renderorder,
    width, height, tilewidth, tileheight, infinite,
    staggeraxis, staggerindex, hexsidelength,
    backgroundcolor, nextlayerid, nextobjectid, class,
    parallaxoriginx, parallaxoriginy,
    properties: [Property], attrs: {...}
  },
  tilesets: [{
    firstgid, lastgid, source, sourcePath, name, class,
    tilewidth, tileheight, tilecount, maxTileId, tileIds,
    columns, margin, spacing, objectalignment,
    tileoffset: {x,y}|null, grid: {orientation,width,height}|null,
    image: Image|null,
    tiles: [Tile],
    properties: [Property], attrs: {...}
  }],
  layers: [Layer|Group|ObjectGroup|ImageLayer],
  diagnostics: [{severity, code, message, source, line, column, path}],
  stats: {tilesets, layers, tileLayers, objectLayers, imageLayers,
          groupLayers, objects, uniqueGids, errors, warnings}
}
```

Las celdas son `u16`/números JS con el GID **sin limpiar**; `0` es vacío. Un GID con flips se decodifica con `decodeGid(gid)` (`id`, `flipH`, `flipV`, `flipD`). Los `VisualId` estables, las páginas y las transformaciones materializadas son responsabilidad del cooker (T2/T3), no del IR.

### Propiedades

`Property` conserva `name`, `type`, `propertytype` (si existe), `value` tipado (`int`/`float`/`bool`/`string`; `color`/`file`/`object`/`class` como cadena) y `properties` anidado para clases. No se aplican en runtime automáticamente: el cook-plan decide qué propiedades interpreta.

### Tilesets de colección

En `columns="0"` los IDs locales pueden tener huecos: `tilecount` cuenta los tiles definidos, `maxTileId` es el máximo id local y `tileIds` la lista ordenada de ids definidos. El rango de GID llega a `firstgid + maxTileId` y la validación comprueba pertenencia exacta; no se asume una rejilla contigua.

## Fixtures y tests

`fixtures/` contiene XML pequeño y determinista; las imágenes apuntan a `assets/amiga/hud/test_icons.png` (ya versionado) y el parser no las decodifica. Ver `fixtures/README.md`.

```bash
node tools/tiled/test-parse-tmx.mjs
```

El autotest cubre XML, atlas/colección, animaciones, objetos, propiedades, grupos, CSV/base64 gzip/zlib, chunks negativos, flips, GID fuera de rango/no definido, zstd y orientación rechazada. Si existe el proyecto local `Beginning Fields.tmx`, lo parsea como integración y comprueba 18 tilesets, 7 capas de tiles, 2 objectgroups, 169 objetos y 256 GID únicos. El test se ejecuta en la pasada completa de `tools/run-host-tests.sh`.

## Límites actuales

- No lee imágenes ni cocina tiles/paletas/páginas; eso es T2/T3.
- No evalúa wangsets/terrain/rules: consume las capas resultantes (el cooker las trata como cualquier capa).
- Rechaza `template` de objeto, orientaciones hex/staggered y compresión `zstd` con diagnósticos; no las convierte en silencio.
- El IR JSON puede ser grande (un mapa 40×40 con 7 capas y objetos ronda 1,5 MB con indentación); es un intermedio host, no un asset de runtime.
- Las plantillas de objeto, la selección de paleta y la política de capas se cierran en T2/T3/T6, no en el parser.
