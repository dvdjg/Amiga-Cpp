#pragma once

/// \file raster_plan.hpp
/// **Convergencia planner ↔ `RasterLayout`** (`ROADMAP_GAME_API.md` §7): construye un
/// `scene::RasterLayout` (la composición **general** de pantalla por bandas — DPF, split-screen,
/// bandas mode-switch, franja HUD) desde un **`ScenePlan`** declarativo + una **vista por capa**
/// (`playfield::PlayfieldHardwareView`). Así el mismo vocabulario del planner sirve al camino de
/// **bajo nivel** (`RasterLayout`, demos de técnica como 127/129) y al de escena (`XlimitedScene`).
///
/// El mapeo lo fija la **estrategia** del plan:
///   - `Single` → 1 banda a pantalla completa;
///   - `Dpf`    → 1 banda **dual** (`band_from_dual_view`, PF1 delante + PF2 detrás);
///   - `Bands`  → 1 banda por capa (`band_from_view`) en el `top` de su colocación.
///
/// El llamador aporta las vistas en el **orden de las capas** del plan y luego **materializa** el
/// layout (o lo extiende con franjas extra, como la tira de 0 planos de la 127).

#include <eng/core/util/expected.hpp>
#include <eng/scene/display.hpp>
#include <eng/scene/plan.hpp>

namespace eng::scene {

/// Causa de fallo de `plan_raster_layout`.
enum class RasterPlanError : eng::u8 {
	NoViews,             ///< faltan vistas para las capas del plan
	UnsupportedStrategy, ///< la estrategia no tiene mapeo a `RasterLayout` (`Empty`/`Unsupported`)
};

/// Construye `out` (un `RasterLayout`) desde `plan` + `views` (una por capa, en orden). Devuelve el
/// nº de bandas añadidas, o el error. No limpia `out` (permite añadir franjas antes/después).
template <eng::u16 MaxLayers>
[[nodiscard]] eng::util::Expected<eng::u8, RasterPlanError>
plan_raster_layout(const ScenePlan<MaxLayers>& plan,
		   eng::Span<const eng::playfield::PlayfieldHardwareView> views,
		   RasterLayout& out) noexcept {
	switch (plan.strategy()) {
	case SceneStrategy::Single: {
		if (views.size() < 1u) return eng::util::unexpected(RasterPlanError::NoViews);
		(void)out.add(band_from_view(views[0], 0u));
		return 1u;
	}
	case SceneStrategy::Dpf: {
		if (views.size() < 2u) return eng::util::unexpected(RasterPlanError::NoViews);
		// PF1 (delante) = primera capa (Foreground); PF2 (detrás) = segunda (Background).
		(void)out.add(band_from_dual_view(views[0], views[1], 0u));
		return 1u;
	}
	case SceneStrategy::Bands: {
		if (views.size() < plan.count()) {
			return eng::util::unexpected(RasterPlanError::NoViews);
		}
		eng::u8 n = 0u;
		for (eng::u16 i = 0u; i < plan.count(); ++i) {
			(void)out.add(band_from_view(views[i], plan.layer(i).placement.top));
			++n;
		}
		return n;
	}
	default:
		return eng::util::unexpected(RasterPlanError::UnsupportedStrategy);
	}
}

} // namespace eng::scene
