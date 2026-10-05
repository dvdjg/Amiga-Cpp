#pragma once

/// Tipos de dominio host compartidos por fuentes, pipelines y reportes de audio.

#include <string>

#include <eng/core/types/types.hpp>
#include <eng/core/types/span.hpp>

#include <concepts>

namespace audio_compressor::domain {

/// Formato PCM normalizado que una fuente entrega al pipeline.
struct AudioFormat {
	eng::u32 sample_rate = 0u; ///< Frecuencia de muestreo en Hz.
	eng::u16 channels = 1u; ///< Número de canales de la fuente original.
	eng::u16 bits_per_sample = 8u; ///< Profundidad PCM original.
	bool signed_samples = true; ///< Indica si las muestras normalizadas usan signo.
};

/// Opciones comunes de la ruta SAMPLE.
struct SampleOptions {
	eng::u16 sample_rate = 0u; ///< Frecuencia objetivo; cero conserva la fuente.
	eng::u16 chunk_samples = 4096u; ///< Muestras descomprimidas por chunk AUZX.
	eng::u64 ram_budget_bytes = 6ull * 1024ull * 1024ull * 1024ull; ///< Presupuesto declarado.
	eng::usize window_samples = 64u * 1024u; ///< Scratch de ingestión por ventana.
	std::string codec = "rle"; ///< Nombre del codec AUZX solicitado.
};

/// Opciones de salida estructural MUSIC.
struct MusicOptions {
	eng::u8 acp1_version = 2u; ///< Versión ACP1 host que se genera.
	bool hpss = false; ///< Separa stems mediante HPSS antes de construir eventos.
};

/// Configuración resuelta de la aplicación CLI, compartida por pipelines y reportes.
struct ApplicationOptions : SampleOptions, MusicOptions {
	std::string mode = "auto"; ///< Modo solicitado: auto, sample o music.
	bool force = false; ///< Permite reemplazar una salida existente.
	bool dry_run = false; ///< Clasifica sin escribir una salida.
	bool play = false; ///< Reproduce la señal normalizada mediante el backend host.
	bool keep_candidates = false; ///< Conserva salidas lineales y candidatas.
	bool compare_candidates = false; ///< Imprime la comparación de codecs.
};

/// Métricas comunes de una conversión y de su round-trip.
struct ConversionReport {
	eng::u64 input_bytes = 0u; ///< Bytes leídos de la fuente.
	eng::u64 pcm_bytes = 0u; ///< Bytes PCM normalizados considerados.
	eng::u64 output_bytes = 0u; ///< Bytes de la salida principal.
	eng::u64 samples = 0u; ///< Muestras reconstruidas de la salida.
	eng::u16 sample_rate = 0u; ///< Frecuencia efectiva de la salida.
	eng::u64 squared_error = 0u; ///< Error cuadrático acumulado del round-trip.
	eng::u64 signal_energy = 0u; ///< Energía de la señal usada para el SNR.
	eng::u8 peak_error = 0u; ///< Error absoluto máximo por muestra.
	bool round_trip_ok = false; ///< Indica que todos los chunks se decodificaron.
	eng::u32 repeated_windows = 0u; ///< Ventanas idénticas encontradas en la señal.
};

/// Contrato estático de una fuente que puede alimentar un pipeline por ventanas.
template <class Source>
concept WindowSource = requires(Source& source, eng::u64 offset, eng::Span<eng::u8> window) {
	{ source.frames() } -> std::convertible_to<eng::u64>;
	{ source.format() } -> std::same_as<AudioFormat>;
	{ source.read(offset, window) } -> std::convertible_to<eng::usize>;
};

} // namespace audio_compressor::domain
