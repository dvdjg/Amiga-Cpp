# HOST-277: W0 — generación procedural determinista (topología) + W4 (solvencia)

Verifica `eng::sim::gen` (`engine/include/eng/sim/gen/world_gen.hpp`): el **núcleo determinista**
de la generación procedural de mundos metroidvania
(`docs/guides/roadmap/ROADMAP_PROCEDURAL_WORLDS.md`, W0/W4).

## Qué comprueba

1. **Determinismo**: misma semilla → mismo grafo (firma por recorrido de vecinos, repetible);
   semillas distintas → grafos distintos.
2. **Cotas**: se generan `rooms` salas (dentro de `MaxRooms`); `rooms > MaxRooms` se rechaza.
3. **Conectividad**: el grafo es **conexo** desde el spawn (BFS alcanza todas las salas).
4. **Bucles**: hay aristas extra (atajos) → `edges > nodos-1` (un metroidvania no es un árbol).
5. **Bioma/peligro**: bioma válido por sala; peligro 0..100; spawn con peligro 0.
6. **Semilla por sala** distinta (para la geometría de W6).
7. **Solvencia (W4)**: `is_solvable` — mundo generado soluble; grafo de 2 salas conectadas soluble;
   **nodo aislado → insoluble** (red de seguridad de la solvencia).

## Notas

- Todo es **puro y sin heap** (arrays de capacidad fija + `eng::util::Graph` + `eng::Xoroshiro64pp`);
  `constexpr`-able. Reutiliza `eng::sim::BiomeKind` (no crea tipos de bioma propios).
- La decisión de **tamaño de sala fijo** (`kRoomW`/`kRoomH` + macro-celdas) está en el header.
- El gating con llaves (W3) y la geometría (W6) se construyen encima sin cambiar este contrato.

## Salida de referencia

```
OK: W0 (generacion procedural determinista de topologia) validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/277_proc_world
```
