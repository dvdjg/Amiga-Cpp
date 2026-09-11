# UAF-R packer (`tools/assets/uaf-pack.ts`)

Exportador host del contenedor de assets del engine (**UAF-R**), el formato "cocinado" que consume `eng::assets::Blob` (`engine/include/eng/assets/uaf.hpp`). Adapta el concepto del *cooked asset* del proyecto **Universal Asset Format** (authoring `.uaf` → runtime nativo) al formato binario que lee el Amiga sin parsing pesado.

## Formato UAF-R

Contenedor de chunks en big-endian (nativo m68k; los lectores funcionan igual en host):

```
header  { u32 magic="UAFR", u16 version, u16 chunk_count }
chunk[] { u16 type, u16 count, u32 size, <size bytes>, pad a 4 }
```

Tipos de chunk (alineados con `eng::assets::ChunkType`): `Palette=1`, `Bitplanes=2`, `CopperTemplates=3`, `PatchTables=4`, `Sprites=5`, `Bobs=6`, `Tiles=7`, `Collision=8`, `Strings=9`, `Samples=10`, `Modules=11`.

- **Palette**: N colores RGB444 en `u16` big-endian.
- **Bitplanes**: cabecera de geometría (`u16 width, u16 height, u16 row_bytes, u8 planes, u8 layout, u8 flags, u8 resv`) seguida de los planos.
- **Samples**: bytes 8-bit con signo, tal cual.

## API

El módulo exporta funciones puras (host-testables) y una CLI:

- `packUaf(chunks)` / `parseUaf(buf)` — empaqueta y valida (offset/size en rango).
- `bitplanesFromIndexed(width, height, planes, pixels)` — **chunky indexado → bitplanes separados** (el paso "amiga convert" del core UAF), con `width` múltiplo de 8.
- `paletteChunkData(colors)` / `bitplanesChunkData(...)` / `sampleChunkData(bytes)` / `stringsChunkData(strings)` / `tilesChunkData(tiles)` — datos de cada chunk. Los consumidores runtime equivalentes son `eng::assets::{PaletteView, BitplanesView, SampleView, StringsView, TilesView}`.
- CLI: `node dist/tools/assets/uaf-pack.js <out.uafr>` genera un blob de demostración (paleta + bitplanes 16×16 + sample) y **auto-valida** la salida con `parseUaf`.

## Ejecutar y probar

```
node dist/tools/assets/uaf-pack.js out/assets/uaf/demo.uafr
node dist/tools/assets/test-uaf-pack.js
```

El test (`test-uaf-pack.ts`) valida el round-trip del contenedor (con padding), la conversión chunky→planar y los errores (magic inválido, blob corto, geometría inválida). El runtime C++ que lee el mismo formato está validado por `tests/host/012_assets_uaf`.

## Extensiones previstas

El formato crecerá (más tipos de chunk, metadatos de escena, contratos runtime) manteniendo el contrato de contenedor: nuevos consumidores son nuevas vistas tipadas sobre `Blob::data(chunk)`, sin romper lo existente.
