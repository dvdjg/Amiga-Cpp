// ============================================================================
// Demo 075: "mixer tones" — tonos puros por las 4 voces, activados por un
// contador binario de 4 bits que cambia cada ~0.5 s.
// ============================================================================
//
// Prueba de diagnóstico del mixer: las 4 voces tocan tonos puros de 300, 600,
// 1200 y 2400 Hz, y la voz k está ACTIVA cuando el bit k del contador está a 1.
// Recorre 0..15 (0 = silencio, 1 = solo 300 Hz, 3 = 300+600, ... 15 = las 4) y
// vuelve a empezar. Así se oye cada voz por separado y todas las combinaciones.
//
// El contador avanza cada ~0.5 s usando el reloj de audio del mixer (~49 Hz:
// 25 interrupciones ≈ 0.5 s), independiente de los fps del bucle principal.
//
// Cada tono es un bucle de 588 muestras (16 ciclos exactos de 300 Hz a 11025 Hz,
// múltiplo de 4) con amplitud ±120/4 = ±30.

#include <eng/audio/sfx_mixer.hpp>
#include <eng/audio/wave_tables.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

constexpr eng::u32 kSampleRate = 11025;
constexpr eng::u32 kToneLen = 588;              // 16 ciclos de 300 Hz, %4 == 0
constexpr eng::s16 kAmplitude = 30;             // 120/4
constexpr eng::u16 kFreq[4] = { 300, 600, 1200, 2400 };
constexpr eng::u16 kTicksPerStep = 25;          // ~0.5 s a ~49 Hz

struct TonesDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00007501u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_tone_block = backend.memory().chip.allocate(kToneLen * 4u, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_tone_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007502u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		for (eng::u32 k = 0; k < 4u; ++k) {
			gen_tone(static_cast<eng::u8*>(m_tone_block.data) + k * kToneLen, kFreq[k]);
		}

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00007503u); return; }
		backend.takeover_display(m_copper_ptr);

		if (!m_sfx.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007504u);
			return;
		}
		m_sfx.set_master_volume(64);

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		// Reloj del mixer (~49 Hz) para el contador de 4 bits (~0.5 s por paso).
		const eng::u16 now = m_sfx.counter();
		eng::u16 elapsed = static_cast<eng::u16>(now - m_last);
		if (elapsed > 32u) elapsed = 32u;
		for (eng::u16 i = 0; i < elapsed; ++i) {
			if (++m_tick >= kTicksPerStep) {
				m_tick = 0;
				m_step = static_cast<eng::u8>((m_step + 1u) & 0x0fu);
				apply_step(m_step);
			}
		}
		m_last = now;

		if (!m_confirmed && context.frame.frame_index >= 60u) {
			const eng::u8* buf = m_sfx.buffer();
			const eng::u32 n = m_sfx.buffer_bytes();
			eng::u8 vmin = 0xffu, vmax = 0u;
			eng::u32 changes = 0;
			for (eng::u32 i = 0; i < n; ++i) {
				const eng::u8 v = buf[i];
				if (v < vmin) vmin = v;
				if (v > vmax) vmax = v;
				if (i > 0u && ((buf[i - 1u] ^ v) & 0x80u) != 0u) ++changes;
			}
			eng::debug::mark_ready(g_eng_run_status,
				static_cast<eng::u32>(m_step) | (static_cast<eng::u32>(m_on_count) << 8u));
			g_eng_run_status.frame = (static_cast<eng::u32>(vmax) << 24u) |
				(static_cast<eng::u32>(vmin) << 16u) | (changes & 0xffffu);
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		draw_scope();
		if (build_copper()) backend.install_copper_list(m_copper_ptr);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Activa/desactiva cada voz según los bits del contador.
	void apply_step(eng::u8 value) {
		m_on_count = 0;
		for (eng::u32 k = 0; k < 4u; ++k) {
			const bool want = (value & (1u << k)) != 0u;
			if (want && !m_on[k]) {
				eng::audio::SfxSample s { eng::Span<const eng::u8>(
					static_cast<const eng::u8*>(m_tone_block.data) + k * kToneLen, kToneLen) };
				m_ch[k] = m_sfx.play_on(static_cast<eng::u16>(eng::audio::MixCh0 << k), s, 1, eng::audio::LoopMode::Loop);
				m_on[k] = true;
			} else if (!want && m_on[k]) {
				if (m_ch[k] >= 0) m_sfx.stop(m_ch[k]);
				m_ch[k] = -1;
				m_on[k] = false;
			}
			if (m_on[k]) ++m_on_count;
		}
	}

	/// Genera un tono con bucle SIN discontinuidad: la fase en la muestra `i` es
	/// exactamente `frac(i*f/rate)` usando la posición entera dentro del ciclo
	/// (`p = (i*cycles) % L`), de modo que `p(L) = 0` y el bucle es continuo.
	/// (Con un acumulador `inc` entero, la fase no cierra y aparece un clic cada
	/// bucle que se hace más audible a mayor frecuencia.)
	void gen_tone(eng::u8* dst, eng::u16 f) {
		const eng::u32 L = kToneLen;
		const eng::u32 cycles = (L * static_cast<eng::u32>(f)) / kSampleRate; // entero exacto
		for (eng::u32 i = 0; i < L; ++i) {
			const eng::u32 p = (i * cycles) % L;      // 0..L-1
			const eng::u32 t = p * 64u;
			const eng::u32 idx = (t / L) & 63u;       // índice de tabla
			const eng::u32 frac = ((t % L) * 256u) / L;
			const eng::s16 a = eng::audio::sine_byte(idx);
			const eng::s16 b = eng::audio::sine_byte(idx + 1u);
			const eng::s16 v = static_cast<eng::s16>(a + (((b - a) * static_cast<eng::s32>(frac)) >> 8));
			dst[i] = static_cast<eng::u8>(static_cast<eng::s8>((static_cast<eng::s32>(v) * kAmplitude) / 127));
		}
	}

	void draw_scope() {
		const eng::u8* buf = m_sfx.buffer();
		const eng::u32 n = m_sfx.buffer_bytes();
		for (eng::u32 i = 0; i < kPlaneBytes; ++i) m_bitplanes[i] = 0;
		if (n == 0u) return;
		for (eng::u16 x = 0; x < 320u; ++x) {
			const eng::u32 si = (static_cast<eng::u32>(x) * n) / 320u;
			const eng::u16 y = static_cast<eng::u16>(255u - buf[si]);
			set_pixel(x, y);
			if (y + 1u < 256u) set_pixel(x, static_cast<eng::u16>(y + 1u));
		}
		for (eng::u16 x = 0; x < 320u; ++x) set_pixel(x, 127u);
	}

	void set_pixel(eng::u16 x, eng::u16 y) {
		if (x >= 320u || y >= 256u) return;
		m_bitplanes[static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u)]
			|= static_cast<eng::u8>(1u << (7u - (x & 7u)));
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		for (eng::u8 i = 0; i < 32; ++i) {
			sched.move(eng::copper::color_register(i), 0x0000);
		}
		sched.move(eng::copper::color_register(0), 0x000fu);
		sched.move(eng::copper::color_register(1), 0x0fffu);
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
	bool m_init_ok = false;
	bool m_confirmed = false;
	eng::u8 m_step = 0;
	eng::u8 m_on_count = 0;
	eng::u16 m_tick = 0;
	eng::u16 m_last = 0;
	eng::u16 m_copper_words = 0;
	bool m_on[4] = { false, false, false, false };
	eng::audio::SfxChannel m_ch[4] = { -1, -1, -1, -1 };
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_tone_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::SfxMixer m_sfx {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	TonesDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
