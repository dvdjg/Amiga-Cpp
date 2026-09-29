# HOST-335 — descriptor único de objeto (`Sprite` ↔ `Visual`/`ActorDesc`)

Respalda la convergencia del **sistema de objetos** (F4a): un `Sprite` (BOB cocinado, camino
`Screen::sprite`) expone su **vista de dominio** `Visual` y se convierte en `ActorDesc` (camino
`World::add_actor`) con `scene::actor_desc_from_sprite`. Así ambas vías usan **el mismo asset** y
no hay dos descriptores distintos. Corrige:

- `Sprite` declara `sheet_bytes`/`mask_bytes` (necesarios para dimensionar los `Span` de `Visual`);
- `Visual` transporta `frame_count`/`frame_stride` (antes solo `bob_from_visual` los fijaba a 1/0);
- `Sprite::visual()` y `actor_desc_from_sprite(...)` unen los dos caminos.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/335_sprite_actor
```

Gate visual: la demo `214_app_sprite` usa el mismo `Sprite` para `screen.sprite(...)` y para
`world.add_actor(actor_desc_from_sprite(...))`.

Ver `docs/engine/architecture/OBJECT_SYSTEM.md` §15 y `ROADMAP_API_COHERENCE.md` (F4).
