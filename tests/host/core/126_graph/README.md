# HOST-126: grafo genérico

Test host de `engine/include/eng/core/util/graph.hpp`: `Graph<MaxNodes, MaxEdges>` con
listas de adyacencia de capacidad fija (sin heap), y sus algoritmos `graph_bfs`,
`graph_astar` (heurística del llamador) y `topological_sort` (Kahn). Es la pieza que
consolida el grafo de waypoints (`ai::navigation::WaypointGraph` se apoya en ella).

## Qué comprueba

1. Adyacencia: nodos, aristas bidireccionales y `for_each_neighbor`.
2. `graph_bfs` con reconstrucción de camino.
3. `graph_astar` (Dijkstra) elige la ruta de **coste mínimo** (barata frente a cara).
4. `topological_sort` de un DAG (orden válido) y de un grafo con **ciclo** (no cubre todo).
5. Capacidad: `add_node`/`add_edge` devuelven `no_node`/`false` al llenarse.

## Salida de referencia

```
Graph:
OK: Graph (adyacencia, BFS, A*, orden topologico, capacidad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/126_graph
```
