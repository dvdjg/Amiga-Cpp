#pragma once

/// \file sound_queue.hpp
/// **Vocabulario de sonido** del planner: la intención (`SoundIntent`), su cola (`SoundQueue`) y el
/// ejecutor que la vuelca al `AudioPlan` (`MixerExecutor`). El **mecanismo** de la cola (no
/// bloqueante + completación) es genérico y vive en `eng/core/util/intent_queue.hpp`; aquí solo está lo
/// específico de audio. El planner completo: `docs/engine/architecture/INTENT_PLANNER.md` §6
/// («el audio es un plan análogo»).

#include <eng/audio/audio.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/intent_queue.hpp>

namespace eng::audio {

/// Intención de sonido: el juego pide **qué** quiere (muestra, frecuencia, volumen, voz), sin
/// nombrar registros de Paula. El ejecutor la compila al `AudioPlan` (`AudioMixer::play`).
struct SoundIntent {
	eng::AudioSample sample {}; ///< muestra 8-bit (Chip RAM)
	u16 length_words = 0;       ///< longitud en palabras
	u16 period = 0;             ///< período de Paula (determina la frecuencia)
	u8 volume = 0;              ///< 0..64
	u8 voice = 0xff;            ///< voz preferida (0..3); 0xff = el ejecutor elige
};

/// Convierte la intención de sonido al evento que consume el mixer.
[[nodiscard]] constexpr SampleEvent sample_event_of(const SoundIntent& i) noexcept {
	return SampleEvent {i.sample, i.length_words, i.period, i.volume, i.voice};
}

/// **Ejecutor** intención→plan: vuelca cada `SoundIntent` al `AudioMixer` (el sumidero `AudioPlan`).
/// Es al plan de audio lo que `scene::SpritePlanExecutor` al `FramePlan`; apto como `Executor` de
/// una `IntentQueue` (`ready`/`run`).
class MixerExecutor {
public:
	/// Liga el mixer (el sumidero). No propietario; debe vivir más que la cola.
	constexpr explicit MixerExecutor(AudioMixer& mixer) noexcept : m_mixer(mixer) {}

	/// La vía admite trabajo siempre: el mixer decide si la petición cabe en el plan de 4 voces.
	[[nodiscard]] constexpr bool ready() const noexcept { return true; }

	/// Compila la intención al plan; `false` si no cupo (sin voz libre).
	bool run(const SoundIntent& item) noexcept { return m_mixer.play(sample_event_of(item)); }

private:
	AudioMixer& m_mixer;
};

/// La cola de sonido del juego: `Item = SoundIntent` (mecanismo genérico de `eng/core/util`).
template <eng::u16 N, class Executor, class Done>
using SoundQueue = eng::IntentQueue<N, SoundIntent, Executor, Done>;

} // namespace eng::audio
