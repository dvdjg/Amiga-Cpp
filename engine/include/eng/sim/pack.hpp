#pragma once

/// \file pack.hpp
/// **Coordinación de manadas** (`eng::sim`): reparto de roles y tácticas emergentes
/// guiadas por señales y jerarquía. El líder marca el objetivo (la mejor presa de su
/// memoria de corto plazo); los miembros convergen a su alrededor tomando posiciones
/// (flanqueo) en vez de amontonarse, y el conjunto comunica con la **llamada de caza**.
///
/// - `PackRole`: líder, seguidor, flanqueador, explorador.
/// - `pack_role_for`: asigna rol según liderazgo e índice.
/// - `flank_goal`: punto al que debe ir cada rol respecto al objetivo (cerca/envolviendo).
///
/// El líder lo determina la jerarquía (`hierarchy.hpp`: más dominancia) y las relaciones
/// `Pack` (`relationship.hpp`). La coordinación la ejecuta `SimWorld::coordinate_packs`.
///
/// Verificación: HOST-172.

#include <eng/core/types/types.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Rol dentro de una manada.
enum class PackRole : eng::u8 {
	Leader = 0,
	Follower = 1,
	Flanker = 2,
	Scout = 3,
	Count = 4,
};

/// Parámetros de coordinación.
struct PackParams {
	eng::u8 flank_distance = 3u; ///< separación de los flancos respecto a la presa
	eng::u8 call_range = 16u;    ///< alcance de la llamada de caza
	eng::u8 call_intensity = 180u;
};

/// Nombre legible.
[[nodiscard]] constexpr const char* pack_role_name(PackRole r) noexcept {
	switch (r) {
		case PackRole::Leader: return "leader";
		case PackRole::Follower: return "follower";
		case PackRole::Flanker: return "flanker";
		case PackRole::Scout: return "scout";
		default: return "?";
	}
}

/// Asigna rol: el líder manda; el resto alterna flanqueadores y seguidores.
[[nodiscard]] constexpr PackRole pack_role_for(bool is_leader, eng::u8 index) noexcept {
	if (is_leader) {
		return PackRole::Leader;
	}
	switch (index % 3u) {
		case 0u: return PackRole::Flanker;
		case 1u: return PackRole::Follower;
		default: return PackRole::Scout;
	}
}

/// Punto al que debe ir un rol respecto al objetivo. Los flanqueadores se reparten a
/// ambos lados (par/impar) para **envolver** en vez de amontonarse.
[[nodiscard]] constexpr eng::Point2s flank_goal(eng::Point2s target, PackRole role,
						eng::u8 index, eng::u8 dist) noexcept {
	switch (role) {
		case PackRole::Leader:
			return target;
		case PackRole::Follower:
			return eng::Point2s {static_cast<eng::s16>(target.x - dist), target.y};
		case PackRole::Flanker:
			return (index % 2u == 0u)
				       ? eng::Point2s {target.x, static_cast<eng::s16>(target.y + dist)}
				       : eng::Point2s {target.x, static_cast<eng::s16>(target.y - dist)};
		case PackRole::Scout:
			return eng::Point2s {static_cast<eng::s16>(target.x - dist * 2),
					     static_cast<eng::s16>(target.y + dist)};
		default:
			return target;
	}
}

} // namespace eng::sim
