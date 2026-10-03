# HOST-245: compositor del scroller de tiras

Prueba `eng/field/strip_composer.hpp` (`StripComposer<Geom>`): emite la copperlist **una vez** con
handles (`move_at`) de las words de `BPLxPT`/`BPLCON1`/split, y por frame **parchea** solo los valores
de `strip_copper_values` (fine scroll, punteros de la ventana y split two-WAIT), **sin re-emitir**.

## Qué comprueba

1. `build()` emite en ambos bloques del doble buffer y `ok()`.
2. `patch()` escribe `BPLCON1` (fine) y, por plano, la dirección de `BPLxPT`
   (`base + p*ring_w_bytes + window_word*2`), en el bloque activo.
3. El **split** con `viewport_h = 256` (línea 300) se emite como **two-WAIT** (cruza la 255); con
   `viewport_h ≤ 208` cabría en un solo `WAIT`.
