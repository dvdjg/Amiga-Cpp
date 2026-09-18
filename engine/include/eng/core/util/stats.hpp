#pragma once

/// \file stats.hpp
/// **Estadística básica** genérica sobre una vista (`eng::util`): media, varianza,
/// desviación típica, mediana/estadísticos de orden e histograma, más medias móviles
/// para telemetría (fps, carga, fps suavizado).
///
/// Como el resto de la librería de escalares, es **agnóstica del tipo**: el mismo
/// código vale para `float`, `double`, `MiniFloat16` o un `Fixed`, usando `mul_norm`/
/// `div_norm` (el producto/división normalizados del escalar). `div_norm` es la
/// división **explícita** del engine: el `Fixed` del núcleo no tiene `operator/` pero
/// sí `div_norm` (saturante), igual que `remap`/`inv_lerp`.
///
/// Coste y límites:
/// - `mean`/`variance`/`histogram` **dividen** vía `div_norm`; con `Fixed` saturan y
///   con `MiniFloat16` llevan ~1e-3 de error.
/// - `stddev` exige además `sqrt` del escalar (no existe para `Fixed`, por diseño).
/// - `kth_smallest`/`median` **mutan** el `scratch` que aporta el llamador (copian y
///   aplican `nth_element`): no asignan memoria.
/// - `ema`/`RunningMean` no dividen por el total más que una vez por muestra.

#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/ring_buffer.hpp>

#include <eng/core/linalg.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/arith.hpp>

namespace eng::util {

using eng::math::div_norm;
using eng::math::mul_norm;
using eng::math::scalar_sqrt;
using eng::math::scalar_traits;

namespace detail {

/// ¿`S` es un `Fixed<s16,…>`? En ese caso `sum`/`mean` acumulan en **32 bits** (`add.l`)
/// y solo estrechan al final, para no saturar al sumar muchos valores pequeños.
template <class S>
struct is_fixed_s16 {
	static constexpr bool value = false;
};
template <int E, typename P>
struct is_fixed_s16<eng::math::Fixed<eng::s16, E, P>> {
	static constexpr bool value = true;
};

[[nodiscard]] constexpr s16 sat_s16(s32 v) noexcept {
	if (v > 32767) return static_cast<s16>(32767);
	if (v < -32768) return static_cast<s16>(-32768);
	return static_cast<s16>(v);
}

} // namespace detail

/// Suma de todos los elementos (0 si la vista está vacía). Con `Fixed<s16>` el
/// acumulador es **s32** (`add.l`) y solo el resultado se estrecha a `s16`.
template <class S>
[[nodiscard]] constexpr S sum(Span<const S> xs) {
	if constexpr (detail::is_fixed_s16<S>::value) {
		s32 acc = 0;
		for (const S& x : xs) {
			acc += static_cast<s32>(x.v);
		}
		return S {detail::sat_s16(acc)};
	} else {
		S acc = scalar_traits<S>::zero();
		for (const S& x : xs) {
			acc = acc + x;
		}
		return acc;
	}
}

/// Media aritmética. Con `Fixed<s16>` suma en **s32** y divide con `div_wide` (`divs.w`),
/// así la suma intermedia no satura; el tamaño de la vista debe caber en `s16`.
template <class S>
[[nodiscard]] constexpr S mean(Span<const S> xs) {
	if (xs.empty()) {
		return scalar_traits<S>::zero();
	}
	if constexpr (detail::is_fixed_s16<S>::value) {
		s32 acc = 0;
		for (const S& x : xs) {
			acc += static_cast<s32>(x.v);
		}
		return S {eng::math::div_wide(acc, static_cast<s16>(xs.size()))};
	} else {
		return div_norm(sum(xs), scalar_traits<S>::from_int(static_cast<int>(xs.size())));
	}
}

/// Varianza **poblacional** en una sola pasada (Welford): estable numéricamente y sin
/// recorrer los datos dos veces. Con `Fixed<s16>` acumula en el propio escalar (una
/// varianza con acumulador ancho necesitaría productos de 64 bits — `__muldi3` —, que
/// no se quieren en 68000): usar valores dentro del rango del fixed.
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

/// Desviación típica (raíz de `variance`). Con `Fixed` requiere incluir
/// `eng/core/fixed_math.hpp` (aporta `scalar_sqrt<Fixed>` vía `isqrt`).
template <class S>
[[nodiscard]] constexpr S stddev(Span<const S> xs) {
	return scalar_sqrt<S>::op(variance(xs));
}

/// k-ésimo menor (0 = mínimo). Copia `items` en `scratch` y aplica `nth_element`; si
/// `scratch` es menor, usa su tamaño. Precondición: `scratch` no vacío.
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
/// `[lo + i·width, lo + (i+1)·width)`. Devuelve cuántos quedaron fuera (por debajo de
/// `lo` o más allá del último cubo). Requiere división.
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

/// Media móvil exponencial: `prev + alpha·(x − prev)`. `alpha` en `[0,1]` (0 ignora la
/// muestra, 1 la toma entera). No divide (el llamador fija `alpha`). Base del suavizado
/// de fps/carga.
template <class S>
[[nodiscard]] constexpr S ema(S prev, S x, S alpha) {
	return prev + mul_norm(alpha, x - prev);
}

/// Media aritmética de una **ventana** de las últimas `N` muestras (sin heap).
template <class S, usize N>
class RunningMean {
	static_assert(N > 0u, "RunningMean: N debe ser mayor que 0");

public:
	/// Añade `x` y devuelve la media de la ventana viva.
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

