#pragma once

/// \file hpss.hpp
/// Separación host-only armónica/percusiva por STFT con máscaras complementarias.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include <eng/core/types/types.hpp>
#include "../include/audio_compressor/domain/audio_types.hpp"

namespace audio_compressor {

/// Resultado PCM8 firmado de las capas armónica y percusiva.
struct HpssResult {
	std::vector<eng::u8> harmonic; ///< Componente sostenida reconstruida a PCM8 firmado.
	std::vector<eng::u8> percussive; ///< Componente transitoria reconstruida a PCM8 firmado.
};

/// Scratch reutilizable de HPSS: evita reservar FFT, medianas y acumuladores en cada ventana.
struct HpssWorkspace {
	std::vector<float> window; ///< Ventana Hann de análisis.
	std::vector<std::complex<float>> spectrum; ///< Espectro completo de las ventanas.
	std::vector<std::complex<float>> frame; ///< Frame FFT temporal.
	std::vector<float> magnitude; ///< Magnitud por tiempo y frecuencia.
	std::vector<float> harmonic_median; ///< Mediana temporal para la máscara armónica.
	std::vector<float> percussive_median; ///< Mediana frecuencial para la máscara percusiva.
	std::vector<float> harmonic_weight; ///< Peso armónico por bin.
	std::vector<float> harmonic; ///< Acumulador armónico overlap-add.
	std::vector<float> percussive; ///< Acumulador percusivo overlap-add.
	std::vector<float> normalization; ///< Peso acumulado de overlap-add.
	std::vector<std::complex<float>> inverse; ///< Buffer FFT inversa.
};

/// FFT radix-2 in-place; `inverse` aplica la normalización 1/N en la salida.
inline void fft(std::vector<std::complex<float>>& values, bool inverse) {
	const eng::usize count = values.size();
	for (eng::usize i = 1u, j = 0u; i < count; ++i) {
		eng::usize bit = count >> 1u;
		for (; (j & bit) != 0u; bit >>= 1u) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(values[i], values[j]);
	}
	constexpr float pi = 3.14159265358979323846f;
	for (eng::usize length = 2u; length <= count; length <<= 1u) {
		const float angle = (inverse ? 2.0f : -2.0f) * pi / static_cast<float>(length);
		const std::complex<float> step {std::cos(angle), std::sin(angle)};
		for (eng::usize first = 0u; first < count; first += length) {
			std::complex<float> phase {1.0f, 0.0f};
			for (eng::usize offset = 0u; offset < length / 2u; ++offset) {
				const auto even = values[first + offset];
				const auto odd = values[first + offset + length / 2u] * phase;
				values[first + offset] = even + odd;
				values[first + offset + length / 2u] = even - odd;
				phase *= step;
			}
		}
	}
	if (inverse) for (auto& value : values) value /= static_cast<float>(count);
}

/// Separates PCM8 into harmonic/percussive layers; unsupported sizes return `false` unchanged.
/// `fft_size` must be a power of two >= 32; hop is fixed at one quarter window.
[[nodiscard]] inline bool hpss_into(const std::vector<eng::u8>& pcm, eng::u16 fft_size, HpssWorkspace& workspace, HpssResult& out) {
	if (pcm.empty() || fft_size < 32u || (fft_size & (fft_size - 1u)) != 0u) return false;
	const eng::usize n = fft_size;
	const eng::usize bins = n / 2u + 1u;
	const eng::usize hop = n / 4u;
	const eng::usize frames = (pcm.size() + hop - 1u) / hop + 1u;
	std::vector<float>& window = workspace.window;
	window.assign(n, 0.0f);
	constexpr float pi = 3.14159265358979323846f;
	for (eng::usize i = 0u; i < n; ++i) window[i] = 0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(i) / static_cast<float>(n - 1u));
	std::vector<std::complex<float>>& spectrum = workspace.spectrum;
	std::vector<std::complex<float>>& frame = workspace.frame;
	spectrum.assign(frames * bins, {}); frame.assign(n, {});
	for (eng::usize t = 0u; t < frames; ++t) {
		const eng::s32 center = static_cast<eng::s32>(t * hop);
		for (eng::usize i = 0u; i < n; ++i) {
			const eng::s32 source = center + static_cast<eng::s32>(i) - static_cast<eng::s32>(n / 2u);
			const float sample = source < 0 || static_cast<eng::usize>(source) >= pcm.size()
				? 0.0f : static_cast<float>(static_cast<eng::s8>(pcm[static_cast<eng::usize>(source)]));
			frame[i] = {sample * window[i], 0.0f};
		}
		fft(frame, false);
		for (eng::usize b = 0u; b < bins; ++b) spectrum[t * bins + b] = frame[b];
	}
	std::vector<float>& magnitude = workspace.magnitude;
	std::vector<float>& harmonic_median = workspace.harmonic_median;
	std::vector<float>& percussive_median = workspace.percussive_median;
	magnitude.assign(frames * bins, 0.0f); harmonic_median.assign(frames * bins, 0.0f); percussive_median.assign(frames * bins, 0.0f);
	for (eng::usize i = 0u; i < magnitude.size(); ++i) magnitude[i] = std::abs(spectrum[i]);
	float window_values[5] {};
	for (eng::usize t = 0u; t < frames; ++t) for (eng::usize b = 0u; b < bins; ++b) {
		for (eng::s32 k = -2; k <= 2; ++k) {
			const eng::usize ti = static_cast<eng::usize>(std::clamp<eng::s32>(static_cast<eng::s32>(t) + k, 0, static_cast<eng::s32>(frames - 1u)));
			window_values[k + 2] = magnitude[ti * bins + b];
		}
		std::sort(window_values, window_values + 5);
		harmonic_median[t * bins + b] = window_values[2];
		for (eng::s32 k = -2; k <= 2; ++k) {
			const eng::usize bi = static_cast<eng::usize>(std::clamp<eng::s32>(static_cast<eng::s32>(b) + k, 0, static_cast<eng::s32>(bins - 1u)));
			window_values[k + 2] = magnitude[t * bins + bi];
		}
		std::sort(window_values, window_values + 5);
		percussive_median[t * bins + b] = window_values[2];
	}
	std::vector<float>& harmonic_weight = workspace.harmonic_weight;
	harmonic_weight.assign(frames * bins, 0.0f);
	for (eng::usize i = 0u; i < harmonic_weight.size(); ++i) {
		const float h = harmonic_median[i] * harmonic_median[i];
		const float p = percussive_median[i] * percussive_median[i];
		harmonic_weight[i] = h / (h + p + 1.0e-12f);
	}
	std::vector<float>& harmonic = workspace.harmonic;
	std::vector<float>& percussive = workspace.percussive;
	std::vector<float>& normalization = workspace.normalization;
	harmonic.assign(pcm.size(), 0.0f); percussive.assign(pcm.size(), 0.0f); normalization.assign(pcm.size(), 0.0f);
	std::vector<std::complex<float>>& inverse = workspace.inverse;
	inverse.assign(n, {});
	for (eng::usize t = 0u; t < frames; ++t) {
		for (eng::usize i = 0u; i < n; ++i) {
			const eng::usize b = i <= n / 2u ? i : n - i;
			const float weight = harmonic_weight[t * bins + b];
			const auto value = spectrum[t * bins + b];
			inverse[i] = value * weight;
		}
		fft(inverse, true);
		const eng::s32 center = static_cast<eng::s32>(t * hop);
		for (eng::usize i = 0u; i < n; ++i) {
			const eng::s32 destination = center + static_cast<eng::s32>(i) - static_cast<eng::s32>(n / 2u);
			if (destination < 0 || static_cast<eng::usize>(destination) >= pcm.size()) continue;
			const eng::usize d = static_cast<eng::usize>(destination);
			const float scale = window[i];
			harmonic[d] += inverse[i].real() * scale;
			normalization[d] += scale * scale;
		}
		for (eng::usize i = 0u; i < n; ++i) {
			const eng::usize b = i <= n / 2u ? i : n - i;
			const float weight = 1.0f - harmonic_weight[t * bins + b];
			inverse[i] = spectrum[t * bins + b] * weight;
		}
		fft(inverse, true);
		for (eng::usize i = 0u; i < n; ++i) {
			const eng::s32 destination = center + static_cast<eng::s32>(i) - static_cast<eng::s32>(n / 2u);
			if (destination < 0 || static_cast<eng::usize>(destination) >= pcm.size()) continue;
			percussive[static_cast<eng::usize>(destination)] += inverse[i].real() * window[i];
		}
	}
	out.harmonic.resize(pcm.size()); out.percussive.resize(pcm.size());
	for (eng::usize i = 0u; i < pcm.size(); ++i) {
		const float divisor = normalization[i] > 1.0e-12f ? normalization[i] : 1.0f;
		const float h = harmonic[i] / divisor;
		const float p = percussive[i] / divisor;
		const auto quantize = [](float value) {
			const int rounded = static_cast<int>(std::lround(value));
			return static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(rounded, -128, 127)));
		};
		out.harmonic[i] = quantize(h);
		out.percussive[i] = quantize(p);
	}
	return true;
}

