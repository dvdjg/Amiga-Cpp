# HOST-437 - `fast_bob_margins` (márgenes del FastCopy, F5)

Test host del cálculo de **márgenes del FastCopy** (`eng/graphics/fast_bob_margins.hpp`,
F5): a partir del **desplazamiento máximo por eje** que declara el asset, el helper da el
padding transparente y el tamaño final de la hoja que el pipeline cocina para el camino
`BobDraw::Opaque` + `FastBobLayer` (dual playfield fast BOBs, HOST-355) — la copia `$F0`
tapa por sí sola la posición anterior, sin `clear` ni máscara.

Cubre: sin movimiento (sin márgenes), 1 px/frame por lado, velocidades distintas por eje,
**doble buffer** (los cuatro márgenes se doblan) y uso `constexpr` en tablas de asset.

Es freestanding (sin hardware, sin heap, sin STL).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/437_fast_bob_margins   # solo este
bash tools/run-host-tests.sh                                           # todos
```

## Referencias

- `docs/reference/amiga/techniques/dual-playfield-fastbobs.md` (técnica).
- `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.4 (F5).
- `engine/include/eng/scene/bobs.hpp` (`FastBobLayer`, HOST-355).
