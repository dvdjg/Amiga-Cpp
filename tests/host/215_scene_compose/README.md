# HOST-215: `scene::compose` (escena por etapas)

Test host del prototipo de **composición de escenas** (`eng/graphics/scene/compose.hpp`):
una escena planar se construye uniendo **etapas** (recursos + emisión de Copper + handles),
en vez de una clase por driver. Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.

## Qué comprueba

1. `compose(scene, memory, recursos, etapas...)` inicializa la escena y ejecuta las etapas.
2. Las etapas de `display`/`palette` emiten BPLCON0/DIW/DDF/punteros y la paleta.
3. Una etapa propia (`lambda`) emite un `PatchHandle`; `set()` lo parchea por frame.
4. La escena queda `ok()` con bitplanes y copperlist válidos.

## Salida de referencia

```
OK: scene::compose (escena por etapas + PatchHandle + ciclo de vida).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/215_scene_compose
```
