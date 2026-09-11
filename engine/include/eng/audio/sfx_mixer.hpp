#pragma once

/// \file sfx_mixer.hpp
/// Efectos de sonido (SFX) por software: envoltura C++23 del Audio Mixer 3.7 de
/// Photon (powerprograms.nl), integrado de forma nativa en el engine.
///
/// El mixer mezcla hasta `mixer_sw_channels` (4) muestras en UN canal hardware de
/// Paula (configurable en `support/audio_mixer/mixer_config.i`). Requiere muestras
/// PREPROCESADAS: cada byte debe caber en el rango ±128/mixer_sw_channels (para 4
/// canales: -32..+31) y la longitud debe ser múltiplo del tamaño mínimo (4 bytes
/// por defecto). Ver `docs/engine/audio/AUDIO_MIXER.md` para los requisitos
/// completos y cómo preprocesar.
///
/// Diseño:
///   - `mixer_amiga`  : puente al ASM (estructura `MixerEffect` + wrappers con
///                      convención de registros Amiga, vía `jsr _MixerXxx`).
///   - `SfxMixer`     : API orientada a juego (play/stop/volumen/canales).
///
/// El código ASM vive en `support/audio_mixer/` y se ensambla con VASM a ELF en
/// `tools/build/build-demo.sh`. Es Amiga-only (usa VBR/interrupciones/DMACON).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/memory/arena.hpp>

