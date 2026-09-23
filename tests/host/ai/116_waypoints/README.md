# HOST-116: grafo de waypoints con A*

Test host de `engine/include/eng/ai/navigation/waypoints.hpp`:
`eng::ai::WaypointGraph<MaxNodes, MaxEdges>` (nodos con posición y aristas
bidireccionales con coste) y A* sobre el grafo, con heurística Manhattan y `scratch`
del llamador (sin heap). Complementa al campo de flujo, que sirve para muchos agentes
con un mismo destino; aquí el destino cambia por agente.

## Qué comprueba

1. **Ruta óptima** en un grafo en diamante (elige la rama más barata) y coste mínimo.
2. **Inalcanzable**: nodo aislado → sin camino; camino trivial de un nodo.
3. **Capacidad**: `add_node` devuelve `no_node` al llenarse.

## Salida de referencia

```
Waypoints:
OK: WaypointGraph (ruta optima, inalcanzable, capacidad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/116_waypoints
```
