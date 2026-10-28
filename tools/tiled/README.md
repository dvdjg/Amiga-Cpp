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
    backgroundcolor, nextlayerid, nextobjectid, class, objectalignment,
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

## Bake y cooker de perfiles

```bash
# 1) Componer capas/grupos/objetos/imágenes en una imagen bake (primer frame de animaciones)
node tools/tiled/compose-map.mjs "ruta/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.cook-plan.json \
  --out out/assets/tiled/beginning-fields

# 2) Cocinar 16c/32c/EHB, escribir propuesta y promover EHB
node tools/tiled/cook-map.mjs "ruta/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.cook-plan.json \
  --out out/assets/tiled/beginning-fields \
  --profiles 16c,32c,ehb --dither checker --select ehb

# 3) Imponer número de colores/paleta final
node tools/tiled/cook-map.mjs "ruta/Beginning Fields.tmx" --out out/assets/tiled/beginning-fields \
  --palette mi_paleta.json --planes 5 --dither checker --select custom
```

`compose-map` aplica orden de capas, opacidad de capa/grupo, offsets, tile objects con alineación/`tileoffset`, image layers y el primer frame de tiles animados. En mapas finitos recorta al rectángulo lógico del mapa; en infinitos calcula bounds por chunks/objetos. Escribe `composed.png` y `compose.json` con stats, avisos y errores; una imagen faltante es error.

`cook-map` ejecuta `quantize-ehb.mjs` (perfil forzado), `slice-tiles.mjs` con `--dither checker` y `emit-xlimited-bank.mjs` (1..6 planos), y escribe `proposal.json`/`proposal.md` con tiles, banco, PSNR y estimación Chip. La paleta automática no se calcula solo sobre `composed.png`: `palette-source.png` compone todas las capas y objetos con su primer frame, de modo que las capas vivas (agua, flores, fuego) también entran en la cuantización. `quantize-ehb` usa semillas farthest-first ponderadas por frecuencia y k-means half-aware con mediana ponderada; no hay una paleta fija por mapa. La recomendación automática es una **propuesta**, no una imposición: EHB se sugiere con capas de sombra o >32 colores; `--select` promueve el perfil elegido a `selected/`, genera `tiled_map.h`, un `MapBundle` v1 (`map.bundle`/`map_bundle.h`) y, si el plan declara objetos `foreground`/`occluder`, los assets `objects.h`, `object_sheet.bin`, `object_masks.bin` y `objects.json`. `--palette` sigue disponible para imponer una paleta externa y `--planes` su profundidad. El checker es el optimal-checker de dos tonos consecutivos por luminancia portado de `amiga-tiles`; `--dither none` conserva el comportamiento anterior.

Salidas por perfil en `profiles/<perfil>/`: `tilebank.raw.bin` (1 B/píxel), `tilebank_indexed.h`, `tilebank.xlimited.bin`, `tiles.json`, `reconstruct.png`, `tilebank.png`, `quant/palette.json`. `selected/` contiene `map.bundle` + `map_bundle.h` (MapBundle v1 BE), `tiled_map.h`, `tilebank_indexed.h` y `tilebank.xlimited.bin`; el runtime lo lee con `eng::map::MapAssetView`/`LayerView` (HOST-498). Si hay objetos de runtime, `selected/` añade `objects.h` (tabla `MapObjectView`/`MapObjectImageView` ordenada por `foot_y`), `object_sheet.bin` (planos con guarda por fila para el shift fino) y `object_masks.bin` (máscara 1bpp), consumidos por `eng::map::MapObjectsView`.

### Fusión automática de animaciones

