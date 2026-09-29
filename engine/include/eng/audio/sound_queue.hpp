#pragma once

/// \file sound_queue.hpp
/// **Cola de sonido** del planner: la cola (`SoundQueue`, sobre `SampleEvent`) y el ejecutor que la
/// vuelca al `AudioPlan` (`MixerExecutor`). El **mecanismo** de la cola (no bloqueante +
/// completación) es genérico y vive en `eng/core/util/intent_queue.hpp`; aquí solo está lo
/// específico de audio. El planner completo: `docs/engine/architecture/INTENT_PLANNER.md` §6
/// («el audio es un plan análogo»).
///
/// El **item** de la cola es `SampleEvent` (`audio.hpp`): es el mismo valor que consume el
/// `AudioMixer` (`play(const SampleEvent&)`), así que no hay dos descriptores de "qué suena" — hay
/// **uno** (la intención) y el estado materializado del plan (`AudioPlan::Channel`).

#include <eng/audio/audio.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/intent_queue.hpp>

namespace eng::audio {

/// **Ejecutor** intención→plan: vuelca cada `SampleEvent` al `AudioMixer` (el sumidero `AudioPlan`).
/// Es al plan de audio lo que `scene::SpritePlanExecutor` al `FramePlan`; apto como `Executor` de
/// una `IntentQueue` (`ready`/`run`).
class MixerExecutor {
public:
	/// Liga el mixer (el sumidero). No propietario; debe vivir más que la cola.
	constexpr explicit MixerExecutor(AudioMixer& mixer) noexcept : m_mixer(mixer) {}

	/// La vía admite trabajo siempre: el mixer decide si la petición cabe en el plan de 4 voces.
	[[nodiscard]] constexpr bool ready() const noexcept { return true; }

	/// Compila la intención al plan; `false` si no cupo (sin voz libre).
	bool run(const SampleEvent& item) noexcept { return m_mixer.play(item); }

private:
	AudioMixer& m_mixer;
};

/// La cola de sonido del juego: `Item = SampleEvent` (mecanismo genérico de `eng/core/util`).
template <eng::u16 N, class Executor, class Done>
using SoundQueue = eng::IntentQueue<N, SampleEvent, Executor, Done>;

} // namespace eng::audio
