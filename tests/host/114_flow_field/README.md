# HOST-114: campo de flujo

Test host de `engine/include/eng/ai/navigation/flow_field.hpp`: un Dijkstra multi-fuente
desde los objetivos rellena, para cada celda de una rejilla `W×H`, el coste al objetivo
más cercano y la dirección hacia él. Muchos agentes siguen el campo con una lectura por
celda, reutilizando la malla de 4 vecinos de `eng::util::pathfinding`.

## Qué comprueba

1. **Coste uniforme**: el coste al objetivo es la distancia Manhattan y el agente llega
   en el mínimo de pasos.
2. **Terreno con coste**: un muro (`0xffff` = bloqueado) obliga a rodearlo y la ruta no
   pisa celdas bloqueadas.
3. **Región inalcanzable**: `integration = 0xffff` y sin dirección.
4. **Sin objetivos**: `compute_flow_field` devuelve `false`.

## Salida de referencia

```
FlowField:
OK: FlowField (camino minimo, muro, inalcanzable, sin objetivos)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/114_flow_field
```
