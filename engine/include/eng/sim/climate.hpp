#pragma once

/// \file climate.hpp
/// **Clima global** (`eng::sim`): un peligro ambiental por región que evoluciona con el
/// tiempo (se forma, se extiende y se disipa). Cierra el ciclo del refugio: la criatura
/// percibe una `exposure` con su `HazardKind`, y la **mitiga el terreno** (abrigo) o el
/// estar a cubierto, no una estructura ad-hoc.
///
/// - `RegionHazard`: tipo y severidad de peligro en una región/habitación.
/// - `Climate<MaxRooms>`: estado por región; `set`/`add` forman el peligro, `tick` lo
///   disipa y `strongest` dice dónde aprieta más.
/// - `effective_exposure`: convierte severidad + abrigo del terreno en la `exposure`
///   efectiva que se fija en `Needs`.
///
/// El clima es agnóstico de la representación; el juego decide si llueve, hiela o hay una
/// tormenta de polvo, y el arte lo dibuja.
///
/// Verificación: HOST-158.

#include <eng/core/types.hpp>
#include <eng/sim/needs.hpp>
#include <eng/sim/terrain.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Peligro ambiental de una región.
struct RegionHazard {
	HazardKind kind = HazardKind::None;
	eng::u8 severity = 0; ///< 0 = sin peligro; 255 = extremo
};

/// Clima por región (hasta `MaxRooms`).
template <eng::u8 MaxRooms>
struct Climate {
	RegionHazard regions[MaxRooms] {};

	constexpr void clear() noexcept {
		for (eng::u8 i = 0; i < MaxRooms; ++i) {
			regions[i] = RegionHazard {};
		}
	}

	/// Fija el peligro de una región (lo sustituye).
	constexpr void set(RoomId r, HazardKind kind, eng::u8 severity) noexcept {
		if (r < MaxRooms) {
			regions[r] = RegionHazard {kind, severity};
		}
	}

	/// Añade severidad (saturada); conserva el tipo si ya había peligro.
	constexpr void add(RoomId r, HazardKind kind, eng::u8 amount) noexcept {
		if (r >= MaxRooms) {
			return;
		}
		if (regions[r].kind == HazardKind::None || regions[r].severity == 0u) {
			regions[r].kind = kind;
		}
		regions[r].severity = u8_sat_add(regions[r].severity, amount);
	}

	[[nodiscard]] constexpr RegionHazard at(RoomId r) const noexcept {
		return r < MaxRooms ? regions[r] : RegionHazard {};
	}
	[[nodiscard]] constexpr eng::u8 severity(RoomId r) const noexcept {
		return r < MaxRooms ? regions[r].severity : 0u;
	}
	[[nodiscard]] constexpr bool severe(RoomId r, eng::u8 threshold = 100u) const noexcept {
		return severity(r) >= threshold;
	}

	/// Disipa el peligro de todas las regiones (una vez por tick de clima).
	constexpr void tick(eng::u8 decay) noexcept {
		for (eng::u8 i = 0; i < MaxRooms; ++i) {
			regions[i].severity = u8_sat_sub(regions[i].severity, decay);
			if (regions[i].severity == 0u) {
				regions[i].kind = HazardKind::None;
			}
		}
	}

	/// Región con más peligro (`no_room` si no hay ninguna).
	[[nodiscard]] constexpr RoomId strongest() const noexcept {
		RoomId best = no_room;
		eng::u8 top = 0u;
		for (eng::u8 i = 0; i < MaxRooms; ++i) {
			if (regions[i].severity > top) {
				top = regions[i].severity;
				best = i;
			}
		}
		return best;
	}

	[[nodiscard]] constexpr eng::u8 max_severity() const noexcept {
		eng::u8 top = 0u;
		for (eng::u8 i = 0; i < MaxRooms; ++i) {
			if (regions[i].severity > top) {
				top = regions[i].severity;
			}
		}
		return top;
	}
};

/// Exposición efectiva tras el abrigo del terreno (0 = protegido). `shelter` en `[0,100]`.
[[nodiscard]] constexpr eng::u8 effective_exposure(eng::u8 severity,
						   eng::u8 shelter) noexcept {
	const eng::u8 cover = shelter > 100u ? static_cast<eng::u8>(100u) : shelter;
	return u8_scale(severity, static_cast<eng::u8>(100u - cover));
}

/// Exposición que sufre una criatura: 0 si está totalmente a cubierto; si no, severidad
/// mitigada por el abrigo del terreno de su región.
[[nodiscard]] constexpr eng::u8 exposure_at(const RegionHazard& h, const RegionTerrain& region,
					    bool fully_sheltered) noexcept {
	if (fully_sheltered) {
		return 0u;
	}
	const eng::u8 shelter = u8_sat_add(region.shelter, terrain_shelter(region.dominant));
	return effective_exposure(h.severity, shelter);
}

} // namespace eng::sim
