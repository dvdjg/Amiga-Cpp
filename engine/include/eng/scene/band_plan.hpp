#pragma once

/// \file band_plan.hpp
/// **Estrategia `Bands`** del planner (`ROADMAP_GAME_API.md` §7): a partir de un `ScenePlan` con
/// capas en **banda** (`LayerPlacement::Band`), valida el **layout** (de arriba abajo, sin solapes,
/// dentro del display) y produce los tramos `{top, height, rol}`. Es el paso previo a emitir el
/// display por bandas con `scene::emit_display`/`ModeSwitchZone` (mecanismo que ya existe).
///
/// Caso típico: **split-screen** (dos jugadores) → dos bandas apiladas, cada una con su motor y su
/// cámara (XUnlimited/YUnlimited por campo para evitar el split de línea). Es **puro**: solo geometría
/// y validación.

#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/scene/plan.hpp>

namespace eng::scene {

/// Tramo de banda calculado por el planner.
struct BandSpan {
	eng::u16 top = 0u;
	eng::u16 height = 0u;
	LayerRole role = LayerRole::Foreground;
};

/// Causa de fallo de `plan_bands`.
enum class BandPlanError : eng::u8 {
	NotBands,   ///< alguna capa no es una banda (usa la estrategia `Bands`)
	BadOrder,   ///< bandas no ordenadas de arriba abajo o solapadas
	OutOfRange, ///< una banda se sale del display
};

/// Valida las capas en banda y escribe sus tramos en `out` (sin terminador). Exige que **todas**
/// sean bandas, con `top` ascendente y sin solape, dentro de `[0, rows)`. Devuelve cuántas, o el error.
[[nodiscard]] inline eng::util::Expected<eng::u16, BandPlanError>
plan_bands(eng::Span<const LayerPlan> layers, eng::u16 rows, eng::Span<BandSpan> out) noexcept {
	if (layers.empty()) {
		return eng::util::unexpected(BandPlanError::NotBands);
	}
	eng::u16 n = 0u;
	eng::u16 prev_end = 0u;
	for (eng::usize i = 0u; i < layers.size(); ++i) {
		const LayerPlan& l = layers[i];
		if (!l.placement.band()) {
			return eng::util::unexpected(BandPlanError::NotBands);
		}
		if (n >= out.size()) {
			return eng::util::unexpected(BandPlanError::OutOfRange); // capacidad de `out` insuficiente
		}
		if (l.placement.top < prev_end) {
			return eng::util::unexpected(BandPlanError::BadOrder);
		}
		const eng::u32 end = static_cast<eng::u32>(l.placement.top) + l.placement.height;
		if (end > rows) {
			return eng::util::unexpected(BandPlanError::OutOfRange);
		}
		out[n].top = l.placement.top;
		out[n].height = l.placement.height;
		out[n].role = l.role;
		++n;
		prev_end = static_cast<eng::u16>(end);
	}
	return n;
}

} // namespace eng::scene
