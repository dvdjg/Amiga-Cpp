#pragma once

/// \file sfx_bank.hpp
/// Banco de muestras y política de voces (puro, sin hardware).
///
/// Separa la lógica PORTABLE de la capa de audio de juego del backend Amiga:
/// aquí viven `SfxDef`, `SampleBank`, `VoiceState` y la política `allow_trigger`
/// (cooldown + límite de instancias), que se pueden testear en host. La
/// orquestación con el mixer Amiga (`GameAudio`) está en `game_audio.hpp`.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::audio {

/// Número máximo de sonidos registrables en un `SampleBank`.
constexpr u8 kMaxSfx = 64;

/// Número máximo de voces activas que `GameAudio` sigue a la vez para aplicar
/// la política (límite de instancias, ducking). Debe ser >= al nº de voces del
/// mixer (4 por canal, típicamente 4..8).
constexpr u8 kMaxActiveVoices = 16;

/// Número máximo de grupos de sonido (presupuesto compartido de voces).
constexpr u8 kMaxGroups = 16;

/// Sentinela "nunca disparado" para el cooldown.
constexpr u32 kNever = 0xFFFFFFFFu;

/// Un sonido de juego + su política.
struct SfxDef {
	Span<const u8> data {};   // muestra preprocesada (ver AUDIO_MIXER.md)
	u8  priority = 0;         // mayor gana una voz ocupada
	u8  max_instances = 1;    // instancias simultáneas máximas (0 = sin límite)
	u16 cooldown_frames = 0;  // frames mínimos entre disparos (0 = sin cooldown)
	bool duck_music = false;  // bajar el volumen de la música mientras suena
	u8  group = 0;            // grupo (0 = sin grupo); comparte presupuesto de voces
};

/// Catálogo fijo de sonidos (sin heap). Indexa `SfxDef` por `id` (0..kMaxSfx-1).
class SampleBank {
public:
	constexpr SampleBank() = default;

	/// Registra (o sobrescribe) el sonido `id`. Ignora ids inválidos o vacíos.
	constexpr void add(u8 id, const SfxDef& def) {
		if (id < kMaxSfx && def.data.size() != 0u) {
			m_defs[id] = def;
			m_present[id] = true;
		}
	}

	/// Devuelve el `SfxDef` de `id`, o `nullptr` si no está registrado.
	constexpr const SfxDef* find(u8 id) const {
		return (id < kMaxSfx && m_present[id]) ? &m_defs[id] : nullptr;
	}

private:
	SfxDef m_defs[kMaxSfx] {};
	bool m_present[kMaxSfx] {};
};

/// Estado de voz por sonido (para la política).
struct VoiceState {
	u32 last_frame = kNever; // último frame en que se disparó
	u8  active = 0;          // instancias activas ahora
};

/// Política pura: ¿se permite disparar `def` en el frame `frame` dado el
/// estado `st`? Aplica cooldown y límite de instancias. Host-testable.
constexpr bool allow_trigger(u32 frame, const SfxDef& def, const VoiceState& st) {
	const bool cooled_down =
		(st.last_frame == kNever) || (frame - st.last_frame >= def.cooldown_frames);
	if (!cooled_down) {
		return false;
	}
	if (def.max_instances != 0u && st.active >= def.max_instances) {
		return false;
	}
	return true;
}

/// Política pura de grupo: ¿el grupo `group` admite una voz más? `group=0` o
/// `group_max=0` significan "sin límite de grupo". Host-testable.
constexpr bool allow_group(u8 group, u8 group_max, u8 group_active) {
	if (group == 0u || group_max == 0u) {
		return true;
	}
	return group_active < group_max;
}

} // namespace eng::audio
