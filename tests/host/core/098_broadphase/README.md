# HOST-098 — broadphase (rejilla espacial)

Respalda `engine/include/eng/core/util/broadphase.hpp`.

## Qué cubre

- `SpatialHash<CellSize, CellsX, CellsY, MaxItems>`: rejilla uniforme donde cada celda
  es una lista intrusiva de entradas de un `Pool`.
- `clear()` + `insert(id, x, y)` (reconstrucción por frame, `O(1)` por inserción).
- `query(box, out)` / `for_each_in(box, fn)` recorren solo las celdas que caen en la
  caja y filtran por `point_in_aabb`.
- Rechazo controlado si el punto está fuera de la rejilla o el pool está lleno.

## Uso previsto

Fase amplia de colisiones (evitar todos-contra-todos) en demos con muchos objetos;
reutiliza `Pool` + `IntrusiveSList` (sin heap).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/098_broadphase
```
