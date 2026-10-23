#pragma once

/// \file formation.hpp
/// **Formaciones y oleadas** (F2 de `ROADMAP_JUEGO_SPRITES_BOBS.md` §5): una tabla
/// cocinada de miembros (delay + offset + id de trayectoria), un actualizador que avisa
/// al juego de quién debe spawnear en cada tick y **generadores de ola en V** (F2.1,
/// `ROADMAP_SPRITES_COMPLETO.md` §3.1). No posee las entidades: el juego las crea en su
/// pool y les asigna la trayectoria indicada.
///
/// Genérico sobre el escalar `S` de las posiciones (patrón del repo: `Vec<2,S>`).
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::scene {

/// Miembro de una formación: cuántos ticks espera antes de aparecer (`0` = en el primer
/// update), dónde (offset respecto al origen de la formación) y qué trayectoria sigue.
/// \tparam S escalar genérico (entero, `Fixed` o `float`)
template <typename S>
struct FormationMember {
	u16 delay_ticks = 0;
	eng::math::Vec<2, S> offset {};
	u8 trajectory = 0;  ///< índice en la tabla de trayectorias del nivel
};

/// Descripción cocinada de una oleada.
/// \tparam S escalar genérico (entero, `Fixed` o `float`)
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
/// \tparam S escalar genérico (entero, `Fixed` o `float`)
/// \tparam SpawnFn callable que crea una entidad
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

/// **Generador de ola en V** (clásica de shmup): reparte `count` miembros simétricos
/// respecto al centro, con `delay_ticks` creciente hacia las puntas (**la punta entra
/// primero**) y un desplazamiento en Y que abre la V en `spread_x / 2` por paso.
/// `open_down = false` deja la punta arriba (los brazos caen hacia abajo); `true` la
/// invierte. Todos los miembros siguen la misma `trajectory`. Con `count` impar la V es
/// exacta; con par, la mitad izquierda incluye el centro.
///
/// El offset X del miembro `i` es `(i - count/2) * spread_x` y su Y es proporcional a la
/// distancia al centro (`dist * spread_x / 2`), con `dist = |i - count/2|`. Devuelve
/// cuántos miembros escribió (tope por `out.size()`).
/// \tparam S escalar genérico (entero, `Fixed` o `float`)
template <typename S>
inline u16 gen_formation_v(eng::Span<FormationMember<S>> out, u8 count,
			   eng::util::type_identity_t<S> spread_x, u16 delay_step, u8 trajectory,
			   bool open_down = false) noexcept {
	if (count == 0u || out.empty()) {
		return 0u;
	}
	const u8 half = static_cast<u8>(count / 2u);
	u16 n = 0u;
	for (u8 i = 0u; i < count && n < out.size(); ++i) {
		const s16 dist = static_cast<s16>(i <= half ? static_cast<u8>(half - i)
							    : static_cast<u8>(i - half));
		const s16 col = static_cast<s16>(i) - static_cast<s16>(half);
		FormationMember<S>& m = out[n];
		m.delay_ticks = static_cast<u16>(dist * delay_step);
		m.offset.x() = static_cast<S>(col) * spread_x;
		const S depth = static_cast<S>(dist) * static_cast<S>(spread_x / 2);
		m.offset.y() = open_down ? static_cast<S>(-depth) : depth;
		m.trajectory = trajectory;
		++n;
	}
	return n;
}

/// **Ola en V con seno en espejo**: como `gen_formation_v` (punta arriba), pero cada
/// mitad usa su propia trayectoria (p. ej. la misma seno con fases 0 y 128): los brazos
/// ondulan en espejo. El miembro central (y la mitad izquierda) usa `trajectory_left`.
/// Devuelve cuántos miembros escribió (tope por `out.size()`).
/// \tparam S escalar genérico (entero, `Fixed` o `float`)
template <typename S>
inline u16 gen_formation_v_sine(eng::Span<FormationMember<S>> out, u8 count,
				eng::util::type_identity_t<S> spread_x, u16 delay_step,
				u8 trajectory_left, u8 trajectory_right) noexcept {
	if (count == 0u || out.empty()) {
		return 0u;
	}
	const u8 half = static_cast<u8>(count / 2u);
	u16 n = 0u;
	for (u8 i = 0u; i < count && n < out.size(); ++i) {
		const s16 dist = static_cast<s16>(i <= half ? static_cast<u8>(half - i)
							    : static_cast<u8>(i - half));
		const s16 col = static_cast<s16>(i) - static_cast<s16>(half);
		FormationMember<S>& m = out[n];
		m.delay_ticks = static_cast<u16>(dist * delay_step);
		m.offset.x() = static_cast<S>(col) * spread_x;
		m.offset.y() = static_cast<S>(dist) * static_cast<S>(spread_x / 2);
		m.trajectory = (i <= half) ? trajectory_left : trajectory_right;
		++n;
	}
	return n;
}

} // namespace eng::scene
