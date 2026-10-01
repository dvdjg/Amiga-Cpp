#pragma once

/// Plan MUSIC host: timeline de eventos y serialización ACP1 separadas de la CLI.

#include <vector>
#include <cstring>

#include <eng/audio/acp1.hpp>
#include <eng/core/types/types.hpp>

#include <audio_compressor/formats/acp1_writer.hpp>

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
	/// Divide stems en unidades, deduplica PCM exacto y construye la timeline ACP1.
	[[nodiscard]] static bool build_plan(const std::vector<std::vector<eng::u8>>& stems,
		eng::u16 chunk_samples, eng::u8 gain, MusicPlan& plan,
		std::vector<std::vector<eng::u8>>& unique_pcm_units) {
		if (stems.empty() || stems.size() > eng::audio::acp1::kMaxTracks || chunk_samples == 0u) return false;
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
