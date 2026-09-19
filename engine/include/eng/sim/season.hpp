#pragma once

/// \file season.hpp
/// **Aforo dinámico** (`eng::sim`): la capacidad de una región no es fija, depende de la
/// **estación** y del **clima**. En invierno o con una tormenta encima, el bioma sostiene
/// menos criaturas; en primavera o con tiempo benigno, más. Es lo que hace que la población
/// respire con el entorno en vez de quedarse clavada en un tope.
///
/// - `Season`: primavera, verano, otoño, invierno.
/// - `SeasonParams`: factor porcentual de cada estación (110/100/90/70 por defecto).
/// - `season_factor` y `climate_factor`: porcentajes que combina `effective_capacity`.
///
/// Verificación: HOST-174 (aforo dinámico).

#include <eng/core/types.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Estación del año.
enum class Season : eng::u8 {
	Spring = 0,
	Summer = 1,
	Autumn = 2,
	Winter = 3,
	Count = 4,
};

/// Factores de capacidad por estación (porcentaje sobre el aforo base).
struct SeasonParams {
	eng::u8 spring = 110u;
	eng::u8 summer = 100u;
	eng::u8 autumn = 90u;
	eng::u8 winter = 70u;
};

/// Nombre legible.
[[nodiscard]] constexpr const char* season_name(Season s) noexcept {
	switch (s) {
		case Season::Spring: return "spring";
		case Season::Summer: return "summer";
		case Season::Autumn: return "autumn";
		case Season::Winter: return "winter";
		default: return "?";
	}
}

/// Factor porcentual de la estación.
[[nodiscard]] constexpr eng::u8 season_factor(Season s,
					      const SeasonParams& p = SeasonParams {}) noexcept {
	switch (s) {
		case Season::Spring: return p.spring;
		case Season::Summer: return p.summer;
		case Season::Autumn: return p.autumn;
		case Season::Winter: return p.winter;
		default: return 100u;
	}
}

/// Factor porcentual según el clima: cuanto más severa la exposición, menos sostiene el
/// bioma (una tormenta extrema reduce a la mitad).
[[nodiscard]] constexpr eng::u8 climate_factor(eng::u8 severity) noexcept {
	if (severity >= 200u) {
		return 50u;
	}
	if (severity >= 100u) {
		return 75u;
	}
	return 100u;
}

/// Aforo efectivo de una región: base del bioma modulada por estación y clima (mínimo 1).
[[nodiscard]] constexpr eng::u8 effective_capacity(eng::u8 base, Season s, eng::u8 severity,
						   const SeasonParams& sp = SeasonParams {}) noexcept {
	if (base == 0u) {
		return 0u;
	}
	const eng::u8 by_season = u8_scale(base, season_factor(s, sp));
	const eng::u8 by_climate = u8_scale(by_season, climate_factor(severity));
	return by_climate == 0u ? static_cast<eng::u8>(1u) : by_climate;
}

} // namespace eng::sim