`cook-animations` agrupa por gid y, además, busca **clusters rectangulares de celdas animadas contiguas** que puedan cocinarse como un solo sprite de mapa. Las reglas son generales: misma capa, bounding box completo, misma línea temporal de frames, al menos 2 celdas/2 gids y un máximo de 8 celdas. Si un gid se repite en el mapa, la fusión duplica frames: se permite solo si `animations.fusion.max_duplicate_bytes cubre el coste (por defecto 0, que exige gids únicos). El grupo fusionado declara `pixel_width/pixel_height` y `sheet_offset/frame_stride/mask_offset/mask_stride`; el runtime lo dibuja con un blit por segmento y `MapCellSaveCacheSet` usa su arena grande. La decisión se registra en `animations.json` (`fused`) y no depende de ningún objeto concreto: la fogata 2×2 de Beginning Fields entra sola porque cumple las reglas.

## Objetos: roles, `foot_y` y MapObjects

`map-objects.mjs` resuelve el rol de cada objeto con este orden: propiedades del objeto (`cook`, `sort_y`, `occluder`) → reglas de `objects.<capa>.rules` (primera coincidencia por `id`/`gid`/`class`/`type`/`name`) → `objects.<capa>.default`. Los modos válidos son `baked`, `foreground`, `occluder`, `collision` y `skip`; `sort_y` es `base` (clave = pie) o `top`; `occluder=canopy` convierte un `foreground` en `occluder` (se dibuja después de los actores). Los objetos `baked` los funde `compose-map`; los `foreground`/`occluder` se emiten como registros de runtime con `foot_y` (pie real) y `sort_key`, ordenados por profundidad. `cook-objects.mjs` rechaza objetos escalados, rotados o sin `gid` porque el runtime dibuja el arte nativo 1:1. Si el gid tiene animación de Tiled, emite sus frames contiguos y una tabla `kMapObjectAnimations`; `eng::map::map_object_sprite` + `MapSpriteCache` permiten dibujar el objeto en cualquier píxel de mundo (no exige alineación a tile).

La demo `208_tiled_occlusion_objects` es el consumidor de referencia. Su plan `fixtures/beginning-fields.objects.cook-plan.json` cuece la casa (por defecto) y deja dos árboles como objetos de runtime (id 43 occluder y id 45 foreground).


## Fixtures y tests

`fixtures/` contiene XML pequeño y determinista; las imágenes apuntan a `assets/amiga/hud/test_icons.png` (ya versionado) y el parser no las decodifica. Ver `fixtures/README.md`.

```bash
node tools/tiled/test-parse-tmx.mjs
node tools/tiled/test-cook.mjs
```

`test-parse-tmx` cubre XML, atlas/colección, animaciones, objetos, propiedades, grupos, CSV/base64 gzip/zlib, chunks negativos, flips, GID fuera de rango/no definido, zstd y orientación rechazada. Si existe el proyecto local `Beginning Fields.tmx`, lo parsea como integración y comprueba 18 tilesets, 7 capas de tiles, 2 objectgroups, 169 objetos y 256 GID únicos. `test-cook-plan` valida el contrato de capas y las reglas por objeto; `test-cook` comprueba el bake del fixture, los tres perfiles, la propuesta/`selected`, la paleta impuesta, la emisión de `objects.h`/sheet/máscara cuando una regla marca un objeto `foreground` y los casos de fusión (cluster rectangular, hueco y cadencia distinta). Ambos se ejecutan en la pasada completa de `tools/run-host-tests.sh`.

## Animaciones y tamaño de tile

`tools/tiled/cook-animations.mjs` acepta `--tile 16` o `--tile 32`; si se omite usa `map.tilewidth`. Solo admite tiles cuadrados de 16×16 o 32×32; rechaza 8×8, tiles rectangulares y mapas cuyo `tilewidth` no coincida. El tamaño se propaga a `TILED_ANIM_TILE`, `ANIM_FRAME_STRIDE`, `ANIM_MASK_STRIDE`, `TILED_ANIM_FRAME_STRIDE` y `TILED_ANIM_MASK_STRIDE`.

Ejemplo:

```bash
node tools/tiled/cook-animations.mjs --ir ir.json --plan plan.json \
  --palette palette.json --planes 6 --tile 32 --out selected/
```

## Límites actuales

- El parser no lee imágenes; `compose-map`/`cook-map` sí, con `pngjs`, en host.
- El formato actual es un **bake opaco** + banco/mapa C + objetos de runtime; no hay todavía `MapBundle` UAF-R, catálogo `VisualId`, páginas runtime ni capas `Overlay`/`Occluder` separadas como celdas.
- Los objetos de runtime usan el arte nativo (escala 1:1) y máscara 1bpp; no se escalan, rotan ni cuecen variantes de flip por objeto (los flips del `gid` se aplican al muestrear).
- La demo 208 emite los blits y verifica en destino que los bits coinciden, pero la composición visible del objeto sigue **NO VERIFICADA** (ver `VALIDATION.md`); el resto del pipeline host está cubierto por tests.
- No evalúa wangsets/terrain/rules: consume las capas resultantes (el cooker las trata como cualquier capa).
- Rechaza `template` de objeto, orientaciones hex/staggered y compresión `zstd` con diagnósticos; no las convierte en silencio.
- El IR JSON puede ser grande (un mapa 40×40 con 7 capas y objetos ronda 1,5 MB con indentación); es un intermedio host, no un asset de runtime.
- Las animaciones se hornean con su primer frame; el `TileAnimator` por Blitter es T5. Las plantillas de objeto y la política de capas se cierran en T5/T6.
