# HOST-087 — listas intrusivas

Respalda `engine/include/eng/core/util/intrusive_list.hpp`.

## Qué cubre

- **`IntrusiveList<T>`** (doble enlace vía `IntrusiveLink<T>`): `push_front`/`push_back`/
  `pop_front`/`pop_back`, `erase` en `O(1)` con el puntero al nodo (limpia el enlace),
  `front`/`back`/`size`/`empty`, `contains`, `clear` e iteración en ambos sentidos.
- **`IntrusiveSList<T>`** (simple enlace vía `IntrusiveSLink<T>`): `push_front`/
  `pop_front`/`insert_after`/`erase_after`/`contains`/iteración, y el uso como
  **free-list** de un pool (reparto LIFO y devolución).

## Idea

El enlace vive **dentro** del objeto, así que insertar/borrar no asigna memoria y es
`O(1)`; la lista nunca posee los nodos. Es lo contrario de `std::list` (nodos con heap).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/087_intrusive_list
```
