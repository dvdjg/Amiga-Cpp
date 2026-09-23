#pragma once

/// \file interval.hpp
/// `eng::util::Interval` e `IntervalSet<N>`: **rangos enteros** semiabiertos `[lo, hi)` y
/// un conjunto de intervalos **disjuntos y ordenados** de capacidad fija, sin heap.
/// Sirve para rangos de nivel (streaming), ventanas temporales (buffs/daño) y tramos de
/// animación: `add` fusiona los intervalos que solapan o son adyacentes y `contains` los
/// consulta por búsqueda binaria.
///
/// Uso:
///   eng::util::IntervalSet<8> visible;
///   visible.add(cam_min, cam_max);
///   if (visible.contains(tile_x)) { ... }
///
/// Verificación: HOST-129.

#include <eng/core/types/types.hpp>

namespace eng::util {

/// Intervalo semiabierto `[lo, hi)`. Vacío si `hi <= lo`.
struct Interval {
	eng::s32 lo = 0;
	eng::s32 hi = 0;

	[[nodiscard]] constexpr bool empty() const noexcept { return hi <= lo; }
	[[nodiscard]] constexpr eng::s32 length() const noexcept { return hi > lo ? hi - lo : 0; }
	[[nodiscard]] constexpr bool contains(eng::s32 x) const noexcept {
		return x >= lo && x < hi;
	}
};

/// ¿Solapan `a` y `b`? (semiabiertos: tocarse por un extremo NO es solapar).
[[nodiscard]] constexpr bool interval_overlaps(Interval a, Interval b) noexcept {
	return a.lo < b.hi && b.lo < a.hi;
}

/// Conjunto de intervalos disjuntos, ordenados y **fusionados** (sin solapes ni
/// adyacencias). Capacidad `N`.
template <eng::u16 N>
class IntervalSet {
	static_assert(N > 0u, "IntervalSet: N debe ser mayor que 0");

public:
	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return N; }
	[[nodiscard]] constexpr eng::u16 size() const noexcept { return m_count; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0u; }
	[[nodiscard]] constexpr Interval at(eng::u16 i) const noexcept { return m_intervals[i]; }

	constexpr void clear() noexcept { m_count = 0u; }

	/// Añade `[lo, hi)`, fusionándolo con los intervalos que solapan o son adyacentes.
	/// Un rango vacío no hace nada. `false` si está lleno y no cabría el intervalo nuevo.
	constexpr bool add(eng::s32 lo, eng::s32 hi) noexcept {
		if (hi <= lo) {
			return true;
		}
		eng::u16 i = 0u;
		while (i < m_count && m_intervals[i].hi < lo) {
			++i; // intervalos estrictamente a la izquierda (hi < lo)
		}
		eng::s32 new_lo = lo;
		eng::s32 new_hi = hi;
		eng::u16 j = i;
		while (j < m_count && m_intervals[j].lo <= new_hi) {
			if (m_intervals[j].lo < new_lo) {
				new_lo = m_intervals[j].lo;
			}
			if (m_intervals[j].hi > new_hi) {
				new_hi = m_intervals[j].hi;
			}
			++j;
		}
		const eng::u16 absorbed = static_cast<eng::u16>(j - i);
		if (absorbed == 0u) {
			// Intervalo nuevo sin solape: insertar (desplazando el resto).
			if (m_count >= N) {
				return false;
			}
			for (eng::u16 k = m_count; k > i; --k) {
				m_intervals[k] = m_intervals[k - 1u];
			}
			m_intervals[i] = Interval {new_lo, new_hi};
			++m_count;
			return true;
		}
		// Al menos uno absorbido: se escriben las nuevas cotas en `i` y se compacta.
		m_intervals[i] = Interval {new_lo, new_hi};
		eng::u16 k = static_cast<eng::u16>(i + 1u);
		for (eng::u16 t = j; t < m_count; ++t) {
			m_intervals[k++] = m_intervals[t];
		}
		m_count = k;
		return true;
	}

	/// ¿Pertenece `x` a algún intervalo? (búsqueda binaria).
	[[nodiscard]] constexpr bool contains(eng::s32 x) const noexcept {
		eng::u16 lo_i = 0u;
		eng::u16 hi_i = m_count;
		while (lo_i < hi_i) {
			const eng::u16 mid = static_cast<eng::u16>((lo_i + hi_i) / 2u);
			if (m_intervals[mid].hi <= x) {
				lo_i = static_cast<eng::u16>(mid + 1u);
			} else {
				hi_i = mid;
			}
		}
		return lo_i < m_count && m_intervals[lo_i].lo <= x;
	}

private:
	Interval m_intervals[N] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::util
