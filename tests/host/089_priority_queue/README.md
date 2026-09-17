# HOST-089 — PriorityQueue

Respalda `engine/include/eng/core/util/priority_queue.hpp`.

## Qué cubre

- **Max-heap** por defecto (`Less<T>`) y **min-heap** con `Greater<T>`: `top()` O(1),
  `push`/`pop` O(log N); se comprueba el orden de extracción (no creciente/decreciente).
- **Capacidad fija**: `push`/`emplace` devuelven `false` al estar llena (no desbordan).
- **Comparador propio**: cola de `Task` ordenada por `prio`; `emplace`, `clear`, `top`.

## Uso previsto

Scheduling, IA/A* y colas de eventos donde importa el orden de prioridad A500 sin
asignar memoria (max-heap sobre almacenamiento inline).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/089_priority_queue
```
