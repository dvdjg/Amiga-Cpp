# Referencia — código dinámico y formatos

El **loader de código relocatable** carga un módulo en RAM, aplica relocaciones y expone símbolos por hash; el contenedor `.engz` comprime y verifica. Permite overlays de código (jefe de zona, minijuego) cargables/descargables. Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §2.

## `DynLoader` — `res/dynloader.hpp`

Soporta **dos formatos**, detectados por el primer longword:

- **`.englib`** (magic `'ENGL'`, `dynloader.hpp:75`): contenedor propio compacto, en orden nativo. El código vive **in situ** en la imagen (la imagen debe permanecer válida). Cabecera de 28 B: `magic`/`version`/`code_size`/`data_size`/`bss_size`/`entry_offset`/`reloc_count`/`export_count`/`import_count`/`reserved` (`EngLibHeader`, `:61`).
- **HUNK** (magic `HUNK_HEADER`): formato nativo AmigaOS (`hunk.hpp`); los segmentos se copian a una `LinearArena` del llamador, así que la imagen puede liberarse tras cargar.

| Tipo | Qué es |
|---|---|
| `LibHandle` (`:38`) | Handle (`0` = inválido). |
| `LibState` (`:41`) | `Empty`/`Ready`/`Unresolved` (cargada con **imports pendientes**)/`Error`. |
| `LibFormat` (`:46`) | `None`/`EngLib`/`Hunk`. |
| `LibExport` (`:47`) | `{name_hash, offset}` desde el inicio del code. |
| `LibImport` (`:54`) | `{name_hash, cell_offset}`: símbolo externo a resolver. |
| `ImportTable` (`:80`) | Tabla de imports; `resolve_imports` (todo o nada). |

`DynLoader::call_init`/`call_fini` invocan el ciclo de vida del módulo; `add_ref`/`release`/`pin`/`unpin`/`refs` gobiernan su permanencia. El loader **no posee** la memoria (la reserva el llamador, p. ej. desde la `AssetCache`): aquí está el parseo + relocación + búsqueda de símbolos, **puro** y host-testable.

## HUNK — `res/hunk.hpp`

`HunkTag` (`hunk.hpp:38`) son los códigos de registro (`HUNK_HEADER`/`CODE`/`DATA`/`BSS`/`RELOC32`/`SYMBOL`/`END`, …). Los bits altos llevan **flags de memoria**: `kHunkFChip`/`kHunkFFast`/`kHunkFAdvisory` (`:67`), con `HunkMem` (`:78`) y `kHunkAnyPolicy = FastPreferred` (`:82`). `HunkSegment` (`:85`) y `HunkSymbolEntry` (`:93`) describen los segmentos y símbolos. `HunkImage` (`:102`) posee su memoria cuando se carga con `MemoryManager` (y `unload(mem)` la libera, R6.3). El formato es **big-endian** (nativo m68k); las relocaciones suman la dirección base del hunk destino a la celda indicada.

## Símbolos — `res/symbol_hash.hpp`

`symbol_hash(const char*)` (`symbol_hash.hpp:16`) y `symbol_hash_n(bytes, n)` (`:27`): **FNV-1a**, la convención de nombres del loader (el `name_hash` de exports/imports de `.englib` y de los símbolos HUNK).

## Contenedor `.engz` — `res/engz.hpp`

Un blob comprimido con cabecera (20 B, little-endian) que declara codec, tamaños, alineación del destino y **CRC-32**:

```
 off  campo                tipo
  0   magic ('ENGZ')       u32    (kEngzMagic = 0x454e475a)
  4   version              u16
  6   codec (res::Codec)   u8
  7   align_log2           u8     (2^align, alineación del destino)
  8   compressed_size      u32
 12   uncompressed_size    u32
 16   payload_crc32        u32
 20   payload              bytes
```

`EngzHeader` (`engz.hpp:38`), `engz_le32`/`engz_le16` (`:49`, `:54`), el parseo/validación (`:67`), `engz_payload` (`:88`), `decode_engz` (`:105`) y `build_engz` (`:121`) para construir el contenedor. Compone la etapa genérica `res::decode` (R6.5).

## Decodificación — `res/decode.hpp`, `res/zx0.hpp`

`Codec` (`decode.hpp:16`) = `Raw`/`Zx0`; `decode(codec, src, dst)` (`:20`) devuelve los bytes escritos o `-1`. `Raw` copia `min(src, dst)`.

`zx0::decompress(src, dst)` (`zx0.hpp:72`) es el **descompresor ZX0** (Einar Saukas, v2), portado del `dzx0.c` de referencia a freestanding sin heap, con destino tipo `Span` y comprobación de límites: es una **etapa genérica** (la usan el codec de audio y el loader de assets; no depende de audio). `BitReader` (`:26`) es el lector de bits MSB-first con el *backtrack* del bit interlace. Verificado con vectores del compresor de referencia (HOST-271). Licencia MIT (ver `https://github.com/einar-saukas/ZX0`).

Volver al [índice de `res/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