namespace eng::audio {

// ---------------------------------------------------------------------------
// Constantes del mixer (espejo de mixer.h / mixer.i).
// ---------------------------------------------------------------------------
constexpr u8  MixPal = 0;   // video system PAL
constexpr u8  MixNtsc = 1;  // video system NTSC

/// Modo de repetición de un efecto.
enum class LoopMode : s16 {
	Once = 1,        // MIX_FX_ONCE: reproduce una vez
	Loop = -1,       // MIX_FX_LOOP: bucle infinito desde el inicio
	LoopOffset = -2, // MIX_FX_LOOP_OFFSET: bucle desde el offset dado
};

/// Canales software del mixer (uno por voz, hasta mixer_sw_channels).
constexpr u16 MixCh0 = 16;   // MIX_CH0
constexpr u16 MixCh1 = 32;   // MIX_CH1
constexpr u16 MixCh2 = 64;   // MIX_CH2
constexpr u16 MixCh3 = 128;  // MIX_CH3

/// Estado de un canal (devuelto por el mixer).
constexpr u16 MixChFree = 0;  // MIX_CH_FREE
constexpr u16 MixChBusy = 1;  // MIX_CH_BUSY

/// Canales hardware de Paula (para MIXER_MULTI; en MIXER_SINGLE se ignoran).
constexpr u8 DmaAud0 = 1;  // DMAF_AUD0
constexpr u8 DmaAud1 = 2;  // DMAF_AUD1
constexpr u8 DmaAud2 = 4;  // DMAF_AUD2
constexpr u8 DmaAud3 = 8;  // DMAF_AUD3

/// Longitud (bytes) del bloque ficticio de datos de plugin que se pasa a
/// `MixerSetup`. Aunque los plugins estén desactivados, el mixer espera
/// punteros no nulos y un tamaño; con `nullptr`/0 la mezcla queda en silencio.
constexpr u32 kPluginDataBytes = 64;

/// Tamaño por defecto del buffer de plugins cuando `MIXER_ENABLE_PLUGINS=0`
/// (el mixer no lo usa, solo necesita un puntero válido).
constexpr u32 kPluginBufferBytes = 896;

namespace mixer_amiga {

/// Estructura de efecto: define una muestra a reproducir. Debe coincidir
/// byte a byte con `MXEffect` de mixer.i (20 bytes, empaquetado).
struct MixerEffect {
	s32 length = 0;             // longitud en bytes (longword)
	const u8* sample = nullptr; // puntero a la muestra (cualquier tipo de RAM)
	s16 loop = 0;               // LoopMode (MIX_FX_*)
	s16 priority = 0;           // prioridad con signo (mayor = gana)
	s32 loop_offset = 0;        // offset de reinicio si loop == LoopOffset
	const void* plugin = nullptr; // NULL sin plugin (requiere MIXER_ENABLE_PLUGINS)
};
static_assert(sizeof(MixerEffect) == 20, "MXEffect debe medir 20 bytes");
static_assert(__builtin_offsetof(MixerEffect, length) == 0);
static_assert(__builtin_offsetof(MixerEffect, sample) == 4);
static_assert(__builtin_offsetof(MixerEffect, loop) == 8);
static_assert(__builtin_offsetof(MixerEffect, priority) == 10);
static_assert(__builtin_offsetof(MixerEffect, loop_offset) == 12);
static_assert(__builtin_offsetof(MixerEffect, plugin) == 16);

/// Envolturas de bajo nivel. Usan `register ... __asm("reg")` (convención de
/// registros Amiga) y `jsr _MixerXxx` (alias C que crea `MIXER_C_DEFS=1`).
/// Patrón idéntico al de `mixer.h` para Bartman GCC.

inline u32 get_buffer_size() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetBufferSize" : "=r"(result) : : "cc", "memory");
	return result;
}

inline void setup(void* buffer, void* plugin_buffer, void* plugin_data, u16 video_system, u16 plugin_data_len) {
	register volatile void* b __asm("a0") = buffer;
	register volatile void* pb __asm("a1") = plugin_buffer;
	register volatile void* pd __asm("a2") = plugin_data;
	register volatile u16 v __asm("d0") = video_system;
	register volatile u16 l __asm("d1") = plugin_data_len;
	__asm__ volatile("jsr _MixerSetup" : : "r"(b), "r"(pb), "r"(pd), "r"(v), "r"(l) : "cc", "memory");
}

inline void install_handler(void* vbr, u16 save_vector) {
	register volatile void* v __asm("a0") = vbr;
	register volatile u16 s __asm("d0") = save_vector;
	__asm__ volatile("jsr _MixerInstallHandler" : : "r"(v), "r"(s) : "cc", "memory");
}

inline void remove_handler() {
	__asm__ volatile("jsr _MixerRemoveHandler" : : : "cc", "memory");
}

inline void start() {
	__asm__ volatile("jsr _MixerStart" : : : "cc", "memory");
}

inline void stop() {
	__asm__ volatile("jsr _MixerStop" : : : "cc", "memory");
}

inline void volume(u16 volume) {
	register volatile u16 v __asm("d0") = volume;
	__asm__ volatile("jsr _MixerVolume" : : "r"(v) : "cc", "memory");
}

/// Reproduce un efecto en el mejor canal libre (por prioridad/edad).
/// Devuelve el canal hw+mixer, o -1 si no hay canal libre.
inline s32 play_fx(const MixerEffect& fx, u32 hardware_channel) {
	register volatile const MixerEffect* e __asm("a0") = &fx;
	register volatile u32 hc __asm("d0") = hardware_channel;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerPlayFX" : "=r"(result) : "r"(e), "r"(hc) : "cc", "memory");
	return static_cast<s32>(result);
}

/// Reproduce un efecto en un canal hw+mixer concreto.
inline s32 play_channel_fx(const MixerEffect& fx, u32 mixer_channel) {
	register volatile const MixerEffect* e __asm("a0") = &fx;
	register volatile u32 mc __asm("d0") = mixer_channel;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerPlayChannelFX" : "=r"(result) : "r"(e), "r"(mc) : "cc", "memory");
	return static_cast<s32>(result);
}

inline void stop_fx(u16 mixer_channel_mask) {
	register volatile u16 m __asm("d0") = mixer_channel_mask;
	__asm__ volatile("jsr _MixerStopFX" : : "r"(m) : "cc", "memory");
}

inline u32 channel_status(u16 mixer_channel) {
	register volatile u16 c __asm("d0") = mixer_channel;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetChannelStatus" : "=r"(result) : "r"(c) : "cc", "memory");
	return result;
}

inline u32 total_channel_count() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetTotalChannelCount" : "=r"(result) : : "cc", "memory");
	return result;
}

