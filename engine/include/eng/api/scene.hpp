#pragma once

/// \file scene.hpp
/// **Vocabulario de escena de la fachada** (planner §7): reexporta al espacio de nombres de juego
/// el **plan de escena** (`ScenePlan`/`LayerPlan`), sus roles/colocación/contenido, las
/// **estrategias** (`Single`/`Dpf`/`Bands`) y los helpers de composición. El juego declara la escena
/// con esto; los motores (`StripScrollLayer`/`XlimitedScene`) quedan detrás. Ver
/// `PUBLIC_GAME_API.md` §2.1.3.

#include <eng/api/scroll.hpp> // `playfield::ScrollPlan` (contenido de una capa)
#include <eng/scene/band_plan.hpp>
#include <eng/scene/banded_target.hpp>
#include <eng/scene/dpf_plan.hpp>
#include <eng/scene/plan.hpp>
#include <eng/scene/raster_plan.hpp>

namespace eng {

using scene::BandSpan;
using scene::LayerContent;
using scene::LayerPlacement;
using scene::LayerPlan;
using scene::LayerRole;
using scene::ScenePlan;
using scene::SceneStrategy;

using scene::apply_dpf_plan;
using scene::for_each_band_part;
using scene::plan_bands;
using scene::plan_raster_layout;

} // namespace eng
