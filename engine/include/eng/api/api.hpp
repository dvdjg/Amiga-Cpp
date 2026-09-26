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
/// **No incluye el backend** (p. ej. `eng/platform/amiga/backend.hpp`): el backend se
/// instancia en `main()` y se pasa a `eng::Engine`. Un juego portable incluye solo esta
/// fachada; una demo Amiga añade su backend y (si procede) las utilidades de plataforma.
///
/// Ver `docs/engine/architecture/ENGINE_STRUCTURE_REVIEW.md` para el estado de la
/// consolidación de la API pública.

#include <eng/api/copper.hpp>
#include <eng/api/game.hpp>
#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/result.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/chr.hpp>
#include <eng/graphics/font_italic.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/glyph_cache.hpp>
#include <eng/graphics/tilemap/attribute_table.hpp>
#include <eng/graphics/tilemap/tile_editor.hpp>
#include <eng/graphics/palette.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/input/input.hpp>
#include <eng/memory/arena.hpp>
#include <eng/res/budget.hpp>
#include <eng/res/chip_pool.hpp>
#include <eng/res/load.hpp>
#include <eng/scene/actor.hpp>
#include <eng/scene/virtual_scene.hpp>
#include <eng/scene/world.hpp>
#include <eng/scene/scroll_plan.hpp>
#include <eng/task/background.hpp>
#include <eng/ui/ui.hpp>
