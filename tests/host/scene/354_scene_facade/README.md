# HOST-354 - scene_facade

Test de la fachada de escena (`eng/scene/display.hpp` y `eng/scene/bobs.hpp`).

## Qué cubre

- **Display declarativo** (`emit_display`): emite el display estándar (DMACON/BPLCONx/módulos/
  DIW/DDF y `BPLxPT`) sobre un `copper::Scheduler`. Se comprueba que `BPL1PT` apunta a la base
  Chip del plano 0 (los dos words high/low del MOVE de 32 bits).
- **Efectos**: `emit_fine_scroll` devuelve un handle válido; `emit_gradient` no rompe la lista.
- **Capa de BOBs** (`BobLayer`/`BobActor`): `resize`/`count` (con recorte a `kMaxActors`) y los
  actores (posición/frame/visibilidad).

## Cómo se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/354_scene_facade
```
