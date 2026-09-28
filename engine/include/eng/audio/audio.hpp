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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Evento de sonido (sfx): reproduce una muestra en un canal de Paula.
struct SampleEvent {
	eng::AudioSample sample {};   // muestra 8-bit (Chip RAM) con su tamaño
	u16 length_words = 0;         // longitud en palabras
	u16 period = 0;               // período de Paula (determina la frecuencia)
	u8  volume = 0;               // 0..64
	u8  channel_hint = 0xff;      // canal preferido (0..3), 0xff = auto
};

/// Evento de música (tracker): reproducir o parar un módulo.
struct MusicEvent {
	eng::MusicBytes module {};   // módulo del tracker (p61/pt/ahx)
	bool play = false;
	bool stop = false;
};

/// Presupuesto de audio de un frame: voces DMA de Paula ocupadas y palabras que leerá el DMA.
/// Es a `AudioPlan` lo que `BlitBudget` a `FramePlan`: una estimación sencilla y verificable
/// (no ciclos exactos de bus) para telemetrizar y acotar el reparto.
struct AudioBudget {
	u8 voices = 0; ///< canales DMA activos (Paula: máx. 4)
	u32 words = 0; ///< palabras de audio sumadas (longitud de cada muestra)
};

/// Severidad del presupuesto de audio (mismos niveles que el de Blitter).
enum class AudioBudgetStatus : u8 { Ok, Warning, Exceeded };

/// Límites configurables del presupuesto de audio.
struct AudioBudgetLimits {
	u8 warning_voices = 4;
	u8 max_voices = 4;
	u32 warning_words = 0xffffffffu;
	u32 max_words = 0xffffffffu;
};

/// Informe derivado de comparar `AudioBudget` contra `AudioBudgetLimits`.
struct AudioBudgetReport {
	AudioBudgetStatus status = AudioBudgetStatus::Ok;
	bool voices_warning = false;
	bool voices_exceeded = false;
	bool words_warning = false;
	bool words_exceeded = false;
};

/// Plan de audio compilado: el estado de los 4 canales de Paula **y** su presupuesto. El plan es
/// portable (no escribe registros): el backend Amiga materializa los canales en `AUDxLCH/LCL/LEN/PER/VOL`
/// y la IRQ de Paula los avanza (el **feeder**).
struct AudioPlan {
	struct Channel {
		eng::AudioSample sample {};
		u16 length_words = 0;
		u16 period = 0;
		u8  volume = 0;
		bool active = false;
	};
	Channel channels[4] {};
	AudioBudget budget {};
	AudioBudgetLimits limits {};
	AudioBudgetReport report {};

	/// Fija los límites y recalcula el informe.
	void set_limits(AudioBudgetLimits l) {
		limits = l;
		rebuild_report();
	}

	/// Recalcula el informe del presupuesto (lo llama el mixer al repartir voces).
	void rebuild_report() {
		report = {};
		report.voices_warning = budget.voices > limits.warning_voices;
		report.voices_exceeded = budget.voices > limits.max_voices;
		report.words_warning = budget.words > limits.warning_words;
		report.words_exceeded = budget.words > limits.max_words;
		if (report.voices_exceeded || report.words_exceeded) {
			report.status = AudioBudgetStatus::Exceeded;
		} else if (report.voices_warning || report.words_warning) {
			report.status = AudioBudgetStatus::Warning;
		}
	}
};

/// Mezclador de Paula (4 canales). Dueño de la asignación de canales: recibe
/// `SampleEvent` y produce un `AudioPlan` con su presupuesto. La lógica es pura
/// (host-testable); el backend materializa el plan en registros Paula y su IRQ lo avanza.
class AudioMixer {
public:
	static constexpr u8 kChannels = 4;

	/// Limpia el plan (canales y presupuesto) al inicio del frame.
	void begin_frame() {
		for (u8 i = 0; i < kChannels; ++i) {
			m_plan.channels[i] = AudioPlan::Channel{};
		}
		m_plan.budget = {};
		m_plan.rebuild_report();
	}

	/// Añade un sfx al plan: usa el `channel_hint` si es válido, si no el primer
	/// canal libre. Devuelve `true` si cupo (falso si los 4 canales están ocupados).
	/// Actualiza el presupuesto (`voices`/`words`) con cada asignación.
	bool play(const SampleEvent& ev) {
		if (ev.sample.empty() || ev.length_words == 0) {
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
		m_plan.budget.voices = active_count();
		m_plan.budget.words += ev.length_words;
		m_plan.rebuild_report();
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
	AudioPlan& plan() { return m_plan; }
	const AudioBudgetReport& budget_report() const { return m_plan.report; }
	const AudioBudget& budget() const { return m_plan.budget; }

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
