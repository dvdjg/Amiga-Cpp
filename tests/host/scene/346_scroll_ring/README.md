# HOST-346 — geometría del anillo de scroll (`scroll_ring`) — F7.3

Respalda `engine/include/eng/scene/scroll_plan.hpp`: la **geometría del anillo de scroll** y las
**bandas cruzadas**, base de los drivers de scroll con guardas (`CopperRing`/`CopperSplit`/
`BlitterColumns`):

- `scroll_ring(visible_w, visible_h, planes, margin_x, margin_y)` → ventana = visible + `2·margen`,
  `row_bytes` y bytes totales;
- `ring_crossed(from, to, tile_size)` → columnas/filas de tile cruzadas al desplazar (valor
  absoluto; 0 si no se cruza tile).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/346_scroll_ring
```
