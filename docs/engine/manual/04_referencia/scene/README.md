# Referencia — `scene/`

El módulo `eng::scene` describe **qué** quiere mostrar un juego (mundo, capas, actores, bandas) y **decide la estrategia** con la que el motor lo materializa. Es el puente entre el vocabulario de juego y los motores concretos (`field/`, Copper).

```
   intención del juego        vocabulario (scene/plan.hpp)      planner                 motor
   ──────────────────         ───────────────────────────       ───────                 ─────
   "fondo, HUD, parallax" ──► ScenePlan = [LayerPlan]  ──►  Single | Dpf | Bands  ──►  field/ · copper
                                                              └─ Unsupported → escape
```

## Grupos

| Grupo | Cabeceras |
|---|---|
| **Mundo** | `world.hpp` (`World`: contenedor **aditivo** de `Layer`, cada una con su `Camera2D` y profundidad; `add_layer` devuelve `Ref<Layer>` anulable), `layer.hpp` (`DrawLayer`), `actor.hpp`/`actor_store.hpp`/`actor_sprite.hpp`/`actor_types.hpp`, `world_tile_map.hpp`, `representation.hpp`. |
| **Planner** | `plan.hpp` (`LayerRole`, `LayerPlacement`, `LayerContent`, `LayerPlan`; `WorldLayerKind`/`ScrollKind`), `route_camera.hpp`, `scroll_plan.hpp` (planificador de scroll adaptativo). Estrategias: `dpf_plan.hpp` (`Dpf`), `band_plan.hpp` (`Bands`), `raster_plan.hpp` (convergencia planner ↔ `RasterLayout`). |
| **Escena declarativa** | `display.hpp`: pantalla como **bandas** (`Band`) materializadas en un `copper::Scheduler` —`emit_display`/`emit_palette`/`emit_gradient`/`emit_fine_scroll`— sin que el juego nombre registros. |
| **Capas de dibujo** | `bobs.hpp` (`BobLayer`: una hoja de sprites + N objetos), `banded_target.hpp` (dibujo **split-aware**: reparte un rect entre las bandas), `virtual_scene.hpp` (**no implementada**). |

## `ScenePlan` — `plan.hpp`

Una escena es una **lista de capas**; cada `LayerPlan` declara su **rol** (`Background`/`Foreground`/`Overlay`), su **colocación** (`LayerPlacement`: banda/campo) y su **scroll** (`playfield::ScrollPlan`). A partir de esa lista el **planner** elige **una de tres estrategias acotadas** (`Single`/`Dpf`/`Bands`) o **rechaza** (`Unsupported`) — nunca un compilador de escena general: lo que no encaje es un **escape** a `eng::field`. `plan.hpp` es **puro** (describe y decide; sin motores ni hardware).

| Símbolo | Qué es |
|---|---|
| `LayerRole` (`plan.hpp`) | `Background`/`Foreground`/`Overlay`: semántica y orden de composición. |
| `LayerPlacement` | `top`/`height` (banda; `0` = full)/`field`; `full()`/`band()`/`ok()`. |
| `LayerContent` | `Scroll` (tilemap) o `Canvas` (lienzo estático, sin mapa). |
| `WorldLayerKind` (`world.hpp`) | `Actors`/`Tilemap`/`Fill`/`Bitmap`: contenido de una capa del mundo. |
| `ScrollKind` (`world.hpp`) | Técnica de scroll pedida: `None`/`Fine`/`BlitterColumns`/`CopperRing`/`CopperSplit`/`Strip`. |

## `World` — `world.hpp`

```cpp
auto fondo = app.world().add_layer("fondo", 0);
if (fondo) {
    fondo->camera().reset({{0, 0, 640, 256}}, {320, 256});
    fondo->camera().set_scroll_x(128);
}
```

`add_layer` devuelve `Ref<Layer>` (**anulable**): si el mundo está lleno devuelve uno inválido en vez de un `Layer&` a memoria basura.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
