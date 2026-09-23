# HOST-118: navmesh lite

Test host de `engine/include/eng/ai/navigation/navmesh_lite.hpp`: versión mínima de la
idea de Recast/Detour para el A500. El mundo transitable se divide en **polígonos
convexos**; `NavMesh` localiza en qué polígono cae un punto, hace A* sobre la
**adyacencia por portales** y puede devolver:
- `find_path`: los **puntos medios de los portales** más el destino.
- `find_smooth_path`: el mismo camino tras **string-pulling** con el algoritmo del embudo
  (*simple stupid funnel*), es decir, solo las esquinas visibles (menos puntos, sin
  zigzag). Sin heap y sin división por valores de runtime (las anclas son el primer
  vértice del polígono, no el centroide).

## Qué comprueba

1. **Localizar**: punto-en-polígono convexo y punto fuera de la malla (`no_poly`).
2. **Camino por portales**: de un polígono a otro devuelve el punto medio del portal y
   el destino.
3. **Tres polígonos encadenados**: los portales intermedios y el destino, en orden.
4. **Dentro de un polígono**: solo el destino; **origen fuera**: sin camino.
5. **String-pulling**: un pasillo recto de tres polígonos se colapsa a inicio + destino,
   mientras `find_path` mantiene los dos puntos medios.

## Salida de referencia

```
NavMeshLite:
OK: NavMeshLite (localizar, portales, poligonos y string-pulling)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/118_navmesh
```
