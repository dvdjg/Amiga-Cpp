#pragma once

/// \file audio_events.hpp
/// **Detección de flancos de eventos de audio** (fase A2): convierte el estado continuo de los
/// backends en mensajes del mini-SO **una sola vez por evento**, no por buffer.
///
/// El mixer produce un buffer cada pocos ms y un módulo puede terminar una vez; si se posteara
/// `MusicEnd`/`AudioUnderrun` «por buffer» el puerto se inundaría. `AudioMsgEdges` recuerda si el
/// evento ya se reportó y solo emite en el **flanco de subida**; vuelve a armarse cuando el evento
/// cesa (p. ej. vuelve a sonar). Es **puro** y host-testable. Ver §8 de `GAME_AUDIO.md`.

#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Mensajes de audio a postear en un tick (los que arrancan en este frame).
struct AudioMsgOut {
	bool music_end = false; ///< postear `MsgType::MusicEnd`
	bool underrun = false;  ///< postear `MsgType::AudioUnderrun`
};

/// Detector de **flancos** de los eventos de audio. `on_tick(estado)` devuelve qué mensajes postear
/// y actualiza su memoria: un evento sostenido se reporta **una vez** hasta que cesa y vuelve.
class AudioMsgEdges {
public:
	/// Evalúa el estado del frame (`music_ended_now`, `underrun_now`) y devuelve los mensajes que
	/// deben postearse en el flanco.
	[[nodiscard]] AudioMsgOut on_tick(bool music_ended_now, bool underrun_now) noexcept {
		AudioMsgOut out {};
		if (music_ended_now && !m_music_end_reported) {
			out.music_end = true;
			m_music_end_reported = true;
		} else if (!music_ended_now) {
			m_music_end_reported = false;
		}
		if (underrun_now && !m_underrun_reported) {
			out.underrun = true;
			m_underrun_reported = true;
		} else if (!underrun_now) {
			m_underrun_reported = false;
		}
		return out;
	}

	/// Fuerza el re-arme (p. ej. al cambiar de modo/música): el próximo flanco vuelve a reportar.
	void reset() noexcept {
		m_music_end_reported = false;
		m_underrun_reported = false;
	}

private:
	bool m_music_end_reported = false;
	bool m_underrun_reported = false;
};

} // namespace eng::audio
