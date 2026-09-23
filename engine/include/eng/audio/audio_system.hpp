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

#include <eng/audio/audio_events.hpp>
#include <eng/audio/audio_mode.hpp>
#include <eng/audio/music_player.hpp>
#include <eng/audio/sfx_mixer.hpp>
#include <eng/os/message.hpp>

namespace eng::audio {

/// Formato del módulo de música.
enum class MusicFormat : u8 {
	None = 0,
	Protracker = 1, // .mod (PtPlayer)
	P61 = 2,        // .p61 (P61Player)
	OctaMED = 3,    // módulo MED incrustado (OctaMedPlayer, A1)
};

/// Sistema de audio del engine (SFX + música).
class AudioSystem {
public:
	AudioSystem() = default;
	AudioSystem(const AudioSystem&) = delete;
	AudioSystem& operator=(const AudioSystem&) = delete;

	/// Inicia el SFX mixer (reserva el buffer Chip y arranca). La música se
	/// arranca aparte con `play_music()`.
	bool init(MemorySystem& memory) {
		return m_sfx.init(memory);
	}

	/// Inicia con **modo y config** (A0): arranca el mixer, aplica el reparto de canales del modo.
	bool init(MemorySystem& memory, const AudioConfig& cfg) {
		m_cfg = cfg;
		if (!init(memory)) {
			return false;
		}
		set_sfx_volume(cfg.master_sfx_vol);
		set_music_volume(cfg.master_music_vol);
		return set_mode(cfg.mode);
	}

	/// Modo de audio vigente.
	[[nodiscard]] AudioMode mode() const { return m_mode; }
	/// Reparto de canales del modo vigente (sin solape).
	[[nodiscard]] ChannelQuota quota() const { return channel_quota(m_mode); }
	[[nodiscard]] const AudioConfig& config() const { return m_cfg; }

	/// Cambia de modo: para la música si el modo no la habilita, ajusta su máscara y deja el
	/// reparto listo. **Único punto** (junto con `init`) que fija el reparto de canales.
	///
	/// Nota: el mixer de SFX tiene máscara **fija** en `mixer_config.i`, así que `GameSfxOnly`
	/// (mixer a 4 canales) no reconfigura el mixer en runtime; el modo documenta el objetivo y
	/// corta/ajusta lo que sí es runtime (música y su máscara).
	bool set_mode(AudioMode mode) {
		m_mode = mode;
		const ChannelQuota q = channel_quota(mode);
		if (!q.music_enabled) {
			stop_music();
		} else {
			set_music_channel_mask(q.music_hw_mask);
		}
		return true;
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
		return play_music(module, format, {});
	}

	/// Como `play_music`, pero con un **buffer de descompresión** para el formato P61 cuando
	/// el módulo trae los samples empaquetados (ver `P61Player::play`).
	bool play_music(const MusicModule& module, MusicFormat format, eng::Span<eng::u8> buffer) {
		stop_music();
		switch (format) {
			case MusicFormat::P61:
				if (m_p61.play(module, buffer)) { m_format = MusicFormat::P61; }
				break;
			case MusicFormat::Protracker:
				if (m_pt.play(module)) { m_format = MusicFormat::Protracker; }
				break;
#if defined(ENG_AUDIO_OCTAMED)
			case MusicFormat::OctaMED:
				if (m_med.play(module)) { m_format = MusicFormat::OctaMED; }
				break;
#endif
			default:
				break;
		}
		return m_format != MusicFormat::None;
	}

	void stop_music() {
		m_p61.stop();
		m_pt.stop();
#if defined(ENG_AUDIO_OCTAMED)
		m_med.stop();
#endif
		m_format = MusicFormat::None;
	}

	/// Avanza la música una vez por frame. P61 y OctaMED son frame-driven; Protracker usa la
	/// interrupción CIA y no necesita esta llamada.
	void update_music() {
		if (m_format == MusicFormat::P61) {
			m_p61.update();
		}
#if defined(ENG_AUDIO_OCTAMED)
		else if (m_format == MusicFormat::OctaMED) {
			m_med.update();
		}
#endif
	}

	/// Marca un **underrun** (el mixer o un stream se quedó sin datos). Lo consume `tick_frame`:
	/// se postea `AudioUnderrun` **una sola vez** por evento, no por buffer (A2).
	void notify_underrun() noexcept { m_underrun_now = true; }

	/// **Tick de frame** (VBlank): avanza la música frame-driven y postea al `port` los mensajes
	/// de audio pendientes (`MusicEnd` al terminar un módulo sin loop; `AudioUnderrun`). No se
	/// postea nada por buffer: `AudioMsgEdges` emite solo en el flanco (A2).
	template <class Port>
	void tick_frame(Port& port) {
		update_music();
		const bool ended = (m_format == MusicFormat::P61) && m_p61.ended();
		const AudioMsgOut out = m_edges.on_tick(ended, m_underrun_now);
		m_underrun_now = false;
		if (out.music_end) {
			eng::os::Msg m {};
			m.type = eng::os::MsgType::MusicEnd;
			port.post(m);
		}
		if (out.underrun) {
			eng::os::Msg m {};
			m.type = eng::os::MsgType::AudioUnderrun;
			port.post(m);
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
#if defined(ENG_AUDIO_OCTAMED)
	OctaMedPlayer m_med {}; ///< opt-in: solo si se define `ENG_AUDIO_OCTAMED` (bloat del módulo)
#endif
	MusicFormat m_format = MusicFormat::None;
	AudioMode m_mode = AudioMode::Game;
	AudioConfig m_cfg {};
	AudioMsgEdges m_edges {};
	bool m_underrun_now = false;
};

} // namespace eng::audio
