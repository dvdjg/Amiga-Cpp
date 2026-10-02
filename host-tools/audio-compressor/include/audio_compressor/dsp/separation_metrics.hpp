#pragma once

/// Métricas host de recomposición y correlación entre pistas separadas.

#include <algorithm>
#include <cmath>
#include <vector>

#include <eng/core/types/types.hpp>

namespace audio_compressor::dsp {

/// Resultado de comparar una señal reconstruida con la mezcla normalizada.
struct ReconstructionMetrics {
	double mse = 0.0; ///< Error cuadrático medio PCM8.
	double snr_db = 0.0; ///< SNR respecto a la energía de la referencia.
	eng::u8 peak_error = 0u; ///< Error absoluto máximo en una muestra.
};

/// Calcula MSE, SNR y pico sin afirmar que la mezcla identifique instrumentos correctamente.
[[nodiscard]] inline ReconstructionMetrics reconstruction_metrics(const std::vector<eng::u8>& reference,
	const std::vector<eng::u8>& reconstructed) noexcept {
	const eng::usize count = std::min(reference.size(), reconstructed.size());
	eng::u64 energy = 0u, squared_error = 0u;
	eng::u8 peak = 0u;
	for (eng::usize i = 0u; i < count; ++i) {
		const eng::s32 source = static_cast<eng::s8>(reference[i]);
		const eng::s32 error = source - static_cast<eng::s8>(reconstructed[i]);
		energy += static_cast<eng::u64>(source * source); squared_error += static_cast<eng::u64>(error * error);
		const eng::u32 absolute = error < 0 ? -error : error; if (absolute > peak) peak = absolute;
	}
	const double mse = count == 0u ? 0.0 : static_cast<double>(squared_error) / count;
	const double snr = squared_error == 0u ? 99.0 : 10.0 * std::log10(static_cast<double>(energy) / squared_error);
	return {mse, snr, peak};
}

/// Devuelve la mayor correlación normalizada entre dos pistas; no sustituye stems de referencia.
[[nodiscard]] inline double cross_track_leakage_db(const std::vector<std::vector<eng::u8>>& tracks) noexcept {
	double worst = 0.0;
	for (eng::usize left = 0u; left < tracks.size(); ++left) for (eng::usize right = left + 1u; right < tracks.size(); ++right) {
		const eng::usize count = std::min(tracks[left].size(), tracks[right].size());
		double dot = 0.0, left_energy = 0.0, right_energy = 0.0;
		for (eng::usize i = 0u; i < count; ++i) {
			const double a = static_cast<eng::s8>(tracks[left][i]); const double b = static_cast<eng::s8>(tracks[right][i]);
			dot += a * b; left_energy += a * a; right_energy += b * b;
		}
		if (left_energy > 0.0 && right_energy > 0.0) worst = std::max(worst, std::abs(dot) / std::sqrt(left_energy * right_energy));
	}
	return worst == 0.0 ? -99.0 : 20.0 * std::log10(std::min(1.0, worst));
}

} // namespace audio_compressor::dsp
