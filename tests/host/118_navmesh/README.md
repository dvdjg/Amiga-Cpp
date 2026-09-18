# HOST-118: navmesh lite

Test host de `engine/include/eng/ai/navigation/navmesh_lite.hpp`: versión mínima de la
idea de Recast/Detour para el A500. El mundo transitable se divide en **polígonos
convexos**; `NavMesh` localiza en qué polígono cae un punto, hace A* sobre la
**adyacencia por portales** y devuelve los **puntos medios de los portales** más el
destino. Sin heap y sin división por valores de runtime (las anclas son el primer
vértice del polígono, no el centroide).

## Qué comprueba

1. **Localizar**: punto-en-polígono convexo y punto fuera de la malla (`no_poly`).
2. **Camino por portales**: de un polígono a otro devuelve el punto medio del portal y
   el destino.
3. **Tres polígonos encadenados**: los portales intermedios y el destino, en orden.
4. **Dentro de un polígono**: solo el destino; **origen fuera**: sin camino.

## Salida de referencia

```
NavMeshLite:
OK: NavMeshLite (localizar, portales, uno/dos/tres poligonos)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/118_navmesh
```
