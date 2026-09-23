# HOST-119: conjuntos disjuntos (Union-Find / DSU)

Test host de `engine/include/eng/core/util/union_find.hpp`:
`eng::util::UnionFind<MaxElements>`, conjuntos disjuntos con compresión de caminos y
unión por tamaño (`find`/`unite` casi `O(1)`). Dos arrays de `u16`, sin heap.

## Qué comprueba

1. Cada elemento arranca en su conjunto; `find`, `connected` y `components`.
2. `unite` fusiona, no repite (devuelve `false`) y mantiene el tamaño de componente.
3. Compresión de caminos: consultas repetidas dan la misma raíz.
4. `reset`.
5. Caso de uso: **islas** de celdas transitables en una tira (componentes conexas).

## Salida de referencia

```
UnionFind:
OK: UnionFind (unir, conectividad, tamanos, islas)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/119_union_find
```
