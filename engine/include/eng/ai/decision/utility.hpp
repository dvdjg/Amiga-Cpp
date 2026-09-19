#pragma once

/// \file utility.hpp
/// **Utility AI** (`eng::ai`): puntuar opciones de decisión con una media ponderada de
/// consideraciones normalizadas y elegir la mejor. Todo entero y determinista (sin
/// `float`), apto para el 68000.
///
/// - `Utility`: acumula consideraciones `(valor, peso)`. Cada valor está normalizado al
///   rango `[0, utility_scale]` (milésimas) y `score()` devuelve la media ponderada,
///   también en `[0, utility_scale]`.
/// - `UtilitySelector<MaxOptions>`: recoge las puntuaciones de las opciones de una
///   decisión y devuelve el índice de la mejor; ante empate gana el índice menor
///   (decisión reproducible).
///
/// Uso:
///   eng::ai::Utility u;
///   u.add(/*distancia*/ 800, /*peso*/ 2);   // cuanto más cerca, mejor
///   u.add(/*municion*/  200, /*peso*/ 1);
///   const eng::s32 ataque = u.score();
///   eng::ai::UtilitySelector<2> sel;
///   sel.add(ataque); sel.add(huida);
///   const eng::usize elegida = sel.best();
///
/// Referencia y encaje: `docs/engine/architecture/GAME_AI_LIBRARY.md`.
/// ```text
///   consideraciones (valor, peso)          Utility                  UtilitySelector<N>
///   ─────────────────────────────          ───────                  ──────────────────
///   distancia 800, peso 2 ─┐            [ Σ valor·peso / Σ peso ] ─► add(score) por opción
///   munición  200, peso 1 ─┴──────────► score() ∈ [0, 1000]      ─► best() = índice mayor
///                                        enteros, determinista        (empate → índice menor)
/// ```
///
/// Verificación: HOST-112.

#include <eng/core/types.hpp>
#include <eng/core/arith.hpp>

namespace eng::ai {

/// Escala de normalización: las consideraciones y la puntuación final van en [0, 1000].
inline constexpr eng::s32 utility_scale = 1000;

/// Media ponderada de consideraciones normalizadas.
class Utility {
public:
	constexpr void reset() noexcept {
		m_weighted = 0;
		m_weights = 0;
	}

	/// Añade una consideración. `value` se recorta a `[0, utility_scale]`; los pesos no
	/// positivos se ignoran (no ensucian la media).
	constexpr void add(eng::s32 value, eng::s32 weight) noexcept {
		if (value < 0) {
			value = 0;
		} else if (value > utility_scale) {
			value = utility_scale;
		}
		if (weight <= 0 || weight > 32767) {
			return; // pesos fuera de rango 16 bits no participan
		}
		// `muls.w`: valor y peso caben en s16 -> producto ensanchado sin `__mulsi3`.
		m_weighted += eng::math::mul_wide(static_cast<eng::s16>(value),
					       static_cast<eng::s16>(weight));
		m_weights += weight;
	}

	/// Puntuación en `[0, utility_scale]`; 0 si no hay consideraciones. Usa `div_wide`
	/// (`divs.w` nativo 32/16), de modo que no aparece `__divsi3`. La suma de pesos debe
	/// caber en `s16` (<= 32767).
	[[nodiscard]] constexpr eng::s32 score() const noexcept {
		if (m_weights <= 0) {
			return 0;
		}
		return eng::math::div_wide(m_weighted + m_weights / 2,
					static_cast<eng::s16>(m_weights));
	}

	[[nodiscard]] constexpr bool empty() const noexcept { return m_weights == 0; }

private:
	eng::s32 m_weighted = 0;
	eng::s32 m_weights = 0;
};

/// Selector de la mejor opción por puntuación (empate -> índice menor).
template <eng::usize MaxOptions>
class UtilitySelector {
	static_assert(MaxOptions > 0u, "UtilitySelector: MaxOptions debe ser mayor que 0");

public:
	static constexpr eng::usize no_option = static_cast<eng::usize>(-1);

	constexpr void reset() noexcept { m_count = 0u; }

	/// Añade la puntuación de una opción (en orden); `false` si ya no caben.
	constexpr bool add(eng::s32 score) noexcept {
		if (m_count >= MaxOptions) {
			return false;
		}
		m_scores[m_count] = score;
		++m_count;
		return true;
	}

	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_count; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0u; }
	[[nodiscard]] constexpr eng::s32 score(eng::usize option) const noexcept {
		return m_scores[option];
	}

	/// Índice de la opción con mayor puntuación; `no_option` si está vacío.
	[[nodiscard]] constexpr eng::usize best() const noexcept {
		if (m_count == 0u) {
			return no_option;
		}
		eng::usize best_i = 0u;
		for (eng::usize i = 1u; i < m_count; ++i) {
			if (m_scores[i] > m_scores[best_i]) {
				best_i = i;
			}
		}
		return best_i;
	}

private:
	eng::s32 m_scores[MaxOptions] {};
	eng::usize m_count = 0u;
};

} // namespace eng::ai
