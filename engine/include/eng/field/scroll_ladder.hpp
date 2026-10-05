#pragma once

/// \file scroll_ladder.hpp
/// **Escalera de motores de scroll** (`eng::playfield::ScrollLadder`): un **registro** de motores
/// (`ScrollLayer<Backend>`) cada uno con la **geometría runtime** que implementa
/// (`RuntimeScrollGeometry`). El planner/`App` **elige** por geometría con `pick(g)`: cubre el caso
/// de geometría **no conocida en compilación** (editor) **sin refactorizar** el motor NTTP (que sigue
/// siendo el rápido). El juego declara los motores que conoce (habitualmente uno por nivel) y los
/// registra; el `App`/planner selecciona el que encaja con lo que cargue.
///
/// Ver `ROADMAP_GAME_API.md` §7 (paso 4). Es `Ref` (no propietario): los motores los posee el juego.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/runtime_scroll_geometry.hpp>
#include <eng/field/scroll_layer.hpp>

namespace eng::playfield {

/// ¿Las dos geometrías describen el **mismo layout** físico (ventana, tiles, planos, anillo)? Ignora
/// los parámetros de control (guard/fetch/split) que no cambian dónde vive el contenido.
[[nodiscard]] constexpr bool same_scroll_layout(const RuntimeScrollGeometry& a,
						const RuntimeScrollGeometry& b) noexcept {
	return a.viewport_w == b.viewport_w && a.viewport_h == b.viewport_h &&
	       a.tile_w == b.tile_w && a.tile_h == b.tile_h && a.planes == b.planes &&
	       a.ring_w_words == b.ring_w_words && a.ring_h == b.ring_h;
}

/// **Registro de motores** con su geometría (ver doc del fichero). Capacidad fija, sin heap.
template <class Backend, eng::u16 MaxEngines = 4u>
class ScrollLadder {
public:
	/// Registra un motor con la geometría que implementa. `false` si no cabe.
	/// \param layer  el motor (lo **posee** el juego).
	/// \param geom   geometría del anillo que implementa el motor.
	/// \return `false` si la escalera está llena.
	[[nodiscard]] bool add(eng::playfield::ScrollLayer<Backend>& layer,
			       const RuntimeScrollGeometry& geom) noexcept {
		if (m_count >= MaxEngines) {
			return false;
		}
		m_layers[m_count] = layer;
		m_geoms[m_count] = geom;
		++m_count;
		return true;
	}

	/// El motor cuya geometría **coincide** con `g` (mismo layout); `Ref` inválido si ninguno encaja.
	/// \param g  geometría cargada en runtime.
	/// \return el motor que la implementa, o `Ref` inválido.
	[[nodiscard]] eng::Ref<eng::playfield::ScrollLayer<Backend>>
	pick(const RuntimeScrollGeometry& g) noexcept {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			if (same_scroll_layout(m_geoms[i], g)) {
				return m_layers[i];
			}
		}
		return {};
	}

	[[nodiscard]] eng::u16 count() const noexcept { return m_count; }
	/// Geometría del motor `i` (para inspección).
	[[nodiscard]] const RuntimeScrollGeometry& geometry(eng::u16 i) const noexcept {
		return m_geoms[i];
	}

private:
	eng::Ref<eng::playfield::ScrollLayer<Backend>> m_layers[MaxEngines] {};
	RuntimeScrollGeometry m_geoms[MaxEngines] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::playfield
