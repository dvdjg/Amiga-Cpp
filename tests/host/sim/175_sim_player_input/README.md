# HOST-175: entrada humana del avatar (`eng::sim`)

Test host de `player_control` de `engine/include/eng/sim/avatar.hpp`: la misma capa que mueve
al jugador con IA acepta **entrada humana** sin tocar el mundo.

## Qué comprueba

1. **Movimiento** por `PlayerInput` (dx/dy) y **recorte** a los límites del mundo; un id
   inexistente no hace nada.
2. **Interacción** (comer/usar) solo donde hay recurso (`biome_food >= 40`); el **descanso**
   recupera energía.
3. **Paridad IA/humano**: `player_step` (IA) y `player_control` (humano) mueven al mismo
   avatar por caminos distintos y el mundo queda coherente.

## Salida de referencia

```
Sim player input:
OK: Sim player input (movimiento, interaccion, paridad IA/humano)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/175_sim_player_input
```
