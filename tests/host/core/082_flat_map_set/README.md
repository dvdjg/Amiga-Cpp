# HOST-082 — FlatMap y FlatSet

Respalda `engine/include/eng/core/util/flat_map.hpp` y
`engine/include/eng/core/util/flat_set.hpp`.

## Qué cubre

- **`FlatMap<K, V, N>`**: entradas ordenadas por clave, `find`/`contains` por búsqueda
  binaria, `insert` (rechaza duplicado y lleno), `insert_or_assign`, `erase`, recorrido
  por orden de clave y rechazo controlado al llenarse.
- **`FlatSet<T, N>`**: conjunto ordenado con `insert` (rechaza duplicado), `contains`,
  `erase` y orden garantizado.

## Cuándo usarlos

Cuando la cardinalidad es pequeña (≤ ~32): memoria exacta, iteración contigua y en
orden, sin función hash y determinista. Para cardinalidades mayores, `HashMap`/
`HashSet`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/082_flat_map_set
```
