#pragma once

/// \file stats.hpp
/// **EstadÃ­stica bÃ¡sica** genÃ©rica sobre una vista (`eng::util`): media, varianza,
/// desviaciÃ³n tÃ­pica, mediana/estadÃ­sticos de orden e histograma, mÃ¡s medias mÃ³viles
/// para telemetrÃ­a (fps, carga, fps suavizado).
///
/// Como el resto de la librerÃ­a de escalares, es **agnÃ³stica del tipo**: el mismo
/// cÃ³digo vale para `float`, `double`, `MiniFloat16` o un `Fixed`, usando `mul_norm`/
/// `div_norm` (el producto/divisiÃ³n normalizados del escalar). `div_norm` es la
/// divisiÃ³n **explÃ­cita** del engine: el `Fixed` del nÃºcleo no tiene `operator/` pero
/// sÃ­ `div_norm` (saturante), igual que `remap`/`inv_lerp`.
///
/// Coste y lÃ­mites:
/// - `mean`/`variance`/`histogram` **dividen** vÃ­a `div_norm`; con `Fixed` saturan y
///   con `MiniFloat16` llevan ~1e-3 de error.
/// - `stddev` exige ademÃ¡s `sqrt` del escalar (no existe para `Fixed`, por diseÃ±o).
/// - `kth_smallest`/`median` **mutan** el `scratch` que aporta el llamador (copian y
///   aplican `nth_element`): no asignan memoria.
/// - `ema`/`RunningMean` no dividen por el total mÃ¡s que una vez por muestra.

#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/ring_buffer.hpp>

#include <eng/core/linalg.hpp>
#include <eng/core/scalar_math.hpp>

namespace eng::util {

using eng::math::div_norm;
using eng::math::mul_norm;
using eng::math::scalar_sqrt;
using eng::math::scalar_traits;

/// Suma de todos los elementos (0 si la vista estÃ¡ vacÃ­a).
template <class S>
[[nodiscard]] constexpr S sum(Span<const S> xs) {
	S acc = scalar_traits<S>::zero();
	for (const S& x : xs) {
		acc = acc + x;
	}
	return acc;
}

/// Media aritmÃ©tica. Requiere divisiÃ³n en el escalar.
template <class S>
[[nodiscard]] constexpr S mean(Span<const S> xs) {
	if (xs.empty()) {
		return scalar_traits<S>::zero();
	}
	return div_norm(sum(xs), scalar_traits<S>::from_int(static_cast<int>(xs.size())));
}

/// Varianza **poblacional** en una sola pasada (Welford): estable numÃ©ricamente y sin
/// recorrer los datos dos veces. Requiere divisiÃ³n.
template <class S>
[[nodiscard]] constexpr S variance(Span<const S> xs) {
	S mu = scalar_traits<S>::zero();
	S m2 = scalar_traits<S>::zero();
	int n = 0;
	for (const S& x : xs) {
		++n;
		const S delta = x - mu;
		mu = mu + div_norm(delta, scalar_traits<S>::from_int(n));
		m2 = m2 + mul_norm(delta, x - mu);
	}
	if (n == 0) {
		return scalar_traits<S>::zero();
	}
	return div_norm(m2, scalar_traits<S>::from_int(n));
}

/// DesviaciÃ³n tÃ­pica (raÃ­z de `variance`). Requiere divisiÃ³n **y** `sqrt` del escalar
/// (no compila con `Fixed`).
template <class S>
[[nodiscard]] constexpr S stddev(Span<const S> xs) {
	return scalar_sqrt<S>::op(variance(xs));
}

/// k-Ã©simo menor (0 = mÃ­nimo). Copia `items` en `scratch` y aplica `nth_element`; si
/// `scratch` es menor, usa su tamaÃ±o. PrecondiciÃ³n: `scratch` no vacÃ­o.
template <class S>
constexpr S kth_smallest(Span<const S> items, Span<S> scratch, usize k) {
	const usize n = items.size() < scratch.size() ? items.size() : scratch.size();
	if (n == 0u) {
		return S {};
	}
	for (usize i = 0; i < n; ++i) {
		scratch[i] = items[i];
	}
	const usize idx = k < n ? k : (n - 1u);
	eng::nth_element(scratch.first(n), idx, [](const S& a, const S& b) { return a < b; });
	return scratch[idx];
}

/// Mediana (elemento central, ``n/2``; para `n` par devuelve el mayor de los dos
/// centrales, sin dividir). Muta `scratch`.
template <class S>
constexpr S median(Span<const S> items, Span<S> scratch) {
	const usize n = items.size() < scratch.size() ? items.size() : scratch.size();
	if (n == 0u) {
		return S {};
	}
	return kth_smallest(items, scratch, n / 2u);
}

/// Histograma por ancho fijo: cuenta en `counts[i]` los valores en
/// `[lo + iÂ·width, lo + (i+1)Â·width)`. Devuelve cuÃ¡ntos quedaron fuera (por debajo de
/// `lo` o mÃ¡s allÃ¡ del Ãºltimo cubo). Requiere divisiÃ³n.
template <class S>
constexpr u32 histogram(Span<const S> xs, Span<u32> counts, S lo, S width) {
	if (!(scalar_traits<S>::zero() < width) || counts.empty()) {
		return static_cast<u32>(xs.size());
	}
	u32 dropped = 0u;
	for (const S& x : xs) {
		if (x < lo) {
			++dropped;
			continue;
		}
		const int bucket = scalar_traits<S>::to_int(div_norm(x - lo, width));
		if (bucket < 0 || static_cast<usize>(bucket) >= counts.size()) {
			++dropped;
			continue;
		}
		++counts[static_cast<usize>(bucket)];
	}
	return dropped;
}

/// Media mÃ³vil exponencial: `prev + alphaÂ·(x âˆ’ prev)`. `alpha` en `[0,1]` (0 ignora la
/// muestra, 1 la toma entera). No divide (el llamador fija `alpha`). Base del suavizado
/// de fps/carga.
template <class S>
[[nodiscard]] constexpr S ema(S prev, S x, S alpha) {
	return prev + mul_norm(alpha, x - prev);
}

/// Media aritmÃ©tica de una **ventana** de las Ãºltimas `N` muestras (sin heap).
template <class S, usize N>
class RunningMean {
	static_assert(N > 0u, "RunningMean: N debe ser mayor que 0");

public:
	/// AÃ±ade `x` y devuelve la media de la ventana viva.
	constexpr S push(S x) {
			if (m_window.full()) {
			m_sum = m_sum - m_window.front();
			m_window.pop_discard();
		}
		m_window.push(x);
		m_sum = m_sum + x;
		return div_norm(m_sum,
				scalar_traits<S>::from_int(static_cast<int>(m_window.size())));
	}

	[[nodiscard]] constexpr usize size() const noexcept { return m_window.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_window.empty(); }
	constexpr void clear() noexcept {
		m_window.clear();
		m_sum = scalar_traits<S>::zero();
	}

private:
	RingBuffer<S, N> m_window {};
	S m_sum = scalar_traits<S>::zero();
};

} // namespace eng::util

