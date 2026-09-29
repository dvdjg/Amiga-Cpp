#pragma once

/// \file streaming.hpp
/// Evaluación host por ventanas para entrenar parámetros sin retener todo el audio.
///
/// El consumidor aporta una fuente con `read(offset, Span<u8>)`, que rellena la ventana y devuelve
/// cuántas muestras contiene. El evaluador reutiliza un único scratch y acumula solo métricas y
/// tamaño; por tanto el coste de RAM es `window_samples`, no la duración del audio.

#include <cstddef>

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace pack_pcm {

/// Presupuesto host para una pasada de entrenamiento por ventanas.
struct TrainingConfig {
	/// Presupuesto máximo declarado para scratch, candidatos y buffers auxiliares.
	eng::u64 ram_budget_bytes = 6ull * 1024ull * 1024ull * 1024ull;
	/// Tamaño de la ventana que una fuente debe mantener disponible.
	eng::usize window_samples = 64u * 1024u;
	/// Número máximo de candidatos evaluados en una pasada.
	eng::u16 max_candidates = 64u;
};

/// Comprueba que la ventana cabe en el presupuesto host declarado.
[[nodiscard]] constexpr bool fits_budget(const TrainingConfig& config) noexcept {
	return config.window_samples != 0u && config.ram_budget_bytes >=
		static_cast<eng::u64>(config.window_samples);
}

/// Parámetros de un candidato que el encoder puede comparar por ventana.
struct Candidate {
	/// Identificador del codec AUZX.
	eng::u8 codec = 2u;
	/// Tamaño de chunk que se simula, en muestras.
	eng::u16 chunk_samples = 4096u;
	/// Bits de cuantización reservados para codecs futuros.
	eng::u8 quant_bits = 8u;
};

/// Métricas acumuladas de una pasada sobre el audio.
struct Metrics {
	/// Muestras procesadas, sin contar buffers no llenos.
	eng::u64 samples = 0u;
	/// Suma de errores cuadrados respecto a la reconstrucción.
	eng::u64 squared_error = 0u;
	/// Error absoluto máximo observado.
	eng::u8 peak_error = 0u;
	/// Bytes comprimidos estimados por el encoder candidato.
	eng::u64 encoded_bytes = 0u;
	/// Error cuadrático medio escalado por 256 para conservar precisión sin float.
	eng::u32 mse_x256 = 0u;
	/// SNR aproximada escalada por 256; cero si la señal no tiene energía.
	eng::u32 snr_db_x256 = 0u;
};

/// Contrato de fuente para el entrenamiento; puede leer WAV, RAW o un archivo ya decodificado.
template <class Source>
concept WindowSource = requires(Source& source, eng::u64 offset, eng::Span<eng::u8> window) {
	{ static_cast<eng::usize>(source.read(offset, window)) };
};

/// Contrato de un codec host que permite medir tamaño y reconstrucción de una ventana.
template <class Encoder>
concept RoundTripEncoder = requires(Encoder& encoder, const Candidate& candidate,
	eng::Span<const eng::u8> samples, eng::Span<eng::u8> encoded,
	eng::Span<eng::u8> reconstructed) {
	{ static_cast<eng::usize>(encoder.encode(candidate, samples, encoded)) };
	{ static_cast<eng::usize>(encoder.decode(candidate, encoded, reconstructed)) };
};

/// Evalúa el tamaño de un candidato con una ventana reutilizada y devuelve false ante una lectura
/// inválida. El adaptador `Encoder` debe ofrecer `encode(candidate, samples)` y puede actualizar
/// sus propias métricas perceptuales; esta función no inventa un error de reconstrucción cuando
/// el codec no ha proporcionado una señal decodificada.
template <WindowSource Source, class Encoder>
[[nodiscard]] bool evaluate(Source& source, Encoder& encoder, const Candidate& candidate,
	eng::Span<eng::u8> scratch, Metrics& metrics) {
	if (scratch.empty()) return false;
	eng::u64 offset = 0u;
	for (;;) {
		const eng::usize count = static_cast<eng::usize>(source.read(offset, scratch));
		if (count == 0u) break;
		if (count > scratch.size()) return false;
		const eng::usize encoded = encoder.encode(candidate, eng::Span<const eng::u8>{scratch.data(), count});
		if (encoded == 0u) return false;
		metrics.samples += count;
		metrics.encoded_bytes += encoded;
		offset += count;
	}
	return metrics.samples != 0u;
}

/// Evalúa tamaño y reconstrucción de todas las ventanas de un candidato.
template <WindowSource Source, RoundTripEncoder Encoder>
[[nodiscard]] bool evaluate_round_trip(Source& source, Encoder& encoder, const Candidate& candidate,
	eng::Span<eng::u8> scratch, eng::Span<eng::u8> encoded, eng::Span<eng::u8> reconstructed,
	Metrics& metrics) {
	if (scratch.empty() || encoded.empty() || reconstructed.size() < scratch.size()) return false;
	eng::u64 offset = 0u;
	eng::u64 signal_energy = 0u;
	eng::u64 error_energy = 0u;
	for (;;) {
		const eng::usize count = static_cast<eng::usize>(source.read(offset, scratch));
		if (count == 0u) break;
		if (count > scratch.size()) return false;
		const eng::usize packed = static_cast<eng::usize>(encoder.encode(
			candidate, eng::Span<const eng::u8>{scratch.data(), count}, encoded));
		const eng::usize decoded = static_cast<eng::usize>(encoder.decode(
			candidate, eng::Span<const eng::u8>{encoded.data(), packed}, reconstructed));
		if (packed == 0u || decoded != count) return false;
		for (eng::usize i = 0u; i < count; ++i) {
			const eng::s32 original = static_cast<eng::s8>(scratch[i]);
			const eng::s32 rebuilt = static_cast<eng::s8>(reconstructed[i]);
			const eng::s32 error = original - rebuilt;
			const eng::u32 absolute = static_cast<eng::u32>(error < 0 ? -error : error);
			error_energy += static_cast<eng::u64>(error * error);
			signal_energy += static_cast<eng::u64>(original * original);
			if (absolute > metrics.peak_error) metrics.peak_error = static_cast<eng::u8>(absolute);
		}
		metrics.samples += count;
		metrics.encoded_bytes += packed;
		offset += count;
	}
	if (metrics.samples == 0u) return false;
	metrics.squared_error = error_energy;
	metrics.mse_x256 = static_cast<eng::u32>((error_energy * 256u) / metrics.samples);
	metrics.snr_db_x256 = error_energy == 0u ? 0xffffffffu :
		static_cast<eng::u32>((signal_energy * 256u) / error_energy);
	return true;
}

/// Devuelve true si `left` es mejor por menor tamaño y, en empate, menor error.
[[nodiscard]] constexpr bool better(const Metrics& left, const Metrics& right) noexcept {
	return left.encoded_bytes < right.encoded_bytes ||
		(left.encoded_bytes == right.encoded_bytes && left.squared_error < right.squared_error);
}

/// Busca el candidato con menor tamaño y, en empate, menor error reconstruido.
template <WindowSource Source, RoundTripEncoder Encoder>
[[nodiscard]] bool search(Source& source, Encoder& encoder, eng::Span<const Candidate> candidates,
	eng::Span<eng::u8> scratch, eng::Span<eng::u8> encoded, eng::Span<eng::u8> reconstructed,
	Candidate& best_candidate, Metrics& best_metrics) {
	bool found = false;
	for (eng::usize i = 0u; i < candidates.size(); ++i) {
		Metrics current {};
		if (!evaluate_round_trip(source, encoder, candidates[i], scratch, encoded, reconstructed, current)) return false;
		if (!found || better(current, best_metrics)) {
			best_candidate = candidates[i]; best_metrics = current; found = true;
		}
	}
	return found;
}

} // namespace pack_pcm
