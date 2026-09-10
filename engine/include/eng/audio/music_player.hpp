#pragma once

/// \file music_player.hpp
/// Música de tracker: envoltura C++23 del reproductor P61 (Photon/Scoopex),
/// integrado en el engine desde `demoscene-repo-orig/lib/libp61`.
///
/// El reproductor es un único `.asm` (`support/music/p61.asm` + `P6112-Play.i`),
/// ensamblado con VASM a ELF en `build-demo.sh`. Expone la API `P61_Init`,
/// `P61_Music` (por frame), `P61_End`, `P61_SetPosition` y el bloque de control
/// `P61_ControlBlock` (volumen maestro, flag de reproducción, posición).
///
/// Diseño: el programador de juego usa `MusicModule` (vista `Span` a la memoria
/// del módulo) y `P61Player`; los punteros de registro (`A0/A1/A2`) quedan en la
/// capa interna `p61_amiga`. Los reproductores pt/ahx seguirán el mismo patrón.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::audio {

/// Módulo de música (vista contigua a la memoria del módulo, sin puntero crudo).
struct MusicModule {
	Span<const u8> data {}; // datos del módulo (.p61 / .mod)
};

namespace p61_amiga {

/// Bloque de control de P61 (espejo de `p61.h`). Vive en el símbolo global
/// `_P61_ControlBlock` exportado por el ASM; aquí se referencia con su nombre
/// exacto (el identificador con guion bajo coincide con el símbolo VASM).
extern "C" struct P61ControlBlock {
	u16 Master;      // volumen maestro (0..64)
	u16 UseTempo;    // ¿usar tempo?
	u16 Play;        // 0 = parado, 1 = reproduciendo
	u16 E8;          // nybble tras comando E8
	const void* VBR; // base de vectores (si execbase no es válida)
	u16 Pos;         // posición actual (solo lectura)
	u16 Pattern;     // patrón actual (solo lectura)
	u16 Row;         // fila actual (solo lectura)
	s32 ChannelOffset[4];
} _P61_ControlBlock;

/// Envolturas de bajo nivel (convención de registros Amiga, vía `jsr _P61Xxx`).

inline s32 init(const void* module, const void* samples, const void* buffer) {
	register volatile const void* m __asm("a0") = module;
	register volatile const void* s __asm("a1") = samples;
	register volatile const void* b __asm("a2") = buffer;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _P61_Init" : "=r"(result) : "r"(m), "r"(s), "r"(b) : "cc", "memory");
	return static_cast<s32>(result);
}

inline void music() {
	__asm__ volatile("jsr _P61_Music" : : : "cc", "memory");
}

inline void end() {
	__asm__ volatile("jsr _P61_End" : : : "cc", "memory");
}

inline void set_position(u8 position) {
	register volatile u8 p __asm("d0") = position;
	__asm__ volatile("jsr _P61_SetPosition" : : "r"(p) : "cc", "memory");
}

} // namespace p61_amiga

/// Reproductor de música P61 orientado a juego.
///
/// Uso (tras tomar el display y arrancar el SFX mixer si lo hay):
///   P61Player music;
///   music.play(module);        // P61_Init
///   // cada frame, en update():
///   music.update();            // P61_Music
///   music.set_master_volume(48);
class P61Player {
public:
	/// Inicia la reproducción del módulo. Devuelve true si P61_Init tuvo éxito.
	bool play(const MusicModule& module) {
		if (module.data.empty()) {
			return false;
		}
		m_playing = (p61_amiga::init(module.data.data(), nullptr, nullptr) == 0);
		return m_playing;
	}

	/// Detiene la música.
	void stop() {
		if (m_playing) {
			p61_amiga::end();
			m_playing = false;
		}
	}

	/// Avanza la reproducción (llamar una vez por frame, en update/VBlank).
	void update() {
		if (m_playing) {
			p61_amiga::music();
		}
	}

	/// Volumen maestro (0..64).
	void set_master_volume(u8 volume) {
		p61_amiga::_P61_ControlBlock.Master = static_cast<u16>(volume & 0x7fu);
	}

	/// Salta a una posición concreta del módulo.
	void set_position(u8 position) {
		if (m_playing) {
			p61_amiga::set_position(position);
		}
	}

	/// ¿Hay música reproduciéndose?
	constexpr bool is_playing() const { return m_playing; }

