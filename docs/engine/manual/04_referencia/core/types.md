# Referencia — `core/types/`

Tipos base, **dominios** (`Tag`) y **vistas tipadas** con banco de memoria. Es el vocabulario de tipos sobre el que se construye toda la API: cuando una función del engine recibe `ChipView<PlaneTag>`, el nombre del tipo ya dice **qué es** (un plano) y **dónde está** (Chip RAM).

## Tipos base — `types.hpp`

Anchos exactos, indiferentes al compilador: `u8`/`u16`/`u32`/`u64`, `s8`/`s16`/`s32`/`s64`, `usize`, `uintptr` (`types.hpp:31`). `pix` es el tipo de coordenada de píxel: `s16` en modo *retro16*, `s32` en *retro32* (`types.hpp:56`). `Result` es el resultado de operaciones que pueden fallar (`types.hpp:89`). `align_up(value, alignment)` sube a la alineación pedida (`types.hpp:102`).

## Dominios (`Tag`) — `domains.hpp`

Un `Tag` es una **etiqueta vacía** que se ata a una vista para nombrar su dominio en el tipo:

```
struct PlaneTag {};   // →  PlaneBytes = Bytes<PlaneTag>
struct BobTag {};     // BOB planar: planos de color + máscara cookie-cut
struct CopperTag {};  // lista de Copper
struct TileBankTag {};// banco de tiles
```

Están `Pattern`/`Plane`/`Palette`/`Sprite`/`Copper`/`TileBank`/`Chunky`/`Work`/`Texture`/`Mask`/`Audio`/`Music`/`IndexedTiles`/`Uaf`/`MapCells`/`Bob`/`MixerBuffer`/`Stack`/`LibSegment` (`domains.hpp:17`). Los alias de vista derivan de ahí: `Pattern`/`PatternWords`, `PlaneBytes`/`PlaneViewBytes`, … (`domains.hpp:40`).

## Vistas tipadas — `typed.hpp`

`TaggedSpan<T, Tag>` es un `Span<T>` que arrastra el `Tag`. Los alias de byte/word son `Bytes`/`ByteView`/`Words`/`WordView` (`typed.hpp:60`). `MemView<Tag, Bank>` añade el **banco de memoria** al tipo; los alias `ChipView`/`SlowView`/`FastView` (`typed.hpp:173`). `as_chip(v, off)` **certifica** el banco (es el acto explícito de decir «esto vive en Chip», `typed.hpp:181`). `Block<Tag, MemoryKind>` es una reserva con tamaño (`typed.hpp:200`).

## `Span` — `span.hpp`

`Span<T>` es una vista **sin dueño** sobre un rango contiguo (`span.hpp:56`): puntero + tamaño que viajan juntos. Reemplaza el par `(T*, n)` que se desincroniza; `fill_zeros(span)` lo pone a cero (`span.hpp:167`).

## `Box` — `box.hpp`

Rectángulo entero (`x, y, w, h`, `box.hpp:22`) con helpers `overlaps`, `intersect`, `merge`, `translate` (`box.hpp:54`).

## Memoria y punteros — `memory_kind.hpp`, `ptr.hpp`

`MemoryKind` (`Chip`/`Slow`/`Fast`/`Any`, `memory_kind.hpp:23`) distingue los bancos; `Address<K>` es un puntero **tipado por el banco** (`memory_kind.hpp:38`), de modo que pasar un puntero Fast donde se espera Chip es un error de tipo. `Ref<T>` es una referencia no propietaria anulable y `NonNull<T>` una que no puede ser nula (`ptr.hpp:28`, `ptr.hpp:66`).

> `Address<Chip>` es el único sitio donde el engine admite un puntero crudo, y solo tras certificarlo (`Address<Chip>::from_storage`). Ver `docs/engine/architecture/MEMORY_OWNERSHIP.md`.

Volver al [índice de `core/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
