# HOST-347 — split vertical del anillo (`ring_split`) — F7.3

Respalda `engine/include/eng/scene/scroll_plan.hpp`: `ring_split(scroll_y, visible_h, window_h)`
describe cómo se muestra la vista sobre un **anillo** de `window_h` filas cuando cruza su final
(`start_row`, `top_rows`, `wrap`). Es lo que el driver **XYUnlimited/CopperSplit** materializa
reapuntando `BPLxPT` en la línea del split (2.ª parte desde la fila 0 del anillo).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/347_ring_split
```
