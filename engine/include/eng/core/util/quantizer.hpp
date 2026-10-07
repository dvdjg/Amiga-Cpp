#pragma once

/// \file quantizer.hpp
/// Entrenamiento Lloyd-Max sin heap para tablas de cuantización.
///
/// El algoritmo recibe las muestras y el almacenamiento de centroides desde el consumidor. No
/// incluye un escalar concreto: funciona con tipos que soporten comparación, suma y `div_norm`.
/// La capacidad de la tabla viaja en `centroids`; el llamador controla memoria, número de niveles y
/// número de iteraciones, de modo que el mismo código sirve al entrenador PC y a una tabla pequeña
/// precalculada para Amiga. El tope de niveles es parámetro de plantilla (`MaxLevels`, 64 por
/// defecto), que dimensiona el scratch interno (`sums`/`counts`) sin heap.

#include <eng/core/math/scalar_ops.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

using eng::math::div_norm;
using eng::math::mul_norm;
using eng::math::scalar_traits;

/// Resultado del entrenamiento y distorsión media en las unidades del escalar `S`.
template <class S>
struct QuantizerResult {
	/// Indica si había muestras y centroides suficientes para entrenar.
	bool valid = false;
	/// Número de iteraciones realmente ejecutadas.
	eng::u16 iterations = 0u;
	/// Distorsión cuadrática media de la última asignación.
	S distortion = scalar_traits<S>::zero();
};

/// Entrena `centroids` con Lloyd-Max y devuelve la distorsión de la última iteración.
template <class S, eng::usize MaxLevels = 64u>
[[nodiscard]] QuantizerResult<S> lloyd_max(eng::Span<const S> samples, eng::Span<S> centroids,
	eng::u16 max_iterations) noexcept {
	QuantizerResult<S> result {};
	if (samples.empty() || centroids.empty() || max_iterations == 0u) return result;

	S minimum = samples[0];
	S maximum = samples[0];
	for (const S& sample : samples) {
		if (sample < minimum) minimum = sample;
		if (maximum < sample) maximum = sample;
	}
	const S levels = scalar_traits<S>::from_int(static_cast<int>(centroids.size() - 1u));
	for (eng::usize i = 0u; i < centroids.size(); ++i) {
		const S index = scalar_traits<S>::from_int(static_cast<int>(i));
		const S span = scalar_traits<S>::norm_from((maximum - minimum) * index);
		centroids[i] = minimum + div_norm(span, levels);
	}

	for (eng::u16 iteration = 0u; iteration < max_iterations; ++iteration) {
		// Los acumuladores y conteos se inicializan en cada ronda; no se reserva memoria.
		S sums[MaxLevels] {};
		eng::u32 counts[MaxLevels] {};
		if (centroids.size() > MaxLevels) return result;
		S distortion = scalar_traits<S>::zero();
		for (const S& sample : samples) {
			eng::usize nearest = 0u;
			S nearest_distance = sample - centroids[0];
			if (nearest_distance < scalar_traits<S>::zero()) nearest_distance = -nearest_distance;
			for (eng::usize i = 1u; i < centroids.size(); ++i) {
				S distance = sample - centroids[i];
				if (distance < scalar_traits<S>::zero()) distance = -distance;
				if (distance < nearest_distance) { nearest = i; nearest_distance = distance; }
			}
			sums[nearest] = sums[nearest] + sample;
			++counts[nearest];
			distortion = distortion + mul_norm(nearest_distance, nearest_distance);
		}
		for (eng::usize i = 0u; i < centroids.size(); ++i) {
			if (counts[i] != 0u) {
				centroids[i] = div_norm(sums[i], scalar_traits<S>::from_int(static_cast<int>(counts[i])));
			}
		}
		result.distortion = div_norm(distortion, scalar_traits<S>::from_int(static_cast<int>(samples.size())));
		result.iterations = static_cast<eng::u16>(iteration + 1u);
		result.valid = true;
	}
	return result;
}

} // namespace eng::util
