# HOST-492: capa PF (no actor)

Valida que una capa de playfield (fondo/enemigo gigante) se modela como **región** (`Layer`/`ScenePlan` + `PlayfieldLayerPlan`), no como un actor con identidad.

## Qué cubre

- `plan_playfield_layer`: DPF activo, PF2 explícito, `Any`, PF ocupado, sin DPF, límite de planos y altura completa.
- `PlayfieldLayer::draw_cpu`: contenido de capa copiado por CPU en el bitmap PF (sin `ActorStore`).
- La capa PF vive en `World`/`Layer` y no consume `ActorStore`.
- `ScenePlan` sigue eligiendo `Dpf` para BG + FG en PF2.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/scene/492_playfield_layer
```
