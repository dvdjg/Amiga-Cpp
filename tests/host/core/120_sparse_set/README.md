# HOST-120: SparseSet (conjunto disperso-denso)

Test host de `engine/include/eng/core/util/sparse_set.hpp`:
`eng::util::SparseSet<T, MaxElements>`, con elementos identificados por un índice `u16`.
Altas, bajas y consultas son `O(1)` y los elementos viven **contiguos**, de modo que
iterarlos es cache-friendly. Es la pieza típica de un ECS.

## Qué comprueba

1. `insert`/`find`/`contains`/`size` y alta duplicada (devuelve `false`).
2. Iteración densa (`ids()`/`values()` en orden contiguo).
3. `erase` con **swap-remove**: el último ocupa el hueco y el resto conserva sus valores.
4. `insert_or_assign`, capacidad, id fuera de rango y `clear`.

## Salida de referencia

```
SparseSet:
OK: SparseSet (insert/find, iteracion densa, swap-remove, capacidad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/120_sparse_set
```
