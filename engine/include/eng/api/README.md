# `eng::api` — fachada pública del engine

Un solo `#include <eng/api/api.hpp>` para el código de juego/demo: reúne las cabeceras
**estables** de la API sin definir tipos nuevos (no duplica la verdad). Incluye bucle y
contrato de juego (`Engine`/`GameContext`), escena y composición (`scene::Scene`/`compose`),
dibujo (`field::Surface`/`DrawTarget`/`graphics::FramePlan`), rasterizado CPU/Blitter
(`field::Rasterizer`/`RasterOp`), paleta, entrada (`input::InputAggregator`), tareas de fondo
(`task::BackgroundQueue`) y los valores preparados de Blitter (`graphics::OrBob`/`LineEor`/
`C2p4`).

**No incluye el backend** (`eng/platform/amiga/backend.hpp`): se instancia en `main()` y se
pasa a `eng::Engine`. Un juego portable incluye solo esta fachada; una demo Amiga añade su
backend.

Estado y decisiones de consolidación: `docs/engine/architecture/ENGINE_STRUCTURE_REVIEW.md`.
