// ============================================================================
// Demo 058: "sfx mixer" — efectos de sonido por el Audio Mixer 3.7 (Photon).
// ============================================================================
//
// Demuestra el soporte nativo del engine para el Audio Mixer 3.7: la capa de
// juego usa `eng::audio::SfxMixer` (abstracción C++23), que envuelve el mixer
// ASM de `support/audio_mixer/` (ensamblado con VASM).
//
// Qué suena: un "alarma" en bucle por el canal 0. FIRE dispara un "beep" corto
// (prioridad alta). Arriba/abajo cambia el volumen maestro (barra cian).
//
// Las muestras se generan ya PREPROCESADAS en init: amplitud ±24 (dentro del
// rango ±32 que exige mixer_sw_channels=4) y longitud múltiplo de 4 bytes.
// Ver docs/engine/audio/AUDIO_MIXER.md para los requisitos completos.
//
// Evidencia: `mark_ready` guarda en `detail` el valor de DMACONR (bit 0 = AUD0EN)
// y el número de canales del mixer, para verificar que el DMA de audio arrancó.

#include <eng/audio/sfx_mixer.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/input_poll.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

// Gancho directo 1..9 (runner --automation-key) para disparar el beep.
extern "C" {
__attribute__((used)) volatile eng::u8 g_tech_new = 0;
}

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kScreenW = 320;
constexpr eng::u16 kScreenH = 256;

constexpr eng::u32 kBeepLen = 128;   // múltiplo de 4
constexpr eng::u32 kAlarmLen = 1024; // múltiplo de 4

constexpr eng::u8  kRainbowLen = 32;
constexpr eng::u8  kBands = 16;
constexpr eng::u16 kBandTop0 = 40;
constexpr eng::u16 kBandHeight = 16;

constexpr eng::u16 kRainbow[kRainbowLen] = {
	0xf00, 0xf40, 0xf80, 0xfb0, 0xff0, 0xcf0, 0x8f0, 0x4f0,
	0x0f0, 0x0f4, 0x0f8, 0x0fc, 0x0ff, 0x0cf, 0x08f, 0x04f,
	0x00f, 0x40f, 0x80f, 0xc0f, 0xf0f, 0xf0c, 0xf08, 0xf04,
	0xf00, 0xc00, 0x800, 0x400, 0x000, 0x222, 0x444, 0x666,
};

struct SfxMixerDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005801u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005802u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		// Muestras preprocesadas (amplitud ±24, múltiplos de 4).
		m_beep_block = backend.memory().chip.allocate(kBeepLen, 4);
		m_alarm_block = backend.memory().chip.allocate(kAlarmLen, 4);
		if (!m_beep_block.valid() || !m_alarm_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005803u);
			return;
		}
		gen_square(static_cast<eng::u8*>(m_beep_block.data), kBeepLen, 4);
		gen_square(static_cast<eng::u8*>(m_alarm_block.data), kAlarmLen, 64);

		for (eng::u8 i = 0; i < 32; ++i) {
			m_palette[i] = static_cast<eng::u16>(i * 0x111u);
		}
		m_palette[0] = 0x000;
		m_palette[1] = 0x0ff; // barra de volumen en cian

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005804u);
			return;
		}

		// Orden: primero el display (congela el sistema), luego el mixer.
		backend.takeover_display(m_copper_ptr);

		if (!m_sfx.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005805u);
			return;
		}

		// Alarma en bucle por el canal 0; guarda el canal para el stop de la demo.
		m_alarm_channel = m_sfx.play_on(eng::audio::MixCh0, alarm_sample(), 1, eng::audio::LoopMode::Loop);

		const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
		m_total_channels = m_sfx.total_channels();
		eng::debug::mark_ready(
			g_eng_run_status,
			(static_cast<eng::u32>(dmaconr) << 16u) | static_cast<eng::u32>(m_total_channels)
		);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::amiga::GameInput gin;
		eng::amiga::poll_input(gin);

		// FIRE o tecla 1..9 → beep (prioridad alta, una vez).
		if (gin.port0 != 0u || g_tech_new != 0u) {
			g_tech_new = 0u;
			m_sfx.play(beep_sample(), 3, eng::audio::LoopMode::Once);
		}

		// Arriba/abajo → volumen maestro (0..64).
		if (gin.port0 & eng::amiga::kJoyUp) {
			if (m_volume < 64u) m_volume += 2u;
		}
		if (gin.port0 & eng::amiga::kJoyDown) {
			if (m_volume > 0u) m_volume -= 2u;
		}
		m_sfx.set_master_volume(m_volume);

		// Dibujo: barra cian cuya altura refleja el volumen maestro.
		clear_plane0();
		const eng::u16 bar_h = static_cast<eng::u16>((static_cast<eng::u32>(m_volume) * 200u) / 64u + 16u);
		fill_rect(m_bitplanes, 156, static_cast<eng::s16>(kScreenH - bar_h), 9, static_cast<eng::s16>(bar_h));

		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
		(void)context;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::audio::SfxSample beep_sample() {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_beep_block.data), kBeepLen) };
	}
	eng::audio::SfxSample alarm_sample() {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_alarm_block.data), kAlarmLen) };
	}

	/// Genera una onda cuadrada con signo (8 bits) de amplitud ±24.
	/// `half` = samples por semiciclo (más pequeño = tono más agudo).
	void gen_square(eng::u8* dst, eng::u32 len, eng::u32 half) {
		for (eng::u32 i = 0; i < len; ++i) {
			dst[i] = ((i / half) & 1u) ? 24u : static_cast<eng::u8>(256u - 24u);
		}
	}

	void set_pixel(eng::u8* plane0, eng::u16 x, eng::u16 y) {
		if (x >= kScreenW || y >= kScreenH) return;
		plane0[static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u)]
			|= static_cast<eng::u8>(1u << (7u - (x & 7u)));
	}

	void fill_rect(eng::u8* plane0, eng::s16 x0, eng::s16 y0, eng::s16 w, eng::s16 h) {
		for (eng::s16 y = y0; y < y0 + h; ++y)
			for (eng::s16 x = x0; x < x0 + w; ++x)
				if (x >= 0 && y >= 0) set_pixel(plane0, static_cast<eng::u16>(x), static_cast<eng::u16>(y));
	}

	void clear_plane0() {
		eng::u16* words = reinterpret_cast<eng::u16*>(m_bitplanes);
		for (eng::u32 i = 0; i < kPlaneBytes / 2u; ++i) words[i] = 0;
	}

	void build_bands() {
		for (eng::u8 b = 0; b < kBands; ++b) {
			eng::graphics::CopperIntent& it = m_intents[b];
			it.kind = eng::graphics::CopperIntentKind::PaletteLine;
			it.top = static_cast<eng::u16>(kBandTop0 + static_cast<eng::u16>(b) * kBandHeight);
			it.bottom = it.top;
			it.hpos = 0;
			it.colors = &kRainbow[(static_cast<eng::u8>(b * 2u)) & (kRainbowLen - 1u)];
			it.first = 0;
			it.count = 1;
		}
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		sched.emit_palette(m_palette);
		build_bands();
		sched.emit_copper_intents(m_intents, kBands);
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	eng::u8 m_volume = 48;
	eng::u32 m_total_channels = 0;
	eng::audio::SfxChannel m_alarm_channel = -1;
	eng::u16 m_palette[32] {};
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_beep_block {};
	eng::MemoryBlock m_alarm_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::graphics::CopperIntent m_intents[kBands] {};
	eng::audio::SfxMixer m_sfx {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SfxMixerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
