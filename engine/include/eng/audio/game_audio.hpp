#pragma once

/// \file game_audio.hpp
/// Audio de juego: orquesta el `SampleBank` + la política de voces + el ducking
/// sobre el `AudioSystem` (SfxMixer + reproductores de música).
///
/// El juego NO maneja punteros ni canales crudos: registra sonidos en el
/// `SampleBank` por `id` y los dispara con `play(id)`. La política (cooldown,
/// límite de instancias, prioridad) y el ducking de la música se gestionan aquí.
///
/// La parte pura (banco + política) está en `sfx_bank.hpp` (host-testable);
/// este header es Amiga-only porque depende del `AudioSystem`.

#include <eng/audio/audio_system.hpp>
#include <eng/audio/sfx_bank.hpp>

namespace eng::audio {

/// Audio de juego: banco + política + ducking sobre el `AudioSystem`.
///
/// Uso típico:
///   GameAudio audio;
///   audio.bank().add(0, { explosion_data, /*prio*/3, /*max*/4, /*cooldown*/6, /*duck*/true });
///   audio.bank().add(1, { laser_data, 1, 2, 2, false });
///   audio.init(backend.memory());
///   audio.play_music(module, MusicFormat::Protracker);
///   // cada frame:
///   audio.play(0, frame);   // dispara explosion (aplica política)
///   audio.update(frame);    // poda voces + ducking
///   audio.update_music();   // avanza música (solo P61)
class GameAudio {
public:
	/// Referencia al `AudioSystem` subyacente (típicamente el del backend).
	explicit GameAudio(AudioSystem& system) : m_audio(&system) {}

	/// Sin sistema: se enlaza después con `attach`.
	GameAudio() = default;
	GameAudio(const GameAudio&) = delete;
	GameAudio& operator=(const GameAudio&) = delete;

	/// Enlaza este `GameAudio` a un `AudioSystem` (p. ej. `backend.audio()`).
	void attach(AudioSystem& system) { m_audio = &system; }

	/// Inicializa el SFX mixer del `AudioSystem` referenciado (buffer Chip +
	/// handler). La música se arranca con `play_music`.
	bool init(MemorySystem& memory) {
		return m_audio->init(memory);
	}

	void shutdown() {
		m_audio->shutdown();
	}

	/// Acceso al banco para registrar sonidos (antes de `play`).
	SampleBank& bank() { return m_bank; }
	const SampleBank& bank() const { return m_bank; }

	// ---- SFX --------------------------------------------------------------

	/// Dispara el sonido `id` aplicando la política (cooldown, límite, grupo,
	/// prioridad). Devuelve el canal (>=0) o -1 si se rechazó.
	SfxChannel play(u8 id, u32 frame) {
		const SfxDef* def = m_bank.find(id);
		if (def == nullptr || !allow_trigger(frame, *def, m_voices[id])) {
			return -1;
		}
		if (!allow_group(def->group, m_group_max[def->group], m_group_active[def->group])) {
			return -1;
		}
		SfxChannel ch = m_audio->play_sfx(
			{def->data}, def->priority,
			def->loop ? LoopMode::Loop : LoopMode::Once
		);
		if (ch >= 0) {
			m_voices[id].last_frame = frame;
			track(id, ch);
		}
		return ch;
	}

	/// Presupuesto de voces compartido por un grupo (0 = sin límite). Los sonidos
	/// con `SfxDef::group == group` comparten este límite de instancias.
	void set_group_budget(u8 group, u8 max) {
		if (group < kMaxGroups) {
			m_group_max[group] = max;
		}
	}

	/// Número de instancias activas del sonido `id` (telemetría).
	u8 active_count(u8 id) const {
		u8 n = 0;
		for (u8 i = 0; i < m_active_count; ++i) {
			if (m_active[i].id == id) {
				++n;
			}
		}
		return n;
	}

	/// ¿El ducking está activo ahora? (telemetría).
	constexpr bool is_ducking() const { return m_ducking; }

	/// Poda las voces acabadas y aplica el ducking de la música. Llamar una vez
	/// por frame (en `update`), antes de `update_music`.
	void update(u32) {
		prune();
		apply_ducking();
	}

