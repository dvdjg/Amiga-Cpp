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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>

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

/// Las `_MixerXxx` **no siguen el ABI C**: el contrato es que preservan lo que usan salvo el
/// scratch ABI (`d0/d1/a0/a1`). Sin declararlo, el compilador puede mantener un valor vivo en un
/// registro que la rutina pisa. Se declaran esos registros menos los fijados como entrada/salida.
/// NO se sobre-declara con los que la rutina salva: un clobber grande dispara ICEs de gcc m68k
/// (`dwarf2out_frame_debug_adjust_cfa`). Ver `docs/debugging/investigaciones/p61-audio-dma.md`.
#define MX_CB_ALL "d0", "d1", "a0", "a1"
#define MX_CB_NO_D0 "d1", "a0", "a1"
#define MX_CB_NO_A0D0 "d1", "a1"

inline u32 get_buffer_size() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetBufferSize" : "=r"(result) : : MX_CB_NO_D0, "cc", "memory");
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
	__asm__ volatile("jsr _MixerInstallHandler" : : "r"(v), "r"(s) : MX_CB_NO_A0D0, "cc", "memory");
}

inline void remove_handler() {
	__asm__ volatile("jsr _MixerRemoveHandler" : : : MX_CB_ALL, "cc", "memory");
}

inline void start() {
	__asm__ volatile("jsr _MixerStart" : : : MX_CB_ALL, "cc", "memory");
}

inline void stop() {
	__asm__ volatile("jsr _MixerStop" : : : MX_CB_ALL, "cc", "memory");
}

inline void volume(u16 volume) {
	register volatile u16 v __asm("d0") = volume;
	__asm__ volatile("jsr _MixerVolume" : : "r"(v) : MX_CB_NO_D0, "cc", "memory");
}

/// Reproduce un efecto en el mejor canal libre (por prioridad/edad).
/// Devuelve el canal hw+mixer, o -1 si no hay canal libre.
inline s32 play_fx(const MixerEffect& fx, u32 hardware_channel) {
	register volatile const MixerEffect* e __asm("a0") = &fx;
	register volatile u32 hc __asm("d0") = hardware_channel;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerPlayFX" : "=r"(result) : "r"(e), "r"(hc) : MX_CB_NO_A0D0, "cc", "memory");
	return static_cast<s32>(result);
}

/// Reproduce un efecto en un canal hw+mixer concreto.
inline s32 play_channel_fx(const MixerEffect& fx, u32 mixer_channel) {
	register volatile const MixerEffect* e __asm("a0") = &fx;
	register volatile u32 mc __asm("d0") = mixer_channel;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerPlayChannelFX" : "=r"(result) : "r"(e), "r"(mc) : MX_CB_NO_A0D0, "cc", "memory");
	return static_cast<s32>(result);
}

inline void stop_fx(u16 mixer_channel_mask) {
	register volatile u16 m __asm("d0") = mixer_channel_mask;
	__asm__ volatile("jsr _MixerStopFX" : : "r"(m) : MX_CB_NO_D0, "cc", "memory");
}

inline u32 channel_status(u16 mixer_channel) {
	register volatile u16 c __asm("d0") = mixer_channel;
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetChannelStatus" : "=r"(result) : "r"(c) : MX_CB_NO_D0, "cc", "memory");
	return result;
}

inline u32 total_channel_count() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetTotalChannelCount" : "=r"(result) : : MX_CB_NO_D0, "cc", "memory");
	return result;
}

inline u32 sample_min_size() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetSampleMinSize" : "=r"(result) : : MX_CB_NO_D0, "cc", "memory");
	return result;
}

/// Reinicia el contador de interrupciones del mixer (requiere MIXER_COUNTER=1).
inline void reset_counter() {
	__asm__ volatile("jsr _MixerResetCounter" : : : MX_CB_ALL, "cc", "memory");
}

/// Nº de interrupciones del mixer desde el último reset (requiere MIXER_COUNTER=1).
inline u16 get_counter() {
	register volatile u32 result __asm("d0");
	__asm__ volatile("jsr _MixerGetCounter" : "=r"(result) : : MX_CB_NO_D0, "cc", "memory");
	return static_cast<u16>(result & 0xffffu);
}

