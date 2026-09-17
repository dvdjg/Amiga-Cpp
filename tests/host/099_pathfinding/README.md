# HOST-099 — búsqueda de caminos

Respalda `engine/include/eng/core/util/pathfinding.hpp`.

## Qué cubre

- **`bfs<W,H>`** (sin peso, 4-vecinos) sobre una malla con `walkable(idx)`; rellena
  `came_from` (índice anterior, `-1` = no visitado).
- **`astar<W,H>`** con coste `cost(from,to)` y heurística Manhattan (admisible para
  coste ≥ 1), sobre `PriorityQueue` de `W·H`.
- **`reconstruct_path<W,H>`**: camino de `start` a `goal` (o `0` si no hay).
- Test: BFS rodea un muro, A* con coste unitario da un camino válido, y un muro
  completo bloquea (BFS devuelve `false`).

## Límites

`W` potencia de dos (índices por máscara); la rejilla debe caber en `s16` (`W·H ≤
32767`). El `scratch` (`came_from`, `queue`, `g_score`, `closed`, `out`) lo aporta el
llamador; sin heap.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/099_pathfinding
```