inline u32 sample_min_size() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetSampleMinSize" : "=r"(result) : : "cc", "memory");
	return result;
}

/// Reinicia el contador de interrupciones del mixer (requiere MIXER_COUNTER=1).
inline void reset_counter() {
	__asm__ volatile("jsr _MixerResetCounter" : : : "cc", "memory");
}

/// Nº de interrupciones del mixer desde el último reset (requiere MIXER_COUNTER=1).
inline u16 get_counter() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetCounter" : "=r"(result) : : "cc", "memory");
	return static_cast<u16>(result & 0xffffu);
}

} // namespace mixer_amiga

/// Una muestra preprocesada lista para el mixer. Expone la memoria como
/// `Span<const u8>` (tamaño viaja con la vista): el programador de juego nunca
/// ve un puntero crudo. El puntero solo aparece en `MixerEffect` (capa interna).
struct SfxSample {
	Span<const u8> data {}; // vista a la muestra (cualquier RAM, múltiplo del mínimo)
};

/// Canal de efecto devuelto por `play()` (combinación hw+mixer). -1 = sin canal.
using SfxChannel = s32;

/// API de juego para efectos de sonido, respaldada por el Audio Mixer 3.7.
///
/// Uso típico (tras tomar el display):
///   SfxMixer sfx;
///   sfx.init(backend.memory());      // reserva el buffer Chip y arranca el mixer
///   sfx.play(explosion, 2, LoopMode::Once);
///   ...
///   sfx.shutdown();                  // detiene y desinstala el handler
class SfxMixer {
public:
	SfxMixer() = default;
	SfxMixer(const SfxMixer&) = delete;
	SfxMixer& operator=(const SfxMixer&) = delete;

	/// Reserva el buffer Chip requerido, configura el mixer y arranca el handler
	/// (VBR=0, propio de un 68000). Asume que el sistema ya no usa interrupciones
	/// de audio (el engine hace takeover del display antes).
	///
	/// NOTA: aunque los plugins estén desactivados en `mixer_config.i`, el mixer
	/// espera punteros NO nulos para el buffer de plugins y el de datos (como en
	/// el ejemplo `CMixer.c`); pasar `nullptr` deja la mezcla en silencio. Por eso
	/// se reservan aquí (Chip RAM) y se pasan a `MixerSetup`.
	bool init(MemorySystem& memory) {
		m_buffer_size = mixer_amiga::get_buffer_size();
		m_buffer = memory.chip.allocate(m_buffer_size, 4);
		if (!m_buffer.valid()) {
			return false;
		}

		// Plugins desactivados (MIXER_ENABLE_PLUGINS=0 en mixer_config.i):
		// MixerGetPluginsBufferSize() es un no-op y deja D0 con basura, así que
		// no se puede usar. El mixer NO usa estos buffers, pero MixerSetup y el
		// handler esperan punteros válidos -> reservamos bloques fijos. Pasar
		// nullptr deja la mezcla en silencio (bug corregido con la demo 068).
		m_plugin_buffer_size = kPluginBufferBytes;
		m_plugin_buffer = memory.chip.allocate(m_plugin_buffer_size, 4);
		if (!m_plugin_buffer.valid()) m_plugin_buffer = memory.slow.allocate(m_plugin_buffer_size, 4);
		m_plugin_data = memory.chip.allocate(kPluginDataBytes, 4);
		if (!m_plugin_data.valid()) m_plugin_data = memory.slow.allocate(kPluginDataBytes, 4);
		if (!m_plugin_buffer.valid() || !m_plugin_data.valid()) {
			return false;
		}

		mixer_amiga::setup(m_buffer.data, m_plugin_buffer.data, m_plugin_data.data,
			MixPal, static_cast<u16>(kPluginDataBytes));
		mixer_amiga::install_handler(nullptr, 0); // VBR=0 (68000), guardar vector
		mixer_amiga::start();
		m_ready = true;
		return true;
	}

