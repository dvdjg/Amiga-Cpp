#pragma once

/// \file api.hpp
/// **Fachada pública del engine**: un solo `#include` para el código de juego/demo.
///
/// Reúne las cabeceras **estables** de la API —bucle y contrato de juego, escena y
/// composición, dibujo (`Surface`/`DrawTarget`/`FramePlan`), rasterizado CPU/Blitter,
/// paleta, entrada, tareas de fondo, los valores preparados de Blitter y la librería GUI
/// (`eng::ui`)— sin definir tipos nuevos: solo incluye las fuentes canónicas, de modo que
/// no hay duplicación ni una segunda verdad que mantener.
///
/// **No incluye el backend** (p. ej. `eng/platform/amiga_minimal.hpp`): el backend se
/// instancia en `main()` y se pasa a `eng::Engine`. Un juego portable incluye solo esta
/// fachada; una demo Amiga añade su backend y (si procede) las utilidades de plataforma.
///
/// Ver `docs/engine/architecture/ENGINE_STRUCTURE_REVIEW.md` para el estado de la
/// consolidación de la API pública.

#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/input/input.hpp>
#include <eng/memory/arena.hpp>
#include <eng/scene/actor.hpp>
#include <eng/task/background.hpp>
#include <eng/ui/ui.hpp>
