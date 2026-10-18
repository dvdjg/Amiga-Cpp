#pragma once

/// \file hotspot.hpp
/// **Hotspots de sala** (`eng::adv`): rectángulos clicables ligados a un objeto del juego
/// (el `Look at`/`Use` clásico). En el contenido convertido cada hotspot nace de un objeto de
/// sala (`OBCD`/`OBIM` → `SIM v2.objects[]`); el runtime de verbos (A1 de `ROADMAP_ADVENTURE.md`)
/// los consume para el hit-test del ratón. Solo consultas puras, genéricas sobre el escalar.

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/adv/types.hpp>

namespace eng::adv {

/// Hotspot rectangular ligado a un id de objeto del juego.
/// \tparam S escalar de coordenada (s16/s32/float).
template <class S>
struct Hotspot {
	eng::math::Vec<2, S> min {}; ///< esquina mínima (inclusive)
	eng::math::Vec<2, S> max {}; ///< esquina máxima (inclusive)
	HotspotId id {kInvalidHotspot}; ///< id de objeto del juego original

	/// ¿`p` cae dentro del rectángulo? (bordes incluidos; se acepta min/max en cualquier orden.)
	/// \param p punto en coordenadas de la sala.
	/// \return `true` si `p` está dentro.
	[[nodiscard]] constexpr bool contains(eng::math::Vec<2, S> p) const noexcept {
		const S x0 = min.v[0] < max.v[0] ? min.v[0] : max.v[0];
		const S x1 = min.v[0] < max.v[0] ? max.v[0] : min.v[0];
		const S y0 = min.v[1] < max.v[1] ? min.v[1] : max.v[1];
		const S y1 = min.v[1] < max.v[1] ? max.v[1] : min.v[1];
		return p.v[0] >= x0 && p.v[0] <= x1 && p.v[1] >= y0 && p.v[1] <= y1;
	}
};

/// Hit-test de hotspots: devuelve el **último** que contiene `p` (el dibujado más al frente,
/// como el original: los objetos posteriores de la sala tapan a los anteriores), o
/// `kInvalidHotspot`.
/// \param hotspots vista de hotspots de la sala (orden de datos: frente al final).
/// \param p punto en coordenadas de la sala.
/// \return id del hotspot o `kInvalidHotspot`.
template <class S>
[[nodiscard]] constexpr HotspotId hit_test(eng::Span<const Hotspot<S>> hotspots,
					   eng::math::Vec<2, S> p) noexcept {
	for (eng::usize i = hotspots.size(); i > 0; --i) {
		if (hotspots[i - 1].contains(p)) {
			return hotspots[i - 1].id;
		}
	}
	return kInvalidHotspot;
}

} // namespace eng::adv