	/// Detiene el mixer y desinstala el handler. No libera el bloque (lo hace la
	/// arena al cerrar la demo).
	void shutdown() {
		if (!m_ready) {
			return;
		}
		mixer_amiga::stop();
		mixer_amiga::remove_handler();
		m_ready = false;
	}

	/// Volumen maestro del mixer (0..64).
	void set_master_volume(u8 volume) {
		if (m_ready) {
			mixer_amiga::volume(static_cast<u16>(volume & 0x7fu));
		}
	}

	/// Reproduce una muestra en el mejor canal libre. `priority` mayor gana.
	/// Devuelve el canal (>=0) o -1 si no hay canal libre.
	SfxChannel play(const SfxSample& sample, s16 priority, LoopMode mode, u32 loop_offset = 0) {
		if (!m_ready || sample.data.empty()) {
			return -1;
		}
		mixer_amiga::MixerEffect fx;
		fx.length = static_cast<s32>(sample.data.size());
		fx.sample = sample.data.data();
		fx.loop = static_cast<s16>(mode);
		fx.priority = priority;
		fx.loop_offset = static_cast<s32>(loop_offset);
		fx.plugin = nullptr;
		return mixer_amiga::play_fx(fx, 0);
	}

	/// Reproduce en un canal software concreto (MixCh0..MixCh3).
	SfxChannel play_on(u16 mixer_channel, const SfxSample& sample, s16 priority, LoopMode mode, u32 loop_offset = 0) {
		if (!m_ready || sample.data.empty()) {
			return -1;
		}
		mixer_amiga::MixerEffect fx;
		fx.length = static_cast<s32>(sample.data.size());
		fx.sample = sample.data.data();
		fx.loop = static_cast<s16>(mode);
		fx.priority = priority;
		fx.loop_offset = static_cast<s32>(loop_offset);
		fx.plugin = nullptr;
		return mixer_amiga::play_channel_fx(fx, mixer_channel);
	}

	/// Detiene la reproducción en el canal dado (máscara hw+mixer).
	void stop(SfxChannel channel) {
		if (m_ready && channel >= 0) {
			mixer_amiga::stop_fx(static_cast<u16>(channel));
		}
	}

	/// ¿Está ocupado el canal dado?
	bool is_playing(SfxChannel channel) const {
		if (!m_ready || channel < 0) {
			return false;
		}
		return mixer_amiga::channel_status(static_cast<u16>(channel)) == MixChBusy;
	}

	/// Número total de canales software disponibles.
	u32 total_channels() const {
		return m_ready ? mixer_amiga::total_channel_count() : 0u;
	}

	/// Tamaño mínimo (en bytes) al que deben ser múltiplos las muestras.
	u32 sample_min_size() const {
		return m_ready ? mixer_amiga::sample_min_size() : 4u;
	}

	constexpr bool ready() const { return m_ready; }

	/// Buffer Chip que el mixer rellena cada interrupción y que Paula reproduce
	/// por DMA (diagnóstico; no usar en gameplay).
	const u8* buffer() const { return static_cast<const u8*>(m_buffer.data); }
	u32 buffer_bytes() const { return m_buffer.size; }

	/// Reinicia el contador de interrupciones del mixer (diagnóstico).
	void reset_counter() { if (m_ready) mixer_amiga::reset_counter(); }
	/// Nº de interrupciones del mixer desde el último reset (diagnóstico).
	u16 counter() const { return m_ready ? mixer_amiga::get_counter() : 0u; }

private:
	bool m_ready = false;
	u32 m_buffer_size = 0;
	u32 m_plugin_buffer_size = 0;
	MemoryBlock m_buffer {};
	MemoryBlock m_plugin_buffer {};
	MemoryBlock m_plugin_data {};
};

} // namespace eng::audio
