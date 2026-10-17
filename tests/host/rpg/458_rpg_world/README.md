# HOST-458: entidades de mundo y objetos en el suelo (`eng::rpg`)

Test host de `engine/include/eng/rpg/world.hpp` (hito M1 de la capa RPG): la composición mínima de un mundo (entidades por `kind` + verbos + índice de componente) y la lista de objetos con posición que une las instancias del `ItemStore` con el mapa.

## Qué comprueba

1. **`EntityStore`**: alta con tipo/verbos/posición, búsqueda por `EntityId`, baja, **reciclado del hueco con id nuevo** (un id viejo no resucita a otra entidad), capacidad con rechazo (`no_entity`), mutación vía `Ref` y recorrido.
2. **Capacidades**: `interaction` (máscara de verbos) y `entity_flags` como datos de la entidad; el motor no ramifica por tipo concreto.
3. **`GroundItems`**: colocar, extraer (compactando), lleno, `room` por defecto y limpieza sin destruir instancias (el juego decide el destino de los objetos).

## Salida de referencia

```
RPG world (entidades, suelo):
OK: RPG world (EntityStore/GroundItems)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/458_rpg_world
```
