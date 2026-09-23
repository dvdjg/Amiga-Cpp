# HOST-164: mapa mental en el movimiento (`eng::sim`)

Test host de `engine/include/eng/sim/mental_map.hpp` y del olvido de lugares: la memoria
espacial deja de ser datos y **guía por dónde se mueve** la criatura.

## Qué comprueba

1. **`place_bias`**: un refugio o una fuente de comida recordados dan sesgo negativo
   (atraen); un peligro recordado, positivo (disuade); lo desconocido es neutro.
2. **`MentalOverlay<W,H>`**: capa de coste adicional por celda para `eng::util::astar`;
   combinada con el `TerrainMap`, abarata la ruta hacia el refugio y encarece la que cruza
   peligro (nunca por debajo de 1).
3. **`deposit_mental_danger`**: marca el peligro recordado en un `eng::ai::InfluenceMap`
   para que el steering o el flow field lo eviten.
4. **`decay_places`/`forget_place`**: el mapa mental se desactualiza — solo decaen los
   **lugares**, no las creencias sobre individuos — y puede olvidarse un lugar concreto.

## Salida de referencia

```
Sim mental map:
OK: Sim mental map (sesgo, overlay para astar, influencia, olvido)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/164_sim_mental_map
```