	/// Posición actual del módulo (0..n).
	u8 position() const {
		return static_cast<u8>(p61_amiga::_P61_ControlBlock.Pos);
	}

private:
	bool m_playing = false;
};

// ---------------------------------------------------------------------------
// PTPlayer (Protracker .mod, Frank Wille) — modo CIA (el reproductor usa la
// interrupción CIA-B Timer-A para el timing; no requiere update por frame).
// ---------------------------------------------------------------------------

namespace pt_amiga {

/// Flag de reproducción `_mt_Enable` (byte, 0 = pausa, no-0 = reproducir).
extern "C" volatile u8 _mt_Enable;

inline void init(const void* module, const void* samples, u8 pos) {
	register volatile const void* m __asm("a0") = module;
	register volatile const void* s __asm("a1") = samples;
	register volatile u8 p __asm("d0") = pos;
	__asm__ volatile("jsr _PtInit" : : "r"(m), "r"(s), "r"(p) : "cc", "memory");
}

inline void install_cia() {
	__asm__ volatile("jsr _PtInstallCIA" : : : "cc", "memory");
}

inline void remove_cia() {
	__asm__ volatile("jsr _PtRemoveCIA" : : : "cc", "memory");
}

inline void end() {
	__asm__ volatile("jsr _PtEnd" : : : "cc", "memory");
}

inline void master_volume(u8 volume) {
	register volatile u8 v __asm("d0") = volume;
	__asm__ volatile("jsr _mt_mastervol" : : "r"(v) : "cc", "memory");
}

inline void channel_mask(u8 mask) {
	register volatile u8 m __asm("d0") = mask;
	__asm__ volatile("jsr _mt_channelmask" : : "r"(m) : "cc", "memory");
}

/// Posición actual del reproductor: D0=fila, D1=patrón. Devuelve
/// `(patrón << 16) | fila` para diagnosticar si la música avanza.
inline u32 get_pos() {
	register volatile u32 row __asm("d0");
	register volatile u32 song __asm("d1");
	__asm__ volatile("jsr _PtGetPos" : "=d"(row), "=d"(song) : : "cc", "memory");
	return (row & 0xffffu) | ((song & 0xffu) << 16u);
}

/// Período actual del canal 1 (melodía), para diagnosticar si el tono cambia.
inline u16 get_period() {
	register volatile u32 p __asm("d0");
	__asm__ volatile("jsr _PtGetPeriod" : "=d"(p) : : "cc", "memory");
	return static_cast<u16>(p & 0xffffu);
}

} // namespace pt_amiga

/// Reproductor de música Protracker (`.mod`), orientado a juego.
///
/// Usa el modo CIA (la interrupción CIA-B se encarga del timing), así que no hay
/// `update()` por frame: solo `play`/`stop`/`set_master_volume`. Arranca la música
/// primero y el SFX mixer después (ver MUSIC_PLAYER.md).
class PtPlayer {
public:
	/// Inicia la reproducción del módulo (devuelve true si hay módulo).
	bool play(const MusicModule& module) {
		if (module.data.empty()) {
			return false;
		}
		pt_amiga::init(module.data.data(), nullptr, 0);
		pt_amiga::install_cia();
		pt_amiga::_mt_Enable = 1;
		m_playing = true;
		return true;
	}

	/// Detiene la música y desinstala la interrupción.
	void stop() {
		if (m_playing) {
			pt_amiga::_mt_Enable = 0;
			pt_amiga::remove_cia();
			pt_amiga::end();
			m_playing = false;
		}
	}

	/// Volumen maestro (0..64).
	void set_master_volume(u8 volume) {
		if (m_playing) {
			pt_amiga::master_volume(static_cast<u8>(volume & 0x7fu));
		}
	}

	/// Reserva canales para el SFX mixer (p. ej. `channel_mask(1)` deja AUD0 libre).
	void set_channel_mask(u8 mask) {
		if (m_playing) {
			pt_amiga::channel_mask(mask);
		}
	}

	constexpr bool is_playing() const { return m_playing; }

	/// Posición actual (fila | patrón<<16) para diagnosticar si la música avanza.
	u32 position() const {
		return m_playing ? pt_amiga::get_pos() : 0u;
	}

	/// Período actual del canal 1 (melodía) para diagnosticar si el tono cambia.
	u16 period() const {
		return m_playing ? pt_amiga::get_period() : 0u;
	}

private:
	bool m_playing = false;
};

} // namespace eng::audio
