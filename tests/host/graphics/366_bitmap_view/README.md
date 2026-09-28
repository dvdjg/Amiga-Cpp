# HOST-366 — bitmap_view

Test host de `eng/graphics/bitmap_view.hpp`: **zona rectangular de memoria gráfica** (planos +
geometría + layout, con la **memoria etiquetada** tag+banco).

- `BitmapView<Tag, Bank>` es **no propietaria** (la memoria la posee un `gfx::Bitmap`/`Block`).
  Sustituye al `BobTarget` (`u8*` suelto) añadiendo la **procedencia en el tipo**.
- El **banco en el tipo** hace que una zona Chip y una Fast sean **tipos distintos** (no se
  confunden) → elige el camino (Blitter DMA vs C2P por CPU) según el tag.

Valida `valid`, `words_per_row`, `bitmap_row_bytes`, `plane_stride`, `interleaved` y `byte_count`
en interleaved y en separado.

Diseño: `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §7.2.
