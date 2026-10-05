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
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/os/message.hpp>
#include <eng/res/asset_cache.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng::audio {

/// Formato del módulo de música.
enum class MusicFormat : u8 {
	None = 0,
	Protracker = 1, // .mod (PtPlayer)
	P61 = 2,        // .p61 (P61Player)
	OctaMED = 3,    // módulo MED incrustado (OctaMedPlayer, A1)
};

/// **Detecta el formato** de un módulo por su cabecera, para `play_music(module)` sin formato
/// explícito: P61 (signo `P61A`) / Protracker (magic en el offset 1080: `M.K.`/`M!K!`/`4CHN`/`6CHN`)
/// / OctaMED (`MMDx`). `None` si no se reconoce.
[[nodiscard]] inline MusicFormat detect_music_format(eng::Span<const eng::u8> m) noexcept {
	const auto eq = [&](eng::usize i, const char* s) {
		for (eng::usize k = 0u; s[k] != '\0'; ++k) {
			if (i + k >= m.size() || m[i + k] != static_cast<eng::u8>(s[k])) {
				return false;
			}
		}
		return true;
	};
	if (eq(0u, "MMD0") || eq(0u, "MMD1") || eq(0u, "MMD2") || eq(0u, "MMD3")) {
		return MusicFormat::OctaMED;
	}
	if (eq(0u, "P61A")) {
		return MusicFormat::P61;
	}
	if (eq(1080u, "M.K.") || eq(1080u, "M!K!") || eq(1080u, "4CHN") || eq(1080u, "6CHN")) {
		return MusicFormat::Protracker;
	}
	return MusicFormat::None;
}

/// Sistema de audio del engine (SFX + música).
class AudioSystem : public eng::util::Noncopyable {
public:
	AudioSystem() = default;
	~AudioSystem() { shutdown(); }

	/// Inicia el SFX mixer (reserva el buffer Chip y arranca). La música se
	/// arranca aparte con `play_music()`.
	/// Inicia el mixer y sus owners; si falla su reserva, deja el sistema en estado detenido.
	bool init(MemoryManager& memory) {
		shutdown();
		m_memory = memory; // el engine reserva aquí el buffer de descompresión de la música
		m_cfg = {};
		m_mode = AudioMode::Game;
		m_edges = {};
		m_underrun_now = false;
		m_format = MusicFormat::None;
		if (!eng::audio::init_mixer_transaction(m_sfx, memory)) {
			m_memory.reset();
			return false;
		}
		return true;
	}

	/// Inicia con **modo y config** (A0): arranca el mixer, aplica el reparto de canales del modo.
	/// Inicia con configuración; un modo rechazado deshace el mixer y cualquier reserva de setup.
	bool init(MemoryManager& memory, const AudioConfig& cfg) {
		if (!init(memory)) return false;
		if (!eng::audio::apply_audio_config(cfg, [&](AudioMode mode) { return set_mode(mode); },
						    [&]() { shutdown(); })) {
			return false;
		}
		m_cfg = cfg;
		set_sfx_volume(cfg.master_sfx_vol);
		set_music_volume(cfg.master_music_vol);
		return true;
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
		for (eng::res::AssetLease& lease : m_sfx_cpu_leases) lease.reset();
		for (eng::res::AssetDmaLease& lease : m_music_asset_leases) lease.reset();
		m_memory.reset();
		m_format = MusicFormat::None;
		m_mode = AudioMode::Game;
		m_cfg = {};
		m_edges = {};
		m_underrun_now = false;
	}

	// ---- SFX --------------------------------------------------------------

	/// Reproduce un efecto en el mejor canal libre (prioridad mayor gana).
	SfxChannel play_sfx(const SfxSample& sample, s16 priority, LoopMode mode, u32 loop_offset = 0) {
		return m_sfx.play(sample, priority, mode, loop_offset);
	}

	/// Reproduce una muestra de un asset CPU-residente en una voz concreta; conserva su owner hasta terminar.
	SfxChannel play_sfx_asset(u16 mixer_channel, eng::res::AssetLease lease, s16 priority,
				  LoopMode mode, u32 loop_offset = 0u) {
		if (!lease.valid() || mode == LoopMode::Loop || mode == LoopMode::LoopOffset) return -1;
		const SfxSample sample {lease.view().data};
		const u8 requested_index = mixer_index(mixer_channel);
		if (requested_index >= 4u || m_sfx.channel_active(requested_index)) return -1;
		m_sfx_cpu_leases[requested_index].reset();
		const SfxChannel channel = m_sfx.play_on(mixer_channel, sample, priority, mode, loop_offset);
		if (channel < 0 || mixer_index(channel) != requested_index || !m_sfx.is_playing(channel)) {
			if (channel >= 0) m_sfx.stop(channel);
			return -1;
		}
		m_sfx_cpu_leases[requested_index] = static_cast<eng::res::AssetLease&&>(lease);
		return channel;
	}

	/// Reproduce un efecto en una voz concreta (MixCh0..MixCh3).
	SfxChannel play_sfx_on(u16 channel, const SfxSample& sample, s16 priority, LoopMode mode, u32 loop_offset = 0) {
		return m_sfx.play_on(channel, sample, priority, mode, loop_offset);
	}