/// Ejecuta HPSS con scratch local; la variante `hpss_into` permite reutilizarlo entre ventanas.
[[nodiscard]] inline bool hpss(const std::vector<eng::u8>& pcm, eng::u16 fft_size, HpssResult& out) {
	HpssWorkspace workspace {};
	return hpss_into(pcm, fft_size, workspace, out);
}

/// Procesa una fuente por ventanas con solapamiento explícito y entrega cada par de capas al consumidor.
/// La política de unión de bordes pertenece al callback; la función limita el scratch al tamaño pedido.
template <audio_compressor::domain::WindowSource Source, class Consumer>
[[nodiscard]] bool hpss_windowed(Source& source, eng::usize window_samples, eng::usize overlap_samples,
	eng::u16 fft_size, Consumer&& consumer) {
	if (window_samples == 0u || overlap_samples >= window_samples || source.frames() == 0u) return false;
	const eng::usize step = window_samples - overlap_samples;
	std::vector<eng::u8> input(window_samples + overlap_samples);
	for (eng::u64 start = 0u; start < source.frames(); start += step) {
		const eng::usize count = static_cast<eng::usize>(std::min<eng::u64>(input.size(), source.frames() - start));
		if (source.read(start, {input.data(), count}) != count) return false;
		input.resize(count);
		HpssResult layers {};
		if (!hpss(input, fft_size, layers) || !consumer(start, layers)) return false;
		input.resize(window_samples + overlap_samples);
	}
	return true;
}

