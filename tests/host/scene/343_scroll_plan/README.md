# HOST-343 — planificador de scroll adaptativo (`eng::scene::scroll_plan`) — F7.3

Respalda `engine/include/eng/scene/scroll_plan.hpp`: elige el **scroll efectivo** de una capa
degradando por presupuesto y **estima la memoria** de la ventana. Implementa el acuerdo del
consumidor NES ([NES_CONSUMER.md](../../../docs/engine/NES_CONSUMER.md) §6): «la capa pide
(`ScrollKind`), el planner dispone».

- `scroll_copper_per_line(kind)`: coste de Copper por línea (`CopperSplit`=4, `CopperRing`=2).
- `choose_scroll(requested, copper_per_line)`: degrada `CopperSplit → CopperRing → Fine → None`;
  `BlitterColumns` no se degrada.
- `scroll_memory(kind, visible_w, visible_h, planes, speed_px)`: ventana = visible + guardas
  (`CopperRing`/`BlitterColumns`: `2·(speed+8)`; `CopperSplit`: ring +16) y bytes por plano.
- `choose_scroll_fitting(...)`: elige la mejor técnica que quepa en Copper **y** Chip.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/343_scroll_plan
```
