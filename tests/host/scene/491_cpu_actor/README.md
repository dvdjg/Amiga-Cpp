# HOST-491: actor CPU (A500)

Valida la materialización **CPU** de actores: `ActorDesc::force_cpu`, la copia planar por palabras de 16 bits y la integración con `SpriteDirector` (`SpritePolicy::Cpu`).

## Qué cubre

- `ActorStore::actor_emit` dibuja en el `BobTarget` de `surface` sin encolar `BlitJob`.
- X no alineada a 16 con la palabra de guarda de la hoja.
- `ClearRect` por CPU (borra la caja previa).
- `CookieCut` con máscara separable.
- `SpriteLoadReport`: `cpu`, `parts` y `emitter_cpu`; sin canales HW ni Blitter.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/scene/491_cpu_actor
```

La validación visual de la ruta de demo se hace con la demo 225 (entidad CPU añadida).
