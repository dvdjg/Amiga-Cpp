# HOST-165: clima con frentes (`eng::sim`)

Test host de la **propagación del clima** entre regiones vecinas
(`SimWorld::diffuse_climate`): una tormenta avanza por el mapa en vez de estar congelada
por región.

## Qué comprueba

1. **Propagación**: el peligro salta una fracción `spread` (%) a las regiones
   **adyacentes**, y solo a esas.
2. **Avance y refuerzo**: repetido, el frente recorre el grafo y el origen se refuerza
   desde el vecino; se conserva el `HazardKind`.
3. **Regiones aisladas**: el frente no salta a una región sin conexión.
4. **Disipación**: `decay` lo apaga y limpia el tipo cuando llega a 0; `set_hazard` sigue
   fijando un peligro global (todas las regiones).

## Salida de referencia

```
Sim climate fronts:
OK: Sim climate fronts (propagacion, avance, disipacion, global)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/165_sim_climate_fronts
```
