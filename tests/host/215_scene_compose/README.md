# HOST-215: `scene::compose` (escena por etapas)

Test host del prototipo de **composición de escenas** (`eng/graphics/scene/compose.hpp`):
una escena planar se construye uniendo **etapas** (recursos + emisión de Copper + handles),
en vez de una clase por driver. Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.

## Qué comprueba

1. `compose(scene, memory, recursos, etapas...)` inicia la escena y ejecuta las etapas.
2. Las etapas de `display`/`palette` emiten BPLCON0/DIW/DDF/punteros y la paleta.
3. Una etapa propia (`lambda`) emite un `PatchHandle`; `set()` lo parchea por frame.
4. Ciclo de vida: una `Task` (`eng::util::FunctionRef<void()>`) ligada con `on_frame` corre una vez por `tick()`.
5. Etapas HAM/EHB: `row_repeat` (cuadruplicado) + `palette_zones` (zonas de paleta por raster) y `reverse_ptrs` (BPLxPT inversos) componen.
6. Preset `canvas` (layout interleaved): expone `surface()` y `fill_polygon` pinta sobre ella.
7. `intents`: una lista de `CopperIntent` se registra en el `Plan` (ordenada por scanline).
8. Geometría/BPLCON0 por constantes (`kPal320x256`, `kBplcon0_*`) + `display(geometry, bplcon0)`. La escena queda `ok()` con bitplanes y copperlist válidos.
9. **Huella estática** de las etapas (`display_words`/`palette_words`/`palette_zone_words`/`row_repeat_words`): `static_assert` de sus valores y contraste con `Scene::words()` real (display+palette+patch y display+zonas+row_repeat).

## Salida de referencia

```
OK: scene::compose (etapas display/paleta/zonas/row_repeat + PatchHandle + ciclo de vida).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/215_scene_compose
```
