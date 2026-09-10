#pragma once

/// \file audio.hpp
/// Audio y música (paso 7 de ENGINE_DESIGN.md §5).
///
/// `SampleEvent`/`MusicEvent` son las intenciones de audio; `AudioMixer` las
/// compila a un `AudioPlan` (4 canales de Paula) con la lógica de asignación de
/// canales (pura, host-testable). El backend escribe los registros Paula
/// (AUDxLCH/LCL/LEN/PER/VOL) desde el plan, y el `MusicPlayer` (futuro) envuelve
/// los reproductores asm de `support/` (p61/pt/ahx, aún por importar de
/// demoscene-repo-orig).
///
/// Es puro (solo `eng::core`): host-testable. Sin heap, sin RTTI.

#include <eng/core/types.hpp>

namespace eng::audio {

/// Evento de sonido (sfx): reproduce una muestra en un canal de Paula.
struct SampleEvent {
	const u8* sample = nullptr;   // puntero a la muestra (Chip RAM)
	u16 length_words = 0;         // longitud en palabras
	u16 period = 0;               // período de Paula (determina la frecuencia)
	u8  volume = 0;               // 0..64
	u8  channel_hint = 0xff;      // canal preferido (0..3), 0xff = auto
};

/// Evento de música (tracker): reproducir o parar un módulo.
struct MusicEvent {
	const void* module = nullptr; // módulo del tracker (p61/pt/ahx)
	bool play = false;
	bool stop = false;
};

/// Plan de audio compilado: el estado de los 4 canales de Paula.
struct AudioPlan {
	struct Channel {
		const u8* sample = nullptr;
		u16 length_words = 0;
		u16 period = 0;
		u8  volume = 0;
		bool active = false;
	};
	Channel channels[4] {};
};

/// Mezclador de Paula (4 canales). Dueño de la asignación de canales: recibe
/// `SampleEvent` y produce un `AudioPlan`. La lógica es pura (host-testable); el
/// backend materializa el plan en registros Paula.
class AudioMixer {
public:
	static constexpr u8 kChannels = 4;

	/// Limpia el plan al inicio del frame.
	void begin_frame() {
		for (u8 i = 0; i < kChannels; ++i) {
			m_plan.channels[i] = AudioPlan::Channel{};
		}
	}

	/// Añade un sfx al plan: usa el `channel_hint` si es válido, si no el primer
	/// canal libre. Devuelve `true` si cupo (falso si los 4 canales están ocupados).
	bool play(const SampleEvent& ev) {
		if (ev.sample == nullptr || ev.length_words == 0) {
			return false;
		}
		u8 channel = ev.channel_hint;
		if (channel >= kChannels) {
			channel = find_free();
		}
		if (channel >= kChannels) {
			return false; // sin canal libre
		}
		m_plan.channels[channel] = AudioPlan::Channel{ev.sample, ev.length_words, ev.period, ev.volume, true};
		return true;
	}

	/// Número de canales activos (para telemetría).
	u8 active_count() const {
		u8 n = 0;
		for (u8 i = 0; i < kChannels; ++i) {
			if (m_plan.channels[i].active) {
				++n;
			}
		}
		return n;
	}

	const AudioPlan& plan() const { return m_plan; }

private:
	u8 find_free() const {
		for (u8 i = 0; i < kChannels; ++i) {
			if (!m_plan.channels[i].active) {
				return i;
			}
		}
		return 0xff;
	}

	AudioPlan m_plan {};
};

} // namespace eng::audio
