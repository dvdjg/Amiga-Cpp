# Test HOST-002: vocabulario portátil de intenciones (Visual/CopperIntent/SpriteIntent/Effect)

Test unitario **host** que valida la nueva estructura de tipos del engine definida en
[`engine/include/eng/graphics/raster_intent.hpp`](../../../../engine/include/eng/graphics/raster_intent.hpp):
el vocabulario portable de intenciones de display del diseño integral
([`ENGINE_DESIGN.md`](../../../../docs/engine/architecture/ENGINE_DESIGN.md) §1 y
[`VISUAL_EFFECT_SPRITE_DESIGN.md`](../../../../docs/engine/architecture/VISUAL_EFFECT_SPRITE_DESIGN.md)).

| Tipo | Qué representa |
|---|---|
| `Visual` | Contenido dibujable de un objeto (Bob/HardwareSprite/Tile/FillRect), con datos via `Span`. |
| `CopperIntent` | Cambio de registros en una franja vertical de líneas raster (paleta/shift/split/sprite rearm/prioridad). |
| `SpriteIntent` | Asignación de un `Visual` de sprite a un canal hardware (para el `SpriteAllocator`). |
| `Effect<E,Plan>` (concept) | Productor de intenciones con estado temporal (`update()` + `apply_into(plan)`). |

Al ser tipos puros (sin hardware, sin STL), se validan en host de forma determinista:
`static_assert` de que son `trivially_copyable` (POD que viaja escena→scheduler), un
`static_assert` de que un efecto mínimo cumple el concept, y comprobaciones runtime de
que la geometría y el estado se conservan.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/002_raster_intent   # solo este
bash tools/run-host-tests.sh                                # todos
```

Salida: `OK: vocabulario de intenciones validado (...)` y código de salida 0.

## Relación con la ingesta de demoscene

Este vocabulario es la **base** sobre la que se asentará la Oleada 1 (`libgfx`) y las
siguientes de `demoscene-repo-orig`: en vez de importar `CopList*`/`Sprite*` como
objetos fugaces, se mapean a `CopperIntent`/`Visual`/`SpriteIntent` y los schedulers los
compilan a `FramePlan`. Ver `docs/demos/effects/CONTINUAR_INGESTA_DEMOSCENE.md`.
