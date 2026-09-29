#pragma once

/// \file sound_planner.hpp
/// **Planner de sonido**: une la **intención** (`SoundQueue`), la **vía** (`MixerExecutor` →
/// `AudioPlan`) y la **completación** (`IntentDonePoster` → `Msg`) en un único objeto, más el
/// reporte de **flanco** de `AudioUnderrun`. Es el audio como «plan análogo» del planner
/// (`INTENT_PLANNER.md` §6): el juego `declare()` sin bloquear; `flush()` (en el bucle, **no** en
/// la ISR) drena al plan y postea los mensajes. Ver también `GAME_AUDIO.md` §8 y
/// `tests/host/audio/373_sound_planner`.
///
/// Separa las tres preocupaciones que antes vivían sueltas: la **cola** (`eng/core/util`), el
/// **plan** (`AudioMixer`/`AudioPlan`) y el **aviso** (`eng/os`). El feeder por IRQ de Paula
/// (`AudioFeeder`) avanza el DMA; este planner gobierna el reparto de voces del frame.

#include <eng/audio/audio.hpp>
#include <eng/audio/audio_events.hpp>
#include <eng/audio/sound_queue.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/os/intent_done.hpp>
#include <eng/os/message.hpp>
#include <eng/os/port.hpp>

namespace eng::audio {

/// **Planner de sonido** con capacidad de cola `N` y de puerto `PortN`.
///
/// - `begin_frame()` limpia el plan (canales + presupuesto).
/// - `declare(intent)` encola una intención (no bloquea) y devuelve su `Ticket`.
/// - `flush()` drena la cola al `AudioPlan` (postea `IntentDone` por petición) y reporta el flanco
///   de `AudioUnderrun` (una vez por evento, no por buffer).
template <eng::u16 N = 8u, eng::u16 PortN = 32u>
class SoundPlanner {
	static_assert((N & (N - 1u)) == 0u, "SoundPlanner: N potencia de dos");

public:
	/// Liga el mixer (el sumidero `AudioPlan`) y el puerto al que postear las completaciones.
	/// No propietario; ambos deben vivir más que el planner.
	SoundPlanner(AudioMixer& mixer, eng::os::MsgPort<PortN>& port) noexcept
		: m_mixer(mixer), m_exec(mixer), m_port(&port) {
		m_queue.bind(m_exec);
		m_queue.bind_done(eng::os::IntentDonePoster<PortN> {&port});
	}

	/// Limpia el plan del frame (canales y presupuesto).
	void begin_frame() noexcept { m_mixer.begin_frame(); }

	/// **Declara** un sonido (no bloquea). Devuelve el ticket de la petición.
	[[nodiscard]] Ticket declare(const SoundIntent& item) noexcept { return m_queue.enqueue(item); }

	/// Drena la cola al `AudioPlan` (postea `IntentDone` por petición ejecutada) y reporta el flanco
	/// de `AudioUnderrun`. Llamar una vez por frame desde el **bucle** (no la ISR).
	void flush() noexcept {
		m_queue.flush();
		const AudioMsgOut out = m_edges.on_tick(false, m_underrun_now);
		m_underrun_now = false;
		if (out.underrun) {
			eng::os::Msg m {};
			m.type = eng::os::MsgType::AudioUnderrun;
			(void)m_port->post(m);
		}
	}

	/// Espera a que la cola se vacíe (punto de bloqueo explícito).
	void wait_all() noexcept { m_queue.wait_all(); }

	/// Marca un **underrun** del feeder; `flush()` lo reporta como flanco **una vez** por evento.
	void notify_underrun() noexcept { m_underrun_now = true; }

	[[nodiscard]] const AudioPlan& plan() const noexcept { return m_mixer.plan(); }
	[[nodiscard]] bool empty() const noexcept { return m_queue.empty(); }

private:
	AudioMixer& m_mixer;
	MixerExecutor m_exec;
	eng::Ref<eng::os::MsgPort<PortN>> m_port {};
	SoundQueue<N, MixerExecutor, eng::os::IntentDonePoster<PortN>> m_queue {};
	AudioMsgEdges m_edges {};
	bool m_underrun_now = false;
};

} // namespace eng::audio
