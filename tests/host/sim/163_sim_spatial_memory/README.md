# HOST-163: memoria espacial y rutas macro (`eng::sim`)

Test host del **mapa mental** y su uso en la navegación de gran escala.

## Qué comprueba

1. **`remember_place`/`best_known_room`/`place_confidence`**: recordar regiones (refugio,
   peligro, comida) con confianza y recuperar la mejor por encima de un umbral.
2. **`route_room`/`route_first_step`**: BFS sobre el grafo de regiones para el pathfinding
   **macro** de la simulación abstracta (la ruta fina la hace `eng::util`/`eng::ai`).
3. **`preferred_refuge`**: manda el refugio recordado (mapa mental) y, si no hay, la
   guarida.
4. **Migración off-screen** hacia el refugio recordado bajo clima adverso, y
   **consolidación** de un refugio percibido (`Den`) en memoria de largo plazo que pasa a
   ser el refugio preferido.

## Salida de referencia

```
Sim spatial memory:
OK: Sim spatial memory (lugares, rutas macro, refugio recordado)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/163_sim_spatial_memory
```
