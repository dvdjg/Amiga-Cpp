# HOST-166: mapa mental en el mundo (ruta fina y flujo) (`eng::sim`)

Test host de los helpers de `SimWorld` que exponen el **mapa mental** de una criatura para
el movimiento fino, sin que el juego conozca los detalles.

## Qué comprueba

1. **`mental_bias`**: sesgo por región según lo recordado (refugio/comida atraen, peligro
   disuade; lo no recordado es neutro).
2. **`stamp_mental_overlay`**: construye el `MentalOverlay` de la criatura y su coste se
   combina con el `TerrainMap` para `eng::util::astar`.
3. **`stamp_mental_danger`**: marca el peligro recordado en un `eng::ai::InfluenceMap`.
4. **Ruta fina**: `astar` sigue encontrando paso pero penalizando el peligro recordado.

## Salida de referencia

```
Sim world mental routes:
OK: Sim world mental routes (sesgo, overlay para astar, influencia)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/166_sim_world_mental_routes
```
