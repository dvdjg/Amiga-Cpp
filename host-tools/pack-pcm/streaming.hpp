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
};

/// Contrato de fuente para el entrenamiento; puede leer WAV, RAW o un archivo ya decodificado.
template <class Source>
concept WindowSource = requires(Source& source, eng::u64 offset, eng::Span<eng::u8> window) {
	{ static_cast<eng::usize>(source.read(offset, window)) };
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

/// Devuelve true si `left` es mejor por menor tamaño y, en empate, menor error.
[[nodiscard]] constexpr bool better(const Metrics& left, const Metrics& right) noexcept {
	return left.encoded_bytes < right.encoded_bytes ||
		(left.encoded_bytes == right.encoded_bytes && left.squared_error < right.squared_error);
}

} // namespace pack_pcm
