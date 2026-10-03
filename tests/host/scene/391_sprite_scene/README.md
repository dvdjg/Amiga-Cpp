# HOST-391 — fachada de sprites de nivel A (`eng::SpriteScene`)

Valida `eng/api/sprites.hpp`: el juego da de alta **actores** (`ActorDesc`) y llama `emit`; el engine
**compone** (HW sprites + `SpriteAllocator` + **BOB fallback**) sin que el juego declare los buffers de
trabajo ni un `FramePlan`. Es la fachada del sistema de objetos NES (8 sprites/línea + overflow): los
actores que no caben como sprite hardware se **degradan** (`result.degraded`).

Cubre: alta de un actor (`add` + `count`), rechazo de una descripción sin contenido, `emit` con un
presupuesto (2 canales HW) → 1 actor materializado, `placements()` coherente con `result().sprites`, y
`remove`.

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/scene/391_sprite_scene
```
