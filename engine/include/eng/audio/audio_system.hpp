#pragma once

/// \file audio_system.hpp
/// Fachada de audio del engine: un único punto de entrada para SFX y música.
///
/// `AudioSystem` compone los subsistemas ya existentes:
///   - `SfxMixer`   (efectos, Audio Mixer 3.7 de Photon) en `sfx_mixer.hpp`.
///   - `P61Player`  (música P61) y `PtPlayer` (música Protracker) en
///     `music_player.hpp`.
///
/// El juego no necesita conocer estos backends: llama a `play_sfx`/`play_music`
/// y el sistema se encarga de arrancar/parar/detener. La memoria contigua se
/// expresa con `Span` (`SfxSample`, `MusicModule`), sin punteros crudos.
///
/// NOTA DE CAPAS (apuntado, no urgente): `SfxMixer`/`P61Player`/`PtPlayer` y sus
/// wrappers `*_amiga` son backend Amiga (inline asm `jsr _MixerXxx`, VASM). Hoy
/// viven en `eng/audio/` junto al vocabulario portable (`audio.hpp`); lo correcto
/// a medio plazo es mover estos tres headers a `eng/platform/` (o
/// `eng/audio/amiga/`) y dejar en `eng/audio/` solo las intenciones. Solo paga
/// hacerlo cuando haya segunda plataforma o se reutilice `eng/audio` en host.

#include <eng/audio/music_player.hpp>
#include <eng/audio/sfx_mixer.hpp>

namespace eng::audio {

/// Formato del módulo de música.
enum class MusicFormat : u8 {
	None = 0,
	Protracker = 1, // .mod (PtPlayer)
	P61 = 2,        // .p61 (P61Player)
};

/// Sistema de audio del engine (SFX + música).
class AudioSystem {
public:
	AudioSystem() = default;
	AudioSystem(const AudioSystem&) = delete;
	AudioSystem& operator=(const AudioSystem&) = delete;

	/// Inicializa el SFX mixer (reserva el buffer Chip y arranca). La música se
	/// arranca aparte con `play_music()`.
	bool init(MemorySystem& memory) {
		return m_sfx.init(memory);
	}

	/// Detiene música y SFX, y desinstala el handler del mixer.
	void shutdown() {
		stop_music();
		m_sfx.shutdown();
	}

	// ---- SFX --------------------------------------------------------------

	/// Reproduce un efecto en el mejor canal libre (prioridad mayor gana).
	SfxChannel play_sfx(const SfxSample& sample, s16 priority, LoopMode mode, u32 loop_offset = 0) {
		return m_sfx.play(sample, priority, mode, loop_offset);
	}

	/// Reproduce un efecto en una voz concreta (MixCh0..MixCh3).
	SfxChannel play_sfx_on(u16 channel, const SfxSample& sample, s16 priority, LoopMode mode, u32 loop_offset = 0) {
		return m_sfx.play_on(channel, sample, priority, mode, loop_offset);
	}

	void stop_sfx(SfxChannel channel) { m_sfx.stop(channel); }
	bool sfx_playing(SfxChannel channel) const { return m_sfx.is_playing(channel); }
	void set_sfx_volume(u8 volume) { m_sfx.set_master_volume(volume); }
	u32 total_sfx_channels() const { return m_sfx.total_channels(); }

	// ---- Música -----------------------------------------------------------

	/// Reproduce un módulo en el formato dado (detiene la música previa).
	bool play_music(const MusicModule& module, MusicFormat format) {
		stop_music();
		switch (format) {
			case MusicFormat::P61:
				if (m_p61.play(module)) { m_format = MusicFormat::P61; }
				break;
			case MusicFormat::Protracker:
				if (m_pt.play(module)) { m_format = MusicFormat::Protracker; }
				break;
			default:
				break;
		}
		return m_format != MusicFormat::None;
	}

	void stop_music() {
		m_p61.stop();
		m_pt.stop();
		m_format = MusicFormat::None;
	}

	/// Avanza la música una vez por frame. P61 es frame-driven; Protracker usa la
	/// interrupción CIA y no necesita esta llamada.
	void update_music() {
		if (m_format == MusicFormat::P61) {
			m_p61.update();
		}
	}

	void set_music_volume(u8 volume) {
		if (m_format == MusicFormat::P61) {
			m_p61.set_master_volume(volume);
		} else if (m_format == MusicFormat::Protracker) {
			m_pt.set_master_volume(volume);
		}
	}

	/// Máscara de canales de música (solo Protracker). Semántica: bit a 1 = canal
	/// audible, bit a 0 = canal silenciado (bit 0 = AUD0 ... bit 3 = AUD3). P. ej.
	/// `set_music_channel_mask(0x0E)` silencia AUD0 (lo deja libre para el mixer)
	/// y mantiene AUD1..AUD3 sonando.
	void set_music_channel_mask(u8 mask) {
		if (m_format == MusicFormat::Protracker) {
			m_pt.set_channel_mask(mask);
		}
	}

	bool music_playing() const { return m_format != MusicFormat::None; }
	MusicFormat music_format() const { return m_format; }

	/// Volumen maestro (0..64): afecta a SFX y música a la vez. Útil para un
	/// control global (mute/fade). Para volúmenes independientes, usar
	/// `set_sfx_volume`/`set_music_volume`.
	void set_master_volume(u8 volume) {
		set_sfx_volume(volume);
		set_music_volume(volume);
	}

	// ---- Subsistemas (uso avanzado) ----------------------------------------

	SfxMixer& sfx() { return m_sfx; }
	P61Player& p61() { return m_p61; }
	PtPlayer& protracker() { return m_pt; }

private:
	SfxMixer m_sfx {};
	P61Player m_p61 {};
	PtPlayer m_pt {};
	MusicFormat m_format = MusicFormat::None;
};

} // namespace eng::audio
