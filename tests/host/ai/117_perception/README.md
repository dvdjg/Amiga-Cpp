# HOST-117: percepción (mapa de influencia + memoria del agente)

Test host de:
- `engine/include/eng/ai/perception/influence_map.hpp`: `InfluenceMap<W,H>`, rejilla de
  influencia (`s32` por celda) con `deposit`/`decay`/`at`/`strongest`.
- `engine/include/eng/ai/perception/agent_memory.hpp`: `AgentMemory`, la última posición
  vista del objetivo y cuántos ticks hace (`see`/`tick`/`fresh`/`stale`/`forget`).

## Qué comprueba

1. **InfluenceMap**: acumular depósitos, degradar sin bajar de 0 y encontrar la celda con
   más influencia (o `no_cell` si todo está a 0).
2. **AgentMemory**: guardar el avistamiento, contar ticks, decidir frescura/caducidad por
   umbral y olvidar.

## Salida de referencia

```
Perception:
OK: Perception (influence map, agent memory)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/117_perception
```
