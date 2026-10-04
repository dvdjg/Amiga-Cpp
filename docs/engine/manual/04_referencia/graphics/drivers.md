# Referencia — `graphics/drivers/`

Drivers de display por estrategia. El juego decide una **posición de cámara** por playfield; el driver la traduce a Copper.

## `TileScrollScene<Mode>` — `drivers/tile_scroll.hpp`

Driver **genérico** de scroll por tiles, independiente del modo gráfico: la misma lógica de scroll (copperlist, `BPLCON1`, punteros `BPLxPT`, módulos y prefetch de tiles por Blitter) se compila para *single* de 4/5/6 planos (6 = EHB) o *dual* 2+3 y 3+3, con scroll fino/coarse independiente por playfield. Es un template sobre el modo (`TileScrollScene<Mode>`) para que todos los límites (planos, bytes/plano, tamaño de tile, módulos) sean `constexpr` y verificables por demo.

Sustituye a `ehb_tile_scroll.hpp` (que se conserva como alias/caso particular).

Convenciones de hardware (AHRM 3.ª):

- `BPLCON1` es un *delay*: valores mayores desplazan el playfield a la derecha. En single, los dos nibbles son iguales; en dual, el bajo es PF1 y el alto PF2.
- En dual, los planos impares (1,3,5) forman PF1 y los pares (2,4,6) PF2; el color 0 de cada playfield es transparente. `BPL2PRI` (bit 6 de `BPLCON2`) decide cuál va delante.
- Con `DDFSTRT=$30` se fetcha un word extra a la izquierda: cada playfield apunta a su coarse `(scroll_x − 1) & ~15` y programa su fine `(16 − fine) & 15`; así `display_start == scroll_x` es continuo, sin salto al cruzar de tile (fine 15 → 0).

Para efectos tipo RoboCod (un bitplane con scroll propio), `TileScrollInput::plane[]` añade un offset coarse por bitplane a los `BPLxPT` sin cambiar la fórmula de los demás.

Tipos: `ScrollPosition2` (`tile_scroll.hpp:52`), `PlaneScrollOffset` (`:62`), `TilePageOrigin` (`:71`), `TileScrollInput` (`:81`), `TileScrollMode` (`:91`, `dual()`/`playfield_count()`), `TileScrollConfig` (`:137`), y el prefetch `HorizontalRingPrefetch` (`:151`, `reset`/`slot_for_world_column`/`next_right_prefetch`).

## Convenciones compartidas — `graphics/playfield_scroll.hpp`

`playfield_scroll.hpp` reúne las convenciones de scroll (fino/coarse, módulos, BPLCON1) compartidas por los drivers de display y el corkscrew, para que la fórmula viva en un solo sitio.

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
