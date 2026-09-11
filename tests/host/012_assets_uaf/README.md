# HOST-012 — assets UAF-R (contenedor de chunks)

Test host de `eng::assets::Blob` (`engine/include/eng/assets/uaf.hpp`), base de la
capa `eng::assets` (UAF-R, `ENGINE_DESIGN.md` §2.7).

## Qué valida

- `bind` de un blob válido (magic `UAFR`, versión, chunks con offset/size en rango).
- `chunk_count`, `chunk(i)`, `find(type)` y `data(i)` (vista acotada).
- Errores: magic inválido, chunk que se sale del blob y blob demasiado corto → `false`.
- `BlobWriter` round-trip: `begin`/`add_chunk`/`finish` → `bind` → vistas tipadas
  `PaletteView` (colores RGB444 big-endian) y `SampleView` (bytes con signo).
- `Reader` tipado big-endian con comprobación de límites (`read_u8/16/32`, `skip`,
  `take`) — la primitiva segura para parsear chunks.
- `BitplanesView`: cabecera de geometría + datos planares validados.

> Nota: los métodos del `Reader` se llaman `read_u8/16/32` (no `u8/16/32`) porque un
> método con el mismo nombre que el tipo `eng::u8/16/32` lo **sombrea** dentro de la
> clase y rompe las firmas que lo usan.

## Formato

```
header  { u32 magic="UAFR", u16 version, u16 chunk_count }
chunk[] { u16 type, u16 count, u32 size, <size bytes>, pad a 4 }
```

Big-endian (nativo m68k); los lectores `read_be16/32` funcionan igual en host.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/012_assets_uaf
```
