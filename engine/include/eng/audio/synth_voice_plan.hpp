#pragma once

/// Planificador host-testable de voces de síntesis: Paula directa, mixer o fallback OctaMED.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Ruta solicitada por una pista de síntesis antes de conocer la ocupación efectiva.
enum class SynthRoute : eng::u8 { Auto = 0u, PreferPaula = 1u, PreferMixer = 2u, PaulaRequired = 3u, MixerRequired = 4u, OctaMED = 5u };

/// Intervalo audible de una pista durante el primer recorrido de la timeline.
struct SynthInterval {
	eng::u32 start_sample = 0u; ///< Inicio inclusivo en muestras.
	eng::u32 end_sample = 0u; ///< Fin exclusivo en muestras.
};

/// Petición de una pista y sus restricciones de destino.
struct SynthTrackRequest {
	SynthRoute route = SynthRoute::Auto; ///< Preferencia o requisito de backend.
	SynthInterval interval {}; ///< Ventana audible usada para detectar solapes.
};

/// Resultado de planificar una obra de síntesis.
enum class SynthPlanStatus : eng::u8 { Direct = 0u, NeedsOctaMED = 1u, Rejected = 2u };

/// Asignación física lógica de una pista; Paula usa índices 1..3 y mixer 0..3.
struct SynthTrackAssignment {
	SynthRoute route = SynthRoute::Auto; ///< Backend finalmente elegido.
	eng::u8 voice = 0u; ///< Índice dentro del backend elegido.
};

/// Asigna pistas no solapadas a las tres voces Paula y cuatro voces mixer.
[[nodiscard]] inline SynthPlanStatus plan_synth_tracks(eng::Span<const SynthTrackRequest> requests,
	eng::Span<SynthTrackAssignment> assignments, bool allow_octamed) noexcept {
	if (requests.empty() || assignments.size() < requests.size() || requests.size() > 8u) return SynthPlanStatus::Rejected;
	SynthInterval paula_intervals[3] {};
	SynthInterval mixer_intervals[4] {};
	bool paula_active[3] {};
	bool mixer_active[4] {};
	for (eng::usize i = 0u; i < requests.size(); ++i) {
		const SynthTrackRequest& request = requests[i];
		if (request.interval.end_sample <= request.interval.start_sample) return SynthPlanStatus::Rejected;
		bool placed = false;
		const bool mixer_first = request.route == SynthRoute::PreferMixer || request.route == SynthRoute::MixerRequired;
		const bool paula_only = request.route == SynthRoute::PaulaRequired;
		const bool mixer_only = request.route == SynthRoute::MixerRequired;
		// Intenta colocar la pista en una de las tres voces Paula sin solape; false si no cabe.
		const auto try_paula = [&]() {
			if (mixer_only) return false;
			for (eng::u8 voice = 0u; voice < 3u; ++voice) {
				if (paula_active[voice] && !(request.interval.end_sample <= paula_intervals[voice].start_sample ||
					paula_intervals[voice].end_sample <= request.interval.start_sample)) continue;
				paula_intervals[voice] = request.interval; paula_active[voice] = true;
				assignments[i] = {SynthRoute::PaulaRequired, voice}; placed = true; return true;
			}
			return false;
		};
		// Intenta colocar la pista en una de las cuatro voces mixer sin solape; false si no cabe.
		const auto try_mixer = [&]() {
			if (paula_only) return false;
			for (eng::u8 voice = 0u; voice < 4u; ++voice) {
				if (mixer_active[voice] && !(request.interval.end_sample <= mixer_intervals[voice].start_sample ||
					mixer_intervals[voice].end_sample <= request.interval.start_sample)) continue;
				mixer_intervals[voice] = request.interval; mixer_active[voice] = true;
				assignments[i] = {SynthRoute::MixerRequired, voice}; placed = true; return true;
			}
			return false;
		};
		if (mixer_first) { (void)try_mixer(); if (!placed) (void)try_paula(); }
		else { (void)try_paula(); if (!placed) (void)try_mixer(); }
		if (!placed) {
			if (request.route == SynthRoute::PaulaRequired || request.route == SynthRoute::MixerRequired || !allow_octamed) return SynthPlanStatus::Rejected;
			return SynthPlanStatus::NeedsOctaMED;
		}
	}
	return SynthPlanStatus::Direct;
}

} // namespace eng::audio