	/// Detiene la voz y libera su lease CPU si pertenecía a una muestra de AssetCache.
	void stop_sfx(SfxChannel channel) {
		m_sfx.stop(channel);
		const u8 index = mixer_index(channel);
		if (index < 4u) m_sfx_cpu_leases[index].reset();
	}
	bool sfx_playing(SfxChannel channel) const { return m_sfx.is_playing(channel); }
	void set_sfx_volume(u8 volume) { m_sfx.set_master_volume(volume); }
	u32 total_sfx_channels() const { return m_sfx.total_channels(); }

	// ---- Música -----------------------------------------------------------

	/// Reproduce un módulo **detectando el formato** por su cabecera (P61/Protracker/OctaMED).
	bool play_music(const MusicModule& module) {
		return play_music(module, detect_music_format(module.data));
	}

	/// Reproduce un módulo en el formato dado (detiene la música previa).
	bool play_music(const MusicModule& module, MusicFormat format) {
		return play_music(module, format, {});
	}

	/// Como `play_music`, pero con un **buffer de descompresión** para el formato P61 cuando
	/// el módulo trae los samples empaquetados (ver `P61Player::play`).
	bool play_music(const MusicModule& module, MusicFormat format, eng::Span<eng::u8> buffer) {
		stop_music();
		switch (format) {
			case MusicFormat::P61: {
				// El **engine resuelve el buffer de descompresión** si el módulo lo pide y no lo
				// dan (`ROADMAP_GAME_API.md` §2): el juego no ve `p61_needs_sample_buffer`.
				eng::Span<eng::u8> buf = buffer;
				if (buf.empty() && eng::audio::p61_needs_sample_buffer(module.data) &&
				    m_memory.valid()) {
					const eng::u32 need = eng::audio::p61_sample_buffer_size(module.data);
					if (need != 0u) {
					m_music_buf = m_memory.get()->chip().reserve<eng::AudioTag>(
						need, 4u);
						if (m_music_buf.valid()) {
							buf = eng::Span<eng::u8> {m_music_buf.view.data(), need};
						}
					}
				}
				if (m_p61.play(module, buf)) { m_format = MusicFormat::P61; }
				break;
			}
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
		if (m_format == MusicFormat::None) {
			release_music_buffer();
		}
		return m_format != MusicFormat::None;
	}

	/// Reproduce un módulo en Chip y retiene su lease mientras el player/Paula pueda volver a leerlo.
	/// Retiene el módulo mientras el player/Paula pueda seguir leyéndolo desde Chip.
	bool play_music_asset(const MusicModule& module, MusicFormat format, eng::res::AssetDmaLease lease,
			      eng::Span<eng::u8> buffer = {}) {
		if (!lease.valid() || lease.view().kind != eng::MemoryKind::Chip) return false;
		if (!play_music(module, format, buffer)) return false;
		m_music_asset_leases[0] = static_cast<eng::res::AssetDmaLease&&>(lease);
		return true;
	}

	void stop_music() {
		m_p61.stop();
		m_pt.stop();
#if defined(ENG_AUDIO_OCTAMED)
		m_med.stop();
#endif
		m_format = MusicFormat::None;
		release_music_buffer();
		for (eng::res::AssetDmaLease& lease : m_music_asset_leases) lease.reset();
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
		for (u8 i = 0u; i < 4u; ++i) {
			if (m_sfx_cpu_leases[i].valid() &&
			    !m_sfx.channel_active(static_cast<u16>(MixCh0 << i))) {
				m_sfx_cpu_leases[i].reset();
			}
		}
		const bool ended = (m_format == MusicFormat::P61) && m_p61.ended();
		const AudioMsgOut out = m_edges.on_tick(ended, m_underrun_now);
		m_underrun_now = false;
		if (out.music_end && m_cfg.post_music_end_msg) {
			eng::os::Msg m {};
			m.type = eng::os::MsgType::MusicEnd;
			port.post(m);
		}
		if (out.underrun && m_cfg.post_underrun_msg) {
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
	/// Traduce la máscara de voz devuelta por Photon al índice estable 0..3 del mixer.
	[[nodiscard]] static u8 mixer_index(SfxChannel channel) noexcept {
		for (u8 i = 0u; i < 4u; ++i) {
		if ((channel & static_cast<SfxChannel>(MixCh0 << i)) != 0) return i;
		}
		return 0xffu;
	}

	/// Devuelve al banco el staging Chip que P61 usa para descomprimir samples empaquetados.
	void release_music_buffer() noexcept {
		if (m_music_buf.valid() && m_memory.valid()) {
			m_memory.get()->chip().release(m_music_buf);
			m_music_buf = {};
		}
	}

	SfxMixer m_sfx {};
	eng::res::AssetLease m_sfx_cpu_leases[4] {};
	eng::res::AssetDmaLease m_music_asset_leases[4] {};
	eng::Ref<MemoryManager> m_memory {};                              ///< para el buffer de música (Chip)
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_music_buf {}; ///< buffer de descompresión P61
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
