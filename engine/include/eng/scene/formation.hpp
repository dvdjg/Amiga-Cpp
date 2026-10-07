#pragma once

/// \file formation.hpp
/// **Formaciones y oleadas** (F2 de `ROADMAP_JUEGO_SPRITES_BOBS.md` §5): una tabla
/// cocinada de miembros (delay + offset + id de trayectoria) y un actualizador que avisa
/// al juego de quién debe spawnear en cada tick. No posee las entidades: el juego las crea
/// en su pool y les asigna la trayectoria indicada.
///
/// Genérico sobre el escalar `S` de las posiciones (patrón del repo: `Vec<2,S>`).
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene {

/// Miembro de una formación: cuántos ticks espera antes de aparecer (`0` = en el primer
/// update), dónde (offset respecto al origen de la formación) y qué trayectoria sigue.
template <typename S>
struct FormationMember {
	u16 delay_ticks = 0;
	eng::math::Vec<2, S> offset {};
	u8 trajectory = 0;  ///< índice en la tabla de trayectorias del nivel
};

/// Descripción cocinada de una oleada.
template <typename S>
struct Formation {
	eng::Span<const FormationMember<S>> members {};
	eng::math::Vec<2, S> origin {};  ///< origen de la formación (mundo)
};

/// Estado de runtime de una oleada.
struct FormationState {
	eng::u32 age = 0;  ///< ticks desde que empezó
	bool active = false;
};

/// Avanza la oleada `ticks` ticks y llama `spawn(member_index, trajectory_id, world_pos)`
/// por cada miembro cuyo `delay_ticks` cae en el intervalo `[age, age+ticks)` (el delay es
/// **ticks de espera**; `0` sale en el primer update). Así ningún miembro se pierde aunque
/// el marco pase de golpe (o llegue un pico de ticks), y las oleadas son deterministas.
/// `spawn` decide qué entidad crear (pool del juego).
template <typename S, class SpawnFn>
inline void formation_update(const Formation<S>& form, FormationState& st, u16 ticks,
			     SpawnFn&& spawn) noexcept {
	if (!st.active) {
		return;
	}
	const eng::u32 prev = st.age;
	st.age += ticks;
	for (eng::usize i = 0; i < form.members.size(); ++i) {
		const FormationMember<S>& m = form.members[i];
		if (static_cast<eng::u32>(m.delay_ticks) >= prev &&
		    static_cast<eng::u32>(m.delay_ticks) < st.age) {
			spawn(i, m.trajectory, form.origin + m.offset);
		}
	}
}

} // namespace eng::scene
