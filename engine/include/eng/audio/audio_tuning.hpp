#pragma once

/// \file audio_tuning.hpp
/// Parámetros y métricas de entrenamiento de audio independientes de la representación.
///
/// El codec host puede usar `float` para explorar rápidamente o `eng::math::Fixed` para obtener
/// resultados deterministas y reutilizables en el runtime. Esta cabecera no incluye ningún escalar
/// concreto: el consumidor aporta `S` y el engine resuelve operaciones mediante `scalar_traits`,
/// `mul_norm` y `div_norm`.

#include <eng/core/math/scalar_ops.hpp>
#include <eng/core/types/span.hpp>

namespace eng::audio {

using eng::math::div_norm;
using eng::math::mul_norm;
using eng::math::scalar_traits;

/// Parámetros normalizados que pueden recorrer el entrenador offline y el decoder.
template <class S>
struct AudioTuning {
	/// Coeficiente del filtro de preénfasis, normalmente 0.85..0.95.
	S pre_emphasis = scalar_traits<S>::zero();
	/// Realimentación de noise shaping, normalmente 0..0.5.
	S noise_shaping = scalar_traits<S>::zero();
	/// Umbral de energía armónica para habilitar el modo tonal, normalmente 0..1.
	S harmonic_threshold = scalar_traits<S>::from_int(1);
	/// Peso relativo del error frente al tamaño en la función de coste.
	S quality_weight = scalar_traits<S>::from_int(1);
};

/// Métricas de error normalizadas para una ventana reconstruida.
template <class S>
struct AudioErrorMetrics {
	/// Error cuadrático medio de la ventana.
	S mean_square = scalar_traits<S>::zero();
	/// Energía media de la señal original.
	S signal_energy = scalar_traits<S>::zero();
	/// Máximo error absoluto observado.
	S peak_error = scalar_traits<S>::zero();
};

/// Calcula MSE, energía y pico con aritmética del escalar `S`.
template <class S>
[[nodiscard]] constexpr AudioErrorMetrics<S> error_metrics(eng::Span<const S> original,
	eng::Span<const S> rebuilt) noexcept {
	AudioErrorMetrics<S> result {};
	const eng::usize count = original.size() < rebuilt.size() ? original.size() : rebuilt.size();
	if (count == 0u) return result;
	S error_sum = scalar_traits<S>::zero();
	S signal_sum = scalar_traits<S>::zero();
	for (eng::usize i = 0u; i < count; ++i) {
		const S error = original[i] - rebuilt[i];
		const S absolute = error < scalar_traits<S>::zero() ? -error : error;
		error_sum = error_sum + mul_norm(error, error);
		signal_sum = signal_sum + mul_norm(original[i], original[i]);
		if (result.peak_error < absolute) result.peak_error = absolute;
	}
	const S denominator = scalar_traits<S>::from_int(static_cast<int>(count));
	result.mean_square = div_norm(error_sum, denominator);
	result.signal_energy = div_norm(signal_sum, denominator);
	return result;
}

/// Calcula un coste escalar: tamaño normalizado más error ponderado por `quality_weight`.
template <class S>
[[nodiscard]] constexpr S cost(S normalized_size, const AudioErrorMetrics<S>& metrics,
	const AudioTuning<S>& tuning) noexcept {
	return normalized_size + mul_norm(tuning.quality_weight, metrics.mean_square);
}

} // namespace eng::audio
