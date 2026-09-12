// ============================================================================
// Demo 071: "mixer four voices" — CUATRO voces pre-renderizadas (máximo single).
// ============================================================================
//
// Paso 4 del plan incremental del mixer: las cuatro voces software del Audio
// Mixer 3.7 (MIXER_SINGLE) a la vez, cada una su propia melodía pre-renderizada:
//   - Voz 1 (MixCh0): melodía aguda del "Himno a la Alegría".
//   - Voz 2 (MixCh1): bajo grave y lento (A A F G).
//   - Voz 3 (MixCh2): contramelodía media (C D C A).
//   - Voz 4 (MixCh3): línea aguda descendente (A G F E).
//
// Amplitud de cada muestra = 120/4 = ±30 para que la suma (±120) no desborde.
// AUD1..AUD3 en silencio (el mixer usa solo AUD0).
//
// Evidencia: osciloscopio del buffer + estadísticas en `detail`, interrupciones
// del mixer en `frame`.

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
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u32 kSampleRate = 11025;
constexpr eng::s32 kVoices = 4;
constexpr eng::s32 kAmplitude = 120 / kVoices; // ±30 -> suma ±120

constexpr eng::u32 kLen = 12800; // todas las muestras duran igual (mismo bucle)

constexpr eng::u16 kV1Freq[] = { 330, 330, 349, 392, 392, 349, 330, 294 };
constexpr eng::u32 kV1Count = sizeof(kV1Freq) / sizeof(kV1Freq[0]);
constexpr eng::u32 kV1Note = kLen / kV1Count;      // 1600

constexpr eng::u16 kV2Freq[] = { 110, 110, 87, 98 };
constexpr eng::u32 kV2Count = sizeof(kV2Freq) / sizeof(kV2Freq[0]);
constexpr eng::u32 kV2Note = kLen / kV2Count;      // 3200

constexpr eng::u16 kV3Freq[] = { 262, 294, 262, 220 };
constexpr eng::u32 kV3Count = sizeof(kV3Freq) / sizeof(kV3Freq[0]);
constexpr eng::u32 kV3Note = kLen / kV3Count;      // 3200

constexpr eng::u16 kV4Freq[] = { 440, 392, 349, 330 };
constexpr eng::u32 kV4Count = sizeof(kV4Freq) / sizeof(kV4Freq[0]);
constexpr eng::u32 kV4Note = kLen / kV4Count;      // 3200

struct FourVoicesDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00007101u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_v1_block = backend.memory().chip.allocate(kLen, 4);
		m_v2_block = backend.memory().chip.allocate(kLen, 4);
		m_v3_block = backend.memory().chip.allocate(kLen, 4);
		m_v4_block = backend.memory().chip.allocate(kLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() ||
			!m_v1_block.valid() || !m_v2_block.valid() || !m_v3_block.valid() || !m_v4_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007102u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		eng::audio::synth_sequence<kV1Note>(static_cast<eng::u8*>(m_v1_block.data), kV1Freq, kV1Count, kSampleRate, static_cast<eng::s16>(kAmplitude));
		eng::audio::synth_sequence<kV2Note>(static_cast<eng::u8*>(m_v2_block.data), kV2Freq, kV2Count, kSampleRate, static_cast<eng::s16>(kAmplitude));
		eng::audio::synth_sequence<kV3Note>(static_cast<eng::u8*>(m_v3_block.data), kV3Freq, kV3Count, kSampleRate, static_cast<eng::s16>(kAmplitude));
		eng::audio::synth_sequence<kV4Note>(static_cast<eng::u8*>(m_v4_block.data), kV4Freq, kV4Count, kSampleRate, static_cast<eng::s16>(kAmplitude));

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00007103u); return; }
		backend.takeover_display(m_copper_ptr);

		if (!m_sfx.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007104u);
			return;
		}
		m_sfx.set_master_volume(64);

		m_ch0 = m_sfx.play_on(eng::audio::MixCh0, sample(m_v1_block), 1, eng::audio::LoopMode::Loop);
		m_ch1 = m_sfx.play_on(eng::audio::MixCh1, sample(m_v2_block), 1, eng::audio::LoopMode::Loop);
		m_ch2 = m_sfx.play_on(eng::audio::MixCh2, sample(m_v3_block), 1, eng::audio::LoopMode::Loop);
		m_ch3 = m_sfx.play_on(eng::audio::MixCh3, sample(m_v4_block), 1, eng::audio::LoopMode::Loop);
		if (m_ch0 < 0 || m_ch1 < 0 || m_ch2 < 0 || m_ch3 < 0) {
			eng::debug::mark_failed(g_eng_run_status,
				0x00007105u | (m_ch0 < 0 ? 1u : 0u) | (m_ch1 < 0 ? 2u : 0u) |
				(m_ch2 < 0 ? 4u : 0u) | (m_ch3 < 0 ? 8u : 0u));
			return;
		}
		m_sfx.reset_counter();

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		if (context.frame.frame_index >= 5u) {
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
				(static_cast<eng::u32>(vmax) << 24u) |
				(static_cast<eng::u32>(vmin) << 16u) |
				(static_cast<eng::u32>(changes & 0xffffu)));
			g_eng_run_status.frame = static_cast<eng::u32>(m_sfx.counter());
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
	eng::audio::SfxSample sample(eng::MemoryBlock& block) {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(block.data), kLen) };
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
		const eng::u16 bplcon0 = static_cast<eng::u16>((static_cast<eng::u16>(kPlanes) << 12u) | 0x0200u);
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, bplcon0, kPlanes, m_bitplanes, kPlaneBytes
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
	eng::u16 m_copper_words = 0;
	eng::audio::SfxChannel m_ch0 = -1;
	eng::audio::SfxChannel m_ch1 = -1;
	eng::audio::SfxChannel m_ch2 = -1;
	eng::audio::SfxChannel m_ch3 = -1;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_v1_block {};
	eng::MemoryBlock m_v2_block {};
	eng::MemoryBlock m_v3_block {};
	eng::MemoryBlock m_v4_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::SfxMixer m_sfx {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	FourVoicesDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
