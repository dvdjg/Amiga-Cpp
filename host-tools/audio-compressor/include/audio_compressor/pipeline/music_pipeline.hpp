#pragma once

/// Plan MUSIC host: timeline de eventos y serialización ACP1 separadas de la CLI.

#include <vector>
#include <cstring>
#include <algorithm>

#include <eng/audio/acp1.hpp>
#include <eng/core/types/types.hpp>

#include "../formats/acp1_writer.hpp"

namespace audio_compressor::pipeline {

/// Plan de pistas normalizadas que el writer ACP1 puede materializar.
struct MusicPlan {
	std::vector<std::vector<eng::u8>> payloads; ///< Unidades AUZX deduplicables por contenido.
	std::vector<std::vector<eng::audio::acp1::Event>> tracks; ///< Eventos secuenciales por pista.
	eng::u32 sample_rate = 0u; ///< Frecuencia común de todas las unidades.
	eng::u32 total_samples = 0u; ///< Extensión de la timeline en muestras.
};

/// Fachada del pipeline MUSIC que mantiene la timeline fuera del serializador ACP1.
struct MusicPipeline {
	static constexpr eng::u8 kPaulaDirectVoices = 3u; ///< AUD1..AUD3 disponibles para tracks directos.
	static constexpr eng::u8 kMixerVoices = 4u; ///< Voces software que se suman en AUD0.
	static constexpr eng::s8 kMixerMinimum = -32; ///< Límite inferior seguro por voz con cuatro sumandos.
	static constexpr eng::s8 kMixerMaximum = 31; ///< Límite superior seguro por voz con cuatro sumandos.
	struct Preflight {
		eng::u8 paula_tracks = 0u; ///< Tracks que requieren AUD1..AUD3.
		eng::u8 mixer_tracks = 0u; ///< Tracks que se sumarían en AUD0.
		eng::u64 estimated_chip_bytes = 0u; ///< Dos buffers PCM por voz directa.
		eng::u64 estimated_fast_bytes = 0u; ///< Stems y scratch host estimados.
	};

