# Referencia — `scene` (`eng/api/scene.hpp`)

`eng/api/scene.hpp` es una **reexportación**: sube al espacio de nombres de juego (`eng::`) el
vocabulario del **planner de escena** (§7). El juego declara la escena con esto; los motores
(`StripScrollLayer`/`XlimitedScene`) quedan detrás.

## Tipos reexportados

| Símbolo | Qué es |
|---|---|
| `ScenePlan<MaxLayers>` | Lista de capas que el planner resuelve. `add(role, placement[, ScrollPlan, LayerContent])`, `count()`, `layer(i)`, `strategy()`. |
| `LayerPlan` | Una capa: `role`, `placement`, `content`, `scroll`. |
| `LayerRole` | `Background` / `Foreground` / `Overlay`. |
| `LayerPlacement` | `{top, height, field}`; `field` = 1/2 en un DPF. |
| `LayerContent` | `Scroll` (campo con tilemap) o `Canvas` (lienzo estático). |
| `SceneStrategy` | `Single` / `Dpf` / `Bands` (`choose_strategy`). |
| `BandSpan` | Tramo `{top, height, role}`. |

## Helpers reexportados

| Helper | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `apply_dpf_plan` | `bool apply_dpf_plan(XlimitedSceneConfig&, const ScenePlan&)` | config del motor + plan | `false` si el plan no es DPF. Siembra geometría/paleta + roles. |
| `plan_raster_layout` | `Expected<u8, RasterPlanError> plan_raster_layout(plan, Span<const PlayfieldHardwareView> views, RasterLayout& out)` | plan, una vista por capa, salida | nº de bandas o error. Deriva el `RasterLayout`. |
| `plan_bands` | `Expected<..., …> plan_bands(layers, rows, Span<BandSpan> out)` | capas, alto del display, salida | nº de tramos o error. Valida el layout de bandas. |
| `for_each_band_part` | `void for_each_band_part(Box, Span<const BandSpan>, Fn)` | rect, tramos, callable `(band, part)` | — (parte un rect por banda: dibujo *split-aware*). |

## Uso típico

```cpp
eng::ScenePlan<2> plan {};
plan.add(eng::LayerRole::Background, eng::LayerPlacement {}, bg_scroll);
plan.add(eng::LayerRole::Foreground, eng::LayerPlacement {0, 0, 2}, {}, eng::LayerContent::Canvas);
// … el App lo convierte en un RasterLayout y lo publica (app.present_layout)
```

> Fuente: `engine/include/eng/api/scene.hpp` (+ `eng/scene/{plan,dpf_plan,band_plan,raster_plan,banded_target}.hpp`).
> Contrato: `docs/engine/architecture/PUBLIC_GAME_API.md` §2.1.3.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
