# `eng::api` — fachada pública del engine

Un solo `#include <eng/api/api.hpp>` para el código de juego/demo: reúne las cabeceras
**estables** de la API sin definir tipos nuevos (no duplica la verdad). Incluye bucle y
contrato de juego (`Engine`/`GameContext`), escena y composición (`scene::Scene`/`compose`),
dibujo (`playfield::Surface`/`DrawTarget`/`graphics::FramePlan`), rasterizado CPU/Blitter
(`playfield::Rasterizer`/`RasterOp`), **scroll de juego** (`api/scroll.hpp`: el vocabulario
`ScrollSpec`/`Camera2D` y los motores de tiras/corcóscru), paleta, entrada
(`input::InputAggregator`), tareas de fondo (`task::BackgroundQueue`) y los valores preparados de
Blitter (`graphics::OrBob`/`LineEor`/`C2p4`).

**No incluye el backend** (`eng/platform/amiga/backend.hpp`): se instancia en `main()` y se
pasa a `eng::Engine`. Un juego portable incluye solo esta fachada; una demo Amiga añade su
backend.

El **3D de juego** tiene su propia fachada, [`3d.hpp`](3d.hpp) (`eng::scene3d`), separada para no cargar el pipeline 2D con el renderer 3D. G0 ofrece escena retenida de capacidad fija, cámara, nodos y render por `render_solid`; los materiales de asset, la animación y la física llegan en las fases G1–G7 de `ROADMAP_3D_GAME_API.md`. Un juego 3D incluye `api.hpp` y `3d.hpp`.

Las fachadas directas `scene/sprite_scene.hpp` (`SpriteScene`) y `scene/composite_scene.hpp` (`CompositeScene`) son **legacy avanzado**: no se incluyen desde `api.hpp` y no deben usarse en codigo nuevo; la via canonica es `scene::SpriteDirector`.

Estado y decisiones de consolidación: `docs/engine/architecture/ENGINE_STRUCTURE_REVIEW.md`.

El camino 2D de alto nivel basado en intenciones se documenta en [INTENTION_API_TUTORIAL.md](../../../docs/guides/INTENTION_API_TUTORIAL.md): SpriteDirector (actores/intenciones), SpriteBank/SpriteGameView (slots y DATA) y el compositor/parcheo interno. Las cabeceras tecnicas siguen disponibles para control avanzado.