	/// Lee un stem por chunks intercalados y construye la timeline sin conservar el WAV completo.
	template <class Source, class Encode>
	[[nodiscard]] static bool build_windowed(Source& source, eng::u16 chunk_samples, bool paula_only,
		eng::u8 gain, Encode&& encode, MusicPlan& plan, std::vector<std::vector<eng::u8>>& unique_pcm_units) {
		if (source.channels() == 0u || source.channels() > eng::audio::acp1::kMaxTracks || chunk_samples == 0u ||
			(paula_only && source.channels() > kPaulaDirectVoices)) return false;
		plan.tracks.assign(source.channels(), {}); unique_pcm_units.clear();
		std::vector<eng::u8> window(chunk_samples);
		for (eng::u16 track = 0u; track < source.channels(); ++track) {
			for (eng::u64 start = 0u; start < source.frames(); start += chunk_samples) {
				const eng::usize count = static_cast<eng::usize>(std::min<eng::u64>(chunk_samples, source.frames() - start));
				if (source.read_channel(track, start, {window.data(), count}) != count) return false;
				std::vector<eng::u8> candidate(window.begin(), window.begin() + count);
				if (!paula_only && track >= kPaulaDirectVoices) {
					for (eng::u8& sample : candidate) sample = static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(static_cast<eng::s32>(static_cast<eng::s8>(sample)), -32, 31)));
				}
				eng::usize unit_id = 0u;
				for (; unit_id < unique_pcm_units.size(); ++unit_id) if (unique_pcm_units[unit_id] == candidate) break;
				if (unit_id == unique_pcm_units.size()) unique_pcm_units.push_back(std::move(candidate));
				plan.tracks[track].push_back({static_cast<eng::u32>(unit_id), static_cast<eng::u32>(start), static_cast<eng::u32>(count), gain});
			}
		}
		plan.payloads.resize(unique_pcm_units.size());
		for (eng::usize unit = 0u; unit < unique_pcm_units.size(); ++unit) if (!encode(unique_pcm_units[unit], plan.payloads[unit])) return false;
		return true;
	}

	/// Comprueba rutas físicas y estima buffers antes de serializar una obra.
	[[nodiscard]] static bool preflight(const std::vector<std::vector<eng::u8>>& stems,
		eng::u16 chunk_samples, bool paula_only, eng::u64 chip_budget, eng::u64 fast_budget,
		Preflight& result) {
		if (stems.empty() || chunk_samples == 0u || (paula_only && stems.size() > kPaulaDirectVoices) || stems.size() > 7u) return false;
		result.paula_tracks = static_cast<eng::u8>(paula_only ? stems.size() : std::min<std::size_t>(stems.size(), kPaulaDirectVoices));
		result.mixer_tracks = static_cast<eng::u8>(paula_only ? 0u : (stems.size() > kPaulaDirectVoices ? stems.size() - kPaulaDirectVoices : 0u));
		result.estimated_chip_bytes = static_cast<eng::u64>(result.paula_tracks) * chunk_samples * 2u + static_cast<eng::u64>(result.mixer_tracks != 0u) * chunk_samples * 2u;
		result.estimated_fast_bytes = 0u;
		for (const auto& stem : stems) result.estimated_fast_bytes += stem.size();
		return result.estimated_chip_bytes <= chip_budget && result.estimated_fast_bytes <= fast_budget;
	}

	/// Divide stems en unidades, deduplica PCM exacto y construye la timeline ACP1.
	[[nodiscard]] static bool build_plan(const std::vector<std::vector<eng::u8>>& stems,
		eng::u16 chunk_samples, eng::u8 gain, bool paula_only, MusicPlan& plan,
		std::vector<std::vector<eng::u8>>& unique_pcm_units) {
		if (stems.empty() || stems.size() > eng::audio::acp1::kMaxTracks || (paula_only && stems.size() > kPaulaDirectVoices) || chunk_samples == 0u) return false;
		plan.tracks.assign(stems.size(), {});
		unique_pcm_units.clear();
		for (eng::usize track = 0u; track < stems.size(); ++track) {
			for (eng::usize start = 0u; start < stems[track].size(); start += chunk_samples) {
				const eng::usize count = std::min<eng::usize>(chunk_samples, stems[track].size() - start);
				eng::usize unit_id = 0u;
				for (; unit_id < unique_pcm_units.size(); ++unit_id) {
					if (unique_pcm_units[unit_id].size() == count && std::memcmp(unique_pcm_units[unit_id].data(), stems[track].data() + start, count) == 0) break;
				}
				if (unit_id == unique_pcm_units.size()) {
					if (unit_id >= 65535u) return false;
					unique_pcm_units.emplace_back(stems[track].begin() + start, stems[track].begin() + start + count);
					if (!paula_only && track >= kPaulaDirectVoices) {
						for (eng::u8& sample : unique_pcm_units.back()) {
							const eng::s32 value = static_cast<eng::s8>(sample);
							sample = static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(value,
								static_cast<eng::s32>(kMixerMinimum), static_cast<eng::s32>(kMixerMaximum))));
						}
					}
				}
				plan.tracks[track].push_back({static_cast<eng::u32>(unit_id), static_cast<eng::u32>(start), static_cast<eng::u32>(count), gain});
			}
		}
		plan.payloads.resize(unique_pcm_units.size());
		return true;
	}

	/// Serializa el plan v2 y valida inmediatamente su parser freestanding.
	[[nodiscard]] static bool write_acp1_v2(const MusicPlan& plan, std::vector<eng::u8>& output) {
		return audio_compressor::build_acp1(plan.payloads, plan.sample_rate, plan.total_samples, output, plan.tracks);
	}
};

} // namespace audio_compressor::pipeline
