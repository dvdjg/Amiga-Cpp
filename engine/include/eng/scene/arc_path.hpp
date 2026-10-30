#pragma once

/// \file arc_path.hpp
/// **Tabla de longitud de arco** para recorrer una polilínea a velocidad
/// constante, con posiciones Q8 (px<<8) y muestreo subpíxel.
///
/// El llamador llena la tabla con muestras de cualquier spline/polilínea; a
/// partir de ahí `sample(distance_q8)` devuelve una posición interpolada. Es
/// genérica sobre la capacidad y el escalar de coordenada; no posee memoria
/// dinámica.
///
/// Uso típico de cámara/actor:
///   ArcPath<512> path;
///   for (muestras) path.push({x_q8, y_q8});
///   path.sample(distancia_q8, pos_q8); // avanza kPxPorFrame<<8 por frame
///
/// \tparam Capacity número máximo de muestras (>= 2).
/// \tparam S escalar de las coordenadas Q8 (por defecto s32).

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene {

/// \tparam Capacity número máximo de muestras (>= 2).
/// \tparam S escalar de las coordenadas Q8 (por defecto s32).
template <u16 Capacity, class S = eng::s32>
class ArcPath {
public:
	static_assert(Capacity >= 2u, "ArcPath necesita al menos dos muestras");

	/// Vacía la tabla.
	constexpr void clear() noexcept {
		m_count = 0u;
		m_total_q8 = 0u;
	}

	/// Añade una muestra Q8. Devuelve `false` si la tabla está llena.
	/// \param p posición en Q8 (px<<8).
	[[nodiscard]] bool push(eng::math::Vec<2, S> p) noexcept {
		if (m_count >= Capacity) return false;
		if (m_count != 0u) {
			const eng::math::Vec<2, S> prev = m_points[m_count - 1u];
			const eng::s32 dx = static_cast<eng::s32>(p.v[0] - prev.v[0]) >> 8;
			const eng::s32 dy = static_cast<eng::s32>(p.v[1] - prev.v[1]) >> 8;
			m_cum_q8[m_count] = m_total_q8 +
				(isqrt_u32(static_cast<eng::u32>(dx * dx + dy * dy)) << 8);
			m_total_q8 = m_cum_q8[m_count];
		} else {
			m_cum_q8[0] = 0u;
		}
		m_points[m_count] = p;
		++m_count;
		return true;
	}

	/// Longitud total de la polilínea en Q8 (px<<8).
	[[nodiscard]] constexpr eng::u32 total_q8() const noexcept { return m_total_q8; }
	/// Número de muestras cargadas.
	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }
	/// ¿Hay al menos dos muestras y longitud no nula?
	[[nodiscard]] constexpr bool valid() const noexcept {
		return m_count >= 2u && m_total_q8 != 0u;
	}

	/// Muestrea a una distancia Q8 (se recorta a `[0,total]`). La interpolación
	/// es lineal entre muestras; con muestras densas el error es subpíxel.
	/// \param distance_q8 distancia recorrida en Q8.
	/// \param out posición interpolada en Q8.
	/// \return `false` si la tabla no tiene recorrido.
	[[nodiscard]] bool sample(eng::u32 distance_q8, eng::math::Vec<2, S>& out) const noexcept {
		if (!valid()) {
			out = m_count != 0u ? m_points[0] : eng::math::Vec<2, S> {};
			return false;
		}
		if (distance_q8 >= m_total_q8) {
			out = m_points[m_count - 1u];
			return true;
		}
		u16 lo = 0u;
		u16 hi = static_cast<u16>(m_count - 1u);
		while (lo + 1u < hi) {
			const u16 mid = static_cast<u16>((lo + hi) >> 1u);
			if (m_cum_q8[mid] <= distance_q8) lo = mid;
			else hi = mid;
		}
		const u32 seg = m_cum_q8[lo + 1u] - m_cum_q8[lo];
		if (seg == 0u) {
			out = m_points[lo];
			return true;
		}
		const u32 frac = distance_q8 - m_cum_q8[lo];
		const eng::math::Vec<2, S> d = m_points[lo + 1u] - m_points[lo];
		// Fracción normalizada a 1/256 del tramo: evita `delta_q8 * frac` en
		// s32 cuando la tabla es gruesa (dos muestras a cientos de px).
		const u32 frac8 = (frac << 8u) / seg;
		out.v[0] = m_points[lo].v[0] +
			   static_cast<S>((static_cast<eng::s32>(d.v[0]) * static_cast<eng::s32>(frac8)) >> 8u);
		out.v[1] = m_points[lo].v[1] +
			   static_cast<S>((static_cast<eng::s32>(d.v[1]) * static_cast<eng::s32>(frac8)) >> 8u);
		return true;
	}

private:
	/// Raíz cuadrada entera exacta (floor) por búsqueda binaria de bits.
	/// `eng::isqrt` aproxima (593 para 360000); la longitud de arco no puede
	/// permitirse ese error porque acumula y desplaza el bucle.
	[[nodiscard]] static constexpr eng::u32 isqrt_u32(eng::u32 n) noexcept {
		eng::u32 result = 0u;
		eng::u32 bit = 1u << 30u;
		while (bit > n) bit >>= 2u;
		while (bit != 0u) {
			if (n >= result + bit) {
				n -= result + bit;
				result = (result >> 1u) + bit;
			} else {
				result >>= 1u;
			}
			bit >>= 2u;
		}
		return result;
	}

	eng::math::Vec<2, S> m_points[Capacity] {};
	eng::u32 m_cum_q8[Capacity] {};
	eng::u16 m_count = 0u;
	eng::u32 m_total_q8 = 0u;
};

} // namespace eng::scene