#undef MX_CB_ALL
#undef MX_CB_NO_D0
#undef MX_CB_NO_A0D0

} // namespace mixer_amiga

	/// Una muestra preprocesada lista para el mixer. Expone la memoria como
	/// `Span<const u8>` (tamaño viaja con la vista); ownership/leases se administran en `AudioSystem`.
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
	~SfxMixer() { shutdown(); }
	SfxMixer() = default;
	SfxMixer(const SfxMixer&) = delete;
	SfxMixer& operator=(const SfxMixer&) = delete;

	/// Reserva el buffer **Chip** de salida (obligatorio) y arranca el handler. El mixer
	/// sintetiza en `mixer_buffer` y Paula lo **reproduce por DMA**: ese buffer **debe** estar en
	/// Chip (`MixerSetup` doc: «A0 must point to a block of memory in **Chip RAM**»). Los buffers
	/// de plugins (desactivados en `mixer_config.i`) admiten **cualquier** RAM: van a Fast→Slow,
	/// nunca a Chip (no son DMA). Sin Chip para la salida, `init` falla (no hay fallback).
	///
	/// NOTA: aunque los plugins estén desactivados, `MixerSetup` y el handler esperan punteros NO
	/// nulos para `plugin_buffer`/`plugin_data` (como en el ejemplo `CMixer.c`); pasar `nullptr`
	/// deja la mezcla en silencio. Por eso se reservan aquí (cualquier RAM) y se pasan a
	/// `MixerSetup`. Ver `MEMORY_OWNERSHIP.md` §"Bancos y contratos" y `GAME_AUDIO.md` §4.
	/// Inicializa transaccionalmente: reinicia el estado y, ante cualquier fallo, no deja bloques vivos.
	bool init(MemoryManager& memory) {
		shutdown();
		m_memory = memory;
		m_buffer_size = 0u;
		m_plugin_buffer_size = 0u;
		m_buffer_size = mixer_amiga::get_buffer_size();
		// Salida del mixer: **Chip obligatorio** (DMA de Paula). Sin fallback: si no cabe, falla.
		m_buffer = memory.chip().reserve<eng::MixerBufferTag>(m_buffer_size, 4u);
		if (!m_buffer.valid()) {
			m_memory.reset();
			m_buffer_size = 0u;
			return false;
		}

		// Plugins desactivados (MIXER_ENABLE_PLUGINS=0 en mixer_config.i): el mixer NO usa estos
		// buffers, pero espera punteros válidos. Aceptan **cualquier RAM** (no son DMA) ->
		// `NoChip` (Fast -> Slow, sin consumir RAM DMA).
		m_plugin_buffer_size = kPluginBufferBytes;
		m_plugin_buffer = eng::fast_or_slow<eng::MixerBufferTag>(memory, m_plugin_buffer_size, 4u);
		m_plugin_data = eng::fast_or_slow<eng::MixerBufferTag>(memory, kPluginDataBytes, 4u);
		if (!m_plugin_buffer.valid() || !m_plugin_data.valid()) {
			release_buffers();
			m_memory.reset();
			m_buffer_size = 0u;
			m_plugin_buffer_size = 0u;
			return false;
		}

		mixer_amiga::setup(m_buffer.view.data(), m_plugin_buffer.view.data(), m_plugin_data.view.data(),
			MixPal, static_cast<u16>(kPluginDataBytes));
		mixer_amiga::install_handler(nullptr, 0); // VBR=0 (68000), guardar vector
		mixer_amiga::start();
		m_ready = true;
		return true;
	}

	/// Detiene el mixer, desinstala el handler y devuelve sus bloques a los bancos. El llamador
	/// debe garantizar que Paula ya no puede leer el buffer Chip antes de invocarlo.
	void shutdown() {
		if (m_ready) {
			mixer_amiga::stop();
			mixer_amiga::remove_handler();
			m_ready = false;
		}
		release_buffers();
		m_memory.reset();
		m_buffer_size = 0u;
		m_plugin_buffer_size = 0u;
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

	/// Estado de una voz software concreta (0..3), para release de leases al terminar naturalmente.
	[[nodiscard]] bool channel_active(u16 voice) const {
		return m_ready && voice < 4u &&
			mixer_amiga::channel_status(static_cast<u16>(MixCh0 << voice)) == MixChBusy;
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
	const u8* buffer() const { return m_buffer.view.as_const().data(); }
	u32 buffer_bytes() const { return static_cast<u32>(m_buffer.view.size()); }

	/// Reinicia el contador de interrupciones del mixer (diagnóstico).
	void reset_counter() { if (m_ready) mixer_amiga::reset_counter(); }
	/// Nº de interrupciones del mixer desde el último reset (diagnóstico).
	u16 counter() const { return m_ready ? mixer_amiga::get_counter() : 0u; }

private:
	/// Devuelve mixer-buffer a Chip y buffers opcionales al banco CPU efectivo que los asignó.
	void release_buffers() noexcept {
		if (!m_memory.valid()) {
			return;
		}
		if (m_buffer.valid()) {
			m_memory->chip().release(m_buffer);
			m_buffer = {};
		}
		release_any(m_plugin_buffer);
		release_any(m_plugin_data);
	}

	/// Libera un buffer no-DMA según el MemoryKind efectivo conservado en el Block.
	void release_any(eng::Block<eng::MixerBufferTag>& block) noexcept {
		if (!block.valid()) {
			block = {};
			return;
		}
		if (block.kind == eng::MemoryKind::Fast) {
			m_memory->fast().release(block.view.data());
		} else if (block.kind == eng::MemoryKind::Slow) {
			m_memory->slow().release(block.view.data());
		} else if (block.kind == eng::MemoryKind::Chip) {
			m_memory->chip().release(block.view.data());
		}
		block.invalidate();
		block = {};
	}

	bool m_ready = false;
	eng::Ref<MemoryManager> m_memory {};
	u32 m_buffer_size = 0;
	u32 m_plugin_buffer_size = 0;
	eng::Block<eng::MixerBufferTag, eng::MemoryKind::Chip> m_buffer {};
	eng::Block<eng::MixerBufferTag> m_plugin_buffer {};
	eng::Block<eng::MixerBufferTag> m_plugin_data {};
};

} // namespace eng::audio
