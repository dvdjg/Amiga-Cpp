# HOST-325 — presupuesto de memoria (`eng::res::Budget`)

Respalda `engine/include/eng/res/budget.hpp`: la vista de solo lectura sobre las arenas del
engine (`MemorySystem`) que permite decidir si un recurso **cabe antes de pedirlo**. Cubre:

- `used_chip`/`remaining_chip`/`capacity_chip` y sus pares Slow/Frame, coherentes con la
  arena real tras una reserva;
- `can_fit(bytes, kind)` / `can_fit_chip` / `can_fit_slow` (incluido el caso «cabe justo»);
- el mapeo `Fast → arena Slow` (el engine no separa una arena Fast propia);
- `memory()` para reservar o tomar un `snapshot()` de telemetría;
- un `Budget` sin memoria ligada no es válido.

Es la mitad de `app.resources()` (`PUBLIC_GAME_API.md` §2.1.4); la caché de assets y
`res::load<T>` se construyen encima (`RESOURCE_SYSTEM.md`).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/325_budget
```
