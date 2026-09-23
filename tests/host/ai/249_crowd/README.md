# HOST-249: crowd generico con vecinos por rejilla espacial (`eng::ai::Crowd`)

Test host del orquestador de **movimiento local** (`engine/include/eng/ai/steering/crowd.hpp`):
actualiza un pool de agentes con separación, evasión de obstáculos e integración, buscando los
vecinos con una **fase amplia** (política) en vez de comparar todos contra todos. La cabecera es
**genérica sobre el escalar** (`Crowd<S, Broadphase>`), y el test la ejercita con **`s32` y
`float`**.

## Que comprueba

1. **Ahorro frente a `N²`**: 64 agentes repartidos en rejilla hacen muchas menos comprobaciones que
   los 64×63 pares de la fuerza bruta (`neighbor_checks()` lo expone).
2. **Separación**: dos agentes solapados se alejan con las actualizaciones.
3. **Misma conducta con otra fase amplia**: `SpatialHashBroadphase` y `BruteForceBroadphase` dan el
   mismo resultado (el algoritmo es independiente de la política de vecinos).
4. **Sin interacción a distancia**: dos agentes lejanos y sin deseo no se mueven.
5. **Capas**: agentes en capas distintas no se repelen.
6. **Inactivos**: los agentes con `flags` a 0 no se mueven ni participan.
7. **Límite de velocidad**: con deseo grande, la velocidad se acota a `max_speed`.
8. **Obstáculos**: un obstáculo estático cercano empuja al agente en contra.
9. **Genericidad**: el mismo algoritmo con `float` separa a los agentes.

## Salida de referencia

```
OK: crowd generico (rejilla, separacion, limites, float) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ai/249_crowd
```
