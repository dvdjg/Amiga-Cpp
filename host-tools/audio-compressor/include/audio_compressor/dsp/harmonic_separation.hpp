#pragma once

/// Separador host experimental: estima candidatos F0 y parciales para síntesis ACP1 por ventanas.

#include <algorithm>
#include <cmath>
#include <vector>

#include <eng/audio/synth_renderer.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::dsp {

/// Nota estimada por el analizador armónico; los tiempos están en muestras.
struct HarmonicNote {
	eng::u64 start_sample = 0u; ///< Inicio de la ventana detectada.
	eng::u32 duration = 0u; ///< Duración de la ventana.
	eng::s16 pitch_semitones_q8_8 = 0; ///< Pitch relativo al F0 base de la pista.
	eng::u16 gain_q8_8 = 0u; ///< Ganancia estimada de la nota.
};

/// Modelo instrumental estimado: un timbre de parciales y una secuencia temporal de notas.
struct HarmonicTrackModel {
	eng::u32 fundamental_hz_q16_16 = 0u; ///< F0 de referencia de la pista.
	std::vector<eng::audio::SynthPartial> partials; ///< Parciales normalizados del timbre.
	std::vector<HarmonicNote> notes; ///< Notas detectadas en ventanas sucesivas.
};

/// Parámetros de la búsqueda armónica host; la memoria crece solo con los modelos resultantes.
struct HarmonicSeparationOptions {
	eng::u16 window_samples = 4096u; ///< Tamaño de ventana temporal.
	eng::u8 max_tracks = 3u; ///< Máximo de candidatos instrumentales conservados.
	eng::u16 min_hz = 80u; ///< Límite inferior de F0.
	eng::u16 max_hz = 1200u; ///< Límite superior de F0.
	eng::u16 frequency_step_hz = 4u; ///< Paso de la rejilla inicial de candidatos.
	eng::u8 partials = 8u; ///< Parciales analizados por candidato.
};

/// Estima modelos instrumentales por DFT directa de parciales y conserva las mejores hipótesis.
[[nodiscard]] inline bool separate_harmonic(const std::vector<eng::u8>& pcm, eng::u32 sample_rate,
	const HarmonicSeparationOptions& options, std::vector<HarmonicTrackModel>& output) {
	if (pcm.empty() || sample_rate == 0u || options.window_samples < 64u || options.max_tracks == 0u ||
		options.partials == 0u || options.frequency_step_hz == 0u || options.min_hz >= options.max_hz) return false;
	struct Candidate { double frequency = 0.0; double energy = 0.0; std::vector<double> amplitudes; };
	const eng::usize count = std::min<eng::usize>(options.window_samples, pcm.size());
	const auto sample = [&](eng::usize index) { return static_cast<double>(static_cast<eng::s8>(pcm[index])); };
	std::vector<Candidate> candidates;
	for (eng::u32 frequency = options.min_hz; frequency <= options.max_hz; frequency += options.frequency_step_hz) {
		Candidate candidate {}; candidate.frequency = frequency;
		const eng::usize lag = std::max<eng::usize>(1u, std::lround(static_cast<double>(sample_rate) / frequency));
		double correlation = 0.0, energy = 0.0;
		for (eng::usize i = 0u; i + lag < count; ++i) { correlation += sample(i) * sample(i + lag); energy += sample(i) * sample(i); }
		candidate.energy = energy > 1.0 ? (correlation * correlation) / energy : 0.0;
		candidates.push_back(std::move(candidate));
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) { return left.energy > right.energy; });
	output.clear();
	for (Candidate& candidate : candidates) {
		bool separated = true;
		for (const HarmonicTrackModel& selected : output) {
			const double selected_hz = selected.fundamental_hz_q16_16 / 65536.0;
			if (std::abs(candidate.frequency - selected_hz) < 40.0) { separated = false; break; }
		}
		if (!separated) continue;
		candidate.amplitudes.assign(options.partials, 0.0);
		for (eng::u8 partial = 1u; partial <= options.partials; ++partial) {
			const double frequency_hz = candidate.frequency * partial;
			if (frequency_hz >= sample_rate * 0.5) break;
			double real = 0.0, imaginary = 0.0;
			for (eng::usize i = 0u; i < count; ++i) {
				const double angle = 6.28318530717958647692 * frequency_hz * i / sample_rate;
				real += sample(i) * std::cos(angle); imaginary -= sample(i) * std::sin(angle);
			}
			candidate.amplitudes[partial - 1u] = 2.0 * std::sqrt(real * real + imaginary * imaginary) / count;
		}
		HarmonicTrackModel model {};
		model.fundamental_hz_q16_16 = static_cast<eng::u32>(candidate.frequency * 65536.0);
		const double peak = std::max(1.0, candidate.amplitudes.front());
		for (eng::usize i = 0u; i < candidate.amplitudes.size(); ++i) {
			const eng::s32 amplitude = static_cast<eng::s32>(std::lround(std::clamp(candidate.amplitudes[i] / peak, -1.0, 1.0) * 32767.0));
			model.partials.push_back({static_cast<eng::u16>((i + 1u) * 256u), static_cast<eng::s16>(amplitude), 0u});
		}
		const double gain = std::clamp(peak / 127.0, 0.0, 1.0);
		model.notes.push_back({0u, static_cast<eng::u32>(count), 0, static_cast<eng::u16>(std::lround(gain * 256.0))});
		output.push_back(std::move(model));
		if (output.size() >= options.max_tracks) break;
	}
	return !output.empty();
}