/// Procesa un PCM completo por ventanas y une las capas solapadas con pesos lineales.
/// Se usa mientras la ingestión PACK-PCM siga entregando cada stem completo; limita el scratch de
/// FFT a una ventana y evita discontinuidades audibles entre llamadas independientes a HPSS.
[[nodiscard]] inline bool hpss_windowed_pcm(const std::vector<eng::u8>& pcm, eng::usize window_samples,
	eng::usize overlap_samples, eng::u16 fft_size, HpssResult& out) {
	if (pcm.empty() || window_samples == 0u || overlap_samples >= window_samples || overlap_samples == 0u) return false;
	std::vector<float> harmonic(pcm.size(), 0.0f), percussive(pcm.size(), 0.0f), weights(pcm.size(), 0.0f);
	std::vector<eng::u8> window(window_samples + overlap_samples);
	HpssWorkspace workspace {};
	for (eng::usize start = 0u; start < pcm.size(); start += window_samples) {
		const eng::usize count = std::min<eng::usize>(window_samples + overlap_samples, pcm.size() - start);
		std::memcpy(window.data(), pcm.data() + start, count);
		window.resize(count);
		HpssResult layers {};
		if (!hpss_into(window, fft_size, workspace, layers)) return false;
		const bool first = start == 0u;
		const bool last = start + count == pcm.size();
		for (eng::usize i = 0u; i < count; ++i) {
			float weight = 1.0f;
			if (!first && i < overlap_samples) weight = static_cast<float>(i) / static_cast<float>(overlap_samples);
			if (!last && count - i <= overlap_samples) {
				const float right = static_cast<float>(count - i - 1u) / static_cast<float>(overlap_samples);
				weight = std::min(weight, right);
			}
			const eng::usize destination = start + i;
			harmonic[destination] += static_cast<float>(static_cast<eng::s8>(layers.harmonic[i])) * weight;
			percussive[destination] += static_cast<float>(static_cast<eng::s8>(layers.percussive[i])) * weight;
			weights[destination] += weight;
		}
		window.resize(window_samples + overlap_samples);
	}
	out.harmonic.resize(pcm.size()); out.percussive.resize(pcm.size());
	for (eng::usize i = 0u; i < pcm.size(); ++i) {
		const float divisor = weights[i] > 1.0e-6f ? weights[i] : 1.0f;
		const auto quantize = [](float value) {
			return static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(static_cast<int>(std::lround(value)), -128, 127)));
		};
		out.harmonic[i] = quantize(harmonic[i] / divisor);
		out.percussive[i] = quantize(percussive[i] / divisor);
	}
	return true;
}

} // namespace audio_compressor
