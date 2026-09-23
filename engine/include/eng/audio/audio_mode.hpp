#pragma once

/// \file audio_mode.hpp
/// **Modos de audio y reparto de canales** (fase A0). Paula solo tiene 4 canales DMA; el engine
/// fija **perfiles** (`AudioMode`) que reparten esos 4 canales entre los backends, en vez de
/// dejar que el juego los reparta a mano. Ver §7 de `GAME_AUDIO.md` y `ROADMAP_AUDIO.md` (A0).
///
/// `channel_quota(mode)` es **puro** (host-testable): devuelve las máscaras de mixer/música de
/// cada modo y garantiza que **no se solapan**. `paula::period_for_hz` también es puro. Las
/// escrituras de registro (DMACON, `AUDn*`) las hace el backend Amiga, no este header.

#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Perfil de audio. `TitleOctaMED` ocupa los 4 canales (mezcla SW de 8 voces).
enum class AudioMode : eng::u8 {
	Silent = 0,       ///< DMA de audio off (pausa total)
	Game = 1,         ///< AUD0 → mixer (SFX), AUD1..AUD3 → música
	GameSfxOnly = 2,  ///< los 4 canales → mixer (sin música de tracker)
	TitleOctaMED = 3, ///< los 4 canales → OctaMED (8 voces SW)
};

/// Reparto de los 4 canales resultante de un modo. Máscaras de 4 bits (bit 0 = AUD0).
struct ChannelQuota {
	eng::u8 mixer_hw_mask = 0u; ///< canales del mixer de SFX
	eng::u8 music_hw_mask = 0u; ///< canales de música (P61/Protracker/OctaMED)
	bool sfx_enabled = false;
	bool music_enabled = false;

	/// ¿Se solapan mixer y música? Nunca debe pasar (garantía de A0).
	[[nodiscard]] constexpr bool overlaps() const noexcept {
		return (mixer_hw_mask & music_hw_mask) != 0u;
	}
};

/// Reparto de canales por modo. **Sin solape** en ningún modo.
[[nodiscard]] constexpr ChannelQuota channel_quota(AudioMode mode) noexcept {
	switch (mode) {
		case AudioMode::Silent:
			return {0x0u, 0x0u, false, false};
		case AudioMode::Game:
			return {0x1u, 0xEu, true, true}; // AUD0 SFX; AUD1..AUD3 música
		case AudioMode::GameSfxOnly:
			return {0xFu, 0x0u, true, false}; // 4 canales para SFX
		case AudioMode::TitleOctaMED:
			return {0x0u, 0xFu, false, true}; // 4 canales para OctaMED
	}
	return {0x0u, 0x0u, false, false};
}

/// Configuración inicial de audio: modo + parámetros de Paula.
struct AudioConfig {
	AudioMode mode = AudioMode::Game;
	eng::u16 mixer_period = 322u;  ///< ~11 kHz PAL (`3546895 / period`)
	eng::u8 mixer_hw_mask = 0x1u;  ///< AUD0 para SFX (modo Game)
	eng::u8 mixer_sw_voices = 4u;  ///< voces software del mixer
	eng::u8 music_hw_mask = 0xEu;  ///< AUD1..AUD3 para música (modo Game)
	eng::u8 master_sfx_vol = 64u;
	eng::u8 master_music_vol = 64u;
	bool post_music_end_msg = true;  ///< postear `MsgType::MusicEnd` (A2)
	bool post_underrun_msg = false;  ///< postear `MsgType::AudioUnderrun` (A2)
};

/// **Helpers de bajo nivel de Paula** (registros y cuentas que el juego no debe repetir).
namespace paula {

/// Reloj de referencia PAL de Paula (Hz).
inline constexpr eng::u32 kPalClock = 3546895u;

/// Periodo de Paula para una frecuencia: `3546895 / hz` (PAL), acotado a **124..65535**.
/// `hz == 0` devuelve el máximo (silencio).
[[nodiscard]] constexpr eng::u16 period_for_hz(eng::u32 hz) noexcept {
	if (hz == 0u) {
		return 65535u;
	}
	const eng::u32 p = kPalClock / hz;
	if (p < 124u) {
		return 124u;
	}
	if (p > 65535u) {
		return 65535u;
	}
	return static_cast<eng::u16>(p);
}

/// ¿La máscara de 4 bits es válida (dentro de los 4 canales)?
[[nodiscard]] constexpr bool valid_channel_mask(eng::u8 mask) noexcept {
	return (mask & 0xF0u) == 0u;
}

} // namespace paula

} // namespace eng::audio