/// Repite el análisis por ventanas y mantiene la identidad de las mejores pistas mediante F0.
[[nodiscard]] inline bool separate_harmonic_windowed(const std::vector<eng::u8>& pcm, eng::u32 sample_rate,
	const HarmonicSeparationOptions& options, std::vector<HarmonicTrackModel>& output) {
	if (pcm.empty() || options.window_samples == 0u) return false;
	output.clear();
	for (eng::usize start = 0u; start < pcm.size(); start += options.window_samples) {
		const eng::usize count = std::min<eng::usize>(options.window_samples, pcm.size() - start);
		std::vector<eng::u8> window(pcm.begin() + start, pcm.begin() + start + count);
		std::vector<HarmonicTrackModel> candidates;
		if (!separate_harmonic(window, sample_rate, options, candidates)) continue;
		if (output.empty()) { output = std::move(candidates); continue; }
		std::vector<bool> used(output.size(), false);
		for (HarmonicTrackModel& candidate : candidates) {
			eng::usize best = output.size(); double best_distance = 1.0e9;
			for (eng::usize track = 0u; track < output.size(); ++track) {
				if (used[track]) continue;
				const double base = output[track].fundamental_hz_q16_16 / 65536.0;
				const double frequency = candidate.fundamental_hz_q16_16 / 65536.0;
				const double distance = std::abs(std::log2(std::max(1.0, frequency / base)) * 12.0);
				if (distance < best_distance) { best_distance = distance; best = track; }
			}
			if (best == output.size() || best_distance > 3.0) continue;
			used[best] = true;
			for (HarmonicNote note : candidate.notes) {
				const double ratio = (candidate.fundamental_hz_q16_16 / 65536.0) /
					(output[best].fundamental_hz_q16_16 / 65536.0);
				note.start_sample += start;
				note.pitch_semitones_q8_8 = static_cast<eng::s16>(std::lround(std::log2(std::max(1.0, ratio)) * 12.0 * 256.0));
				output[best].notes.push_back(note);
			}
		}
	}
	output.erase(std::remove_if(output.begin(), output.end(), [](const HarmonicTrackModel& track) { return track.notes.empty(); }), output.end());
	return !output.empty();
}

} // namespace audio_compressor::dsp
