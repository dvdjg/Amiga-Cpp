// ============================================================================
// Demo 057: "audio mixer" — tono de Paula vía AudioMixer → AudioPlan → Paula.
// ============================================================================
//
// Demuestra el paso 7 de ENGINE_DESIGN.md §5: la cadena completa de audio.
//   `SampleEvent` → `eng::audio::AudioMixer` (lógica pura) → `AudioPlan`
//   → `eng::amiga::PaulaAudio` (escribe registros de Paula + arranca el DMA).
//
// Qué muestra/suena: un tono cuadrado en bucle por el canal 0. El joystick
// arriba/abajo cambia el tono (período de Paula); FIRE lo silencia (volumen 0).
// Visualmente, un degradado arcoíris de fondo con una barra central cuya altura
// refleja el tono (más agudo = más alta) y que se apaga al silenciar.
//
// Evidencia: `mark_ready` guarda en `detail` el valor leído de DMACONR tras
// arrancar el audio (bit 0 = AUD0EN, bit 9 = DMAEN), para verificar por el
// canal lateral que el DMA de audio quedó activo.

#include <eng/audio/audio.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/input/input.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/audio_paula.hpp>
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

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kScreenW = 320;
constexpr eng::u16 kScreenH = 256;

constexpr eng::u32 kSampleBytes = 64;   // 32 words, tono cuadrado
constexpr eng::u16 kSampleWords = kSampleBytes / 2u;

constexpr eng::u16 kPeriodMin = 124;    // límite de Paula (AHRM)
constexpr eng::u16 kPeriodMax = 1024;
constexpr eng::u16 kPeriodStep = 16;

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

struct AudioMixerDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005701u);
			return;
		}

		m_sample_block = backend.memory().chip.allocate(kSampleBytes, 4);
		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		if (!m_sample_block.valid() || !m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005702u);
			return;
		}
		m_sample = static_cast<eng::u8*>(m_sample_block.data);
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		// Tono cuadrado: primera mitad +127, segunda mitad -128 (8 bits con signo).
		for (eng::u32 i = 0; i < kSampleBytes; ++i) {
			m_sample[i] = (i < kSampleBytes / 2u) ? 0x7fu : 0x80u;
		}

		for (eng::u8 i = 0; i < 32; ++i) {
			m_palette[i] = static_cast<eng::u16>(i * 0x111u);
		}
		m_palette[0] = 0x000;
		m_palette[1] = 0x0ff; // barra de tono en cian

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005703u);
			return;
		}

		// Orden canónico: primero se toma el display (congela el DMA del sistema,
		// dejando solo master+copper+bitplane) y DESPUÉS se arranca el audio, para
		// que DMACONR refleje el estado limpio del engine.
		backend.takeover_display(m_copper_ptr);
		apply_audio();
		const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);

		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(dmaconr));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::input::InputAggregator agg;
		eng::amiga::poll_input(agg);

		// Tono: arriba = más agudo (período menor), abajo = más grave.
		if (agg.pad0.up && m_period > kPeriodMin) {
			m_period = static_cast<eng::u16>(m_period - kPeriodStep);
		}
		if (agg.pad0.down && m_period < kPeriodMax) {
			m_period = static_cast<eng::u16>(m_period + kPeriodStep);
		}
		// FIRE silencia (volumen 0); sin FIRE, volumen fijo.
		m_volume = agg.pad0.fire ? 0u : 48u;

		apply_audio();

		// Dibujo: barra central cuya altura refleja el tono (más agudo = más alta).
		clear_plane0();
		const eng::u16 bar_h = static_cast<eng::u16>(
			(static_cast<eng::u32>(kPeriodMax - m_period) * 200u) / (kPeriodMax - kPeriodMin) + 16u
		);
		if (m_volume != 0u) {
			fill_rect(m_bitplanes, 156, static_cast<eng::s16>(kScreenH - bar_h), 9, static_cast<eng::s16>(bar_h));
		}

		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
		(void)context;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Construye el `AudioPlan` con el `AudioMixer` y lo materializa en Paula.
	void apply_audio() {
		m_mixer.begin_frame();
		eng::audio::SampleEvent ev;
		ev.sample = m_sample;
		ev.length_words = kSampleWords;
		ev.period = m_period;
		ev.volume = m_volume;
		ev.channel_hint = 0; // canal 0 (salida derecha)
		m_mixer.play(ev);
		m_paula.apply(m_mixer.plan());
	}

	void set_pixel(eng::u8* plane0, eng::u16 x, eng::u16 y) {
		if (x >= kScreenW || y >= kScreenH) {
			return;
		}
		plane0[static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u)]
			|= static_cast<eng::u8>(1u << (7u - (x & 7u)));
	}

	void fill_rect(eng::u8* plane0, eng::s16 x0, eng::s16 y0, eng::s16 w, eng::s16 h) {
		for (eng::s16 y = y0; y < y0 + h; ++y) {
			for (eng::s16 x = x0; x < x0 + w; ++x) {
				if (x >= 0 && y >= 0) {
					set_pixel(plane0, static_cast<eng::u16>(x), static_cast<eng::u16>(y));
				}
			}
		}
	}

	void clear_plane0() {
		eng::u16* words = reinterpret_cast<eng::u16*>(m_bitplanes);
		for (eng::u32 i = 0; i < kPlaneBytes / 2u; ++i) {
			words[i] = 0;
		}
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
	eng::u16 m_period = 300;
	eng::u8 m_volume = 48;
	eng::u16 m_palette[32] {};
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_sample = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_sample_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::graphics::CopperIntent m_intents[kBands] {};
	eng::audio::AudioMixer m_mixer {};
	eng::amiga::PaulaAudio m_paula {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	AudioMixerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
