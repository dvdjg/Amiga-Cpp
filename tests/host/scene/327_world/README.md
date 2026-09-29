# HOST-327 — mundo retenido (`eng::scene::World` / `Layer`)

Respalda `engine/include/eng/scene/world.hpp`: el contenedor aditivo de capas con su cámara
(primer escalón de `SCENE_AND_RESOURCES.md`). Cubre:

- `add_layer` y su **rechazo anulable** al llenarse (`Ref<Layer>` inválido, sin fallo
  silencioso);
- `count`/`capacity`/`full`;
- `layer(i)` por índice (fuera de rango → `nullptr`) y `find(id)` (inexistente → `nullptr`);
- `id()`/`depth()` de la capa y su **cámara**: `scroll_x`/`set_scroll_x` con recorte al mundo.

El planner que materializa las capas (playfield/tilemap/efecto) se construye encima; la demo
`214_app_sprite` usa `app.world()` para mover el sprite según el scroll de la capa.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/327_world
```
