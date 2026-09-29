# HOST-345 — planner de región (`eng::scene::plan_region`) — F4c (decisión)

Respalda `engine/include/eng/scene/scroll_plan.hpp`: `plan_region(WorldRegion, visible_w,
visible_h, RegionBudget)` elige el **`ScrollKind` efectivo** degradando por presupuesto de Copper
y Chip, y devuelve `RegionPlan { scroll, cost(RegionCost), memory(ScrollMemory), ok }`.

- presupuesto holgado → respeta la petición (`CopperSplit`/xyunlimited);
- Copper ajustado → degrada a `CopperRing` (xlimited, guarda `speed+8`);
- Chip ajustado → degrada a `Fine` (ventana visible);
- sin presupuesto → `ok=false`, `scroll=None`;
- región que pide `None` (estática) → siempre `ok`.

Cierra el modelo «la capa pide, el planner dispone» a nivel de región; la **materialización**
(playfield/Copper) va encima.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/345_region_plan
```
