#pragma once

/// \file society.hpp
/// **Sociedad** (`eng::sim`): reputación entre facciones y estructura de manada. Es lo
/// que convierte un conjunto de criaturas en colonias, tribus o jerarquías: una criatura
/// recuerda cómo la ha tratado cada facción y actúa en consecuencia (comercio, tributos,
/// hostilidad), y las especies gregarias forman packs con un líder.
///
/// Se mantiene fuera de `AbstractCreature` para que cada criatura no cargue con una
/// matriz de reputación: la tabla es **global por facción** y la consulta es `O(1)`.
/// Con `SimTraits::society == false` el juego no la usa y el camino se elimina en
/// compilación.
///
/// Verificación: HOST-153.

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Manada/colonia: un líder y sus miembros, con capacidad fija.
struct Pack {
	EntityId leader = no_entity;
	eng::util::StaticVector<EntityId, kMaxPackMembers> members {};

	[[nodiscard]] constexpr bool contains(EntityId e) const noexcept {
		if (e == leader) {
			return true;
		}
		for (eng::usize i = 0; i < members.size(); ++i) {
			if (members[i] == e) {
				return true;
			}
		}
		return false;
	}

	/// Une a `e`; si la manada estaba vacía, pasa a ser el líder. `false` si está llena.
	/// El líder no figura en `members` (se cuentan por separado).
	constexpr bool join(EntityId e) noexcept {
		if (e == no_entity || contains(e)) {
			return true;
		}
		if (leader == no_entity) {
			leader = e;
			return true;
		}
		return members.push_back(e);
	}

	/// Saca a `e` de la manada. Si se va el líder, el primer miembro asciende.
	constexpr void leave(EntityId e) noexcept {
		if (e == leader) {
			leader = members.empty() ? no_entity : members[0];
			if (!members.empty()) {
				members.erase(0u);
			}
			return;
		}
		for (eng::usize i = 0; i < members.size(); ++i) {
			if (members[i] == e) {
				members.erase(i);
				return;
			}
		}
	}

	[[nodiscard]] constexpr eng::usize size() const noexcept {
		return members.size() + (leader != no_entity ? 1u : 0u);
	}
};

/// Tabla de reputación por facción. `0` es neutral, `+100` aliado incondicional y `-100`
/// enemigo declarado; los umbrales de hostilidad/amistad son política del juego.
struct Society {
	eng::s8 reputation[kMaxFactions] {};

	static constexpr eng::s8 hostile_threshold = -40;
	static constexpr eng::s8 friendly_threshold = 40;

	[[nodiscard]] constexpr eng::s8 rep(FactionId f) const noexcept {
		return f < kMaxFactions ? reputation[f] : static_cast<eng::s8>(0);
	}

	/// Ajusta la reputación con una facción (clamp a `[-100, 100]`).
	constexpr void adjust(FactionId f, eng::s16 delta) noexcept {
		if (f >= kMaxFactions) {
			return;
		}
		const eng::s16 next = static_cast<eng::s16>(reputation[f]) + delta;
		if (next > 100) {
			reputation[f] = 100;
		} else if (next < -100) {
			reputation[f] = -100;
		} else {
			reputation[f] = static_cast<eng::s8>(next);
		}
	}

	[[nodiscard]] constexpr bool hostile(FactionId f) const noexcept {
		return rep(f) <= hostile_threshold;
	}
	[[nodiscard]] constexpr bool friendly(FactionId f) const noexcept {
		return rep(f) >= friendly_threshold;
	}
	[[nodiscard]] constexpr bool neutral(FactionId f) const noexcept {
		return !hostile(f) && !friendly(f);
	}

	constexpr void reset() noexcept {
		for (eng::u8 i = 0; i < kMaxFactions; ++i) {
			reputation[i] = 0;
		}
	}
};

} // namespace eng::sim
