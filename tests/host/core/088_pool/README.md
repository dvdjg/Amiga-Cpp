# HOST-088 — Pool con handles generacionales

Respalda `engine/include/eng/core/util/pool.hpp`.

## Qué cubre

- **Alta/baja `O(1)`** sin heap: `add()` ocupa un slot (reiniciándolo a `T{}`) y
  `remove(handle)` lo libera.
- **Handles generacionales**: al reciclar un slot sube su generación, así que el handle
  antiguo queda inválido y **no puede "resucitar"**; se comprueba reutilizando un slot.
- `get`/`valid`/`used`/`at`/`handle_at`, `size`/`empty`/`full`/`capacity`, `reset`
  (sube la generación de todos los slots) y `for_each` sobre los vivos.

## Uso previsto

Formaliza el patrón que el engine repetía (`ActorStore`, `BackgroundQueue`): un parque
de objetos accesibles por `Handle` estable y reutilizable.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/088_pool
```