	// ---- Música (delegación) ----------------------------------------------

	bool play_music(const MusicModule& module, MusicFormat format) {
		return m_audio->play_music(module, format);
	}
	void stop_music() { m_audio->stop_music(); }
	void update_music() { m_audio->update_music(); }

	/// Reserva canales de música para el SFX (solo Protracker). P. ej.
	/// `set_music_channel_mask(1)` deja AUD0 libre para el mixer.
	void set_music_channel_mask(u8 mask) {
		m_audio->set_music_channel_mask(mask);
	}

	// ---- Volumen -----------------------------------------------------------

	/// Volumen maestro global (SFX + música), 0..64.
	void set_master_volume(u8 volume) {
		m_audio->set_master_volume(volume);
	}
	/// Volumen de la música en reposo (sin ducking), 0..64.
	void set_music_volume(u8 volume) {
		m_normal_volume = static_cast<u8>(volume & 0x7fu);
		if (!m_ducking) {
			m_audio->set_music_volume(m_normal_volume);
		}
	}
	/// Volumen de la música durante el ducking, 0..64.
	void set_duck_volume(u8 volume) {
		m_duck_volume = static_cast<u8>(volume & 0x7fu);
	}
	/// Volumen del SFX, 0..64.
	void set_sfx_volume(u8 volume) {
		m_audio->set_sfx_volume(volume);
	}

	/// Acceso al `AudioSystem` subyacente (uso avanzado).
	AudioSystem& system() { return *m_audio; }

private:
	struct ActiveVoice {
		u8 id = 0;
		SfxChannel channel = -1;
	};

	/// Registra una voz activa (id + canal) en una ranura libre o acabada.
	void track(u8 id, SfxChannel channel) {
		for (u8 i = 0; i < m_active_count; ++i) {
			if (!m_audio->sfx().is_playing(m_active[i].channel)) {
				m_active[i] = {id, channel};
				return;
			}
		}
		if (m_active_count < kMaxActiveVoices) {
			m_active[m_active_count++] = {id, channel};
		}
	}

	/// Compacta la lista de voces activas (quita las acabadas) y recalcula los
	/// contadores por sonido y por grupo.
	void prune() {
		u8 w = 0;
		for (u8 i = 0; i < m_active_count; ++i) {
			if (m_audio->sfx().is_playing(m_active[i].channel)) {
				m_active[w++] = m_active[i];
			}
		}
		m_active_count = w;

		for (u8 i = 0; i < kMaxSfx; ++i) {
			m_voices[i].active = 0;
		}
		for (u8 g = 0; g < kMaxGroups; ++g) {
			m_group_active[g] = 0;
		}
		for (u8 i = 0; i < m_active_count; ++i) {
			++m_voices[m_active[i].id].active;
			const SfxDef* def = m_bank.find(m_active[i].id);
			if (def != nullptr && def->group != 0u) {
				++m_group_active[def->group];
			}
		}
	}

	/// Si hay alguna voz con `duck_music` activa, baja la música; si no, la
	/// restaura. Solo cambia el volumen cuando cambia el estado de ducking.
	void apply_ducking() {
		bool duck = false;
		for (u8 i = 0; i < m_active_count; ++i) {
			const SfxDef* def = m_bank.find(m_active[i].id);
			if (def != nullptr && def->duck_music) {
				duck = true;
				break;
			}
		}
		if (duck != m_ducking) {
			m_ducking = duck;
			m_audio->set_music_volume(duck ? m_duck_volume : m_normal_volume);
		}
	}

	AudioSystem* m_audio = nullptr;
	SampleBank m_bank {};
	VoiceState m_voices[kMaxSfx] {};
	ActiveVoice m_active[kMaxActiveVoices] {};
	u8 m_group_active[kMaxGroups] {};
	u8 m_group_max[kMaxGroups] {};
	u8 m_active_count = 0;
	u8 m_normal_volume = 48;
	u8 m_duck_volume = 20;
	bool m_ducking = false;
};

} // namespace eng::audio
