# Demo 062 — fachada de sprites de nivel A (`eng::SpriteScene`)

Tutorial: el juego **da de alta actores** (`ActorDesc`) y llama `emit`; el engine **compone**
(HW sprites + `SpriteAllocator` + BOB fallback) **sin** que el juego declare los buffers de trabajo ni
un `FramePlan`. Es la fachada del sistema de objetos NES (8 sprites/línea + overflow).

```cpp
eng::SpriteScene<9> sprites;
sprites.set_budget({8u, 4096u, 0u});              // 8 canales HW, 4k palabras de BOB, 0 capas
sprites.add(d);                                   // d = ActorDesc (visual + posicion + prioridad)
auto r = sprites.emit(plan, ctx);                 // compone; los BOB van al plan
m_manager.apply(sprites.placements().data(), r.sprites);  // HW sprites a los registros (copper)
```

Muestra 9 actores de 16×16 en fila (y=100): los **8 primeros caben** en los canales 0..7 (parejas de
color: rojo, verde, azul, amarillo); el **noveno no cabe** y queda `as_bob` (contado en
`result.degraded`, publicado en `g_eng_run_status.detail`). Por debajo trabaja `scene::compose_sprites`
(+ `SpriteAllocator` + BOB); la demo de técnica equivalente (nivel B) es
[054_sprite_allocator](../../../../techniques/amiga/sprites/054_sprite_allocator/README.md).

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/features/engine/amiga/062_sprite_scene --release
bash ./tools/run/run-demo.sh demos/features/engine/amiga/062_sprite_scene
```
