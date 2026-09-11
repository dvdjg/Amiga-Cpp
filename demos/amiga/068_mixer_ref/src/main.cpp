// ============================================================================
// Demo 068: "mixer ref" — test mínimo ESPEJO de CMixer.c (Audio Mixer 3.7).
// ============================================================================
//
// Replica EXACTAMENTE la secuencia del ejemplo C oficial del mixer
// (`AmigaAudioMixer/Examples/CMixerSource/CMixer.c`):
//   1. MixerGetBufferSize() -> buffer Chip.
//   2. MixerGetPluginsBufferSize() -> buffer de plugins (no NULL).
//   3. buffer de datos de plugin.
//   4. MixerSetup(buffer, plugin_buffer, plugin_data, MIX_PAL, plugin_data_size).
//   5. MixerInstallHandler(VBR=0, 0).
//   6. MixerStart().
//   7. MXEffect GLOBAL + MixerPlayFX(effect, DMAF_AUD2).
//
// Objetivo: aislar si el fallo (salida ≈ silencio) está en el wrapper SfxMixer
// o en la propia integración/config del mixer. Aquí se usan las rutinas ASM
// crudas (namespace mixer_amiga), sin la capa de juego.
//
// Evidencia: osciloscopio del buffer de mezcla en pantalla + contador de
// interrupciones del mixer (MIXER_COUNTER=1) en `detail`/`frame`.

#include <eng/audio/sfx_mixer.hpp>
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

constexpr eng::u32 kSampleLen = 64;     // 32 words, onda cuadrada audible
constexpr eng::u32 kPluginDataLen = 64; // tamaño ficticio para D1 (no hay plugins)

// MXEffect GLOBAL (como en CMixer.c, no local en la pila).
eng::audio::mixer_amiga::MixerEffect g_effect {};

struct MixerRefDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006801u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_sample_block = backend.memory().chip.allocate(kSampleLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sample_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006802u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		build_square(static_cast<eng::u8*>(m_sample_block.data));

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006803u); return; }
		backend.takeover_display(m_copper_ptr);

		// --- Secuencia ESPEJO de CMixer.c -------------------------------------
		const eng::u32 buffer_size = eng::audio::mixer_amiga::get_buffer_size();
		// NOTA: MixerGetPluginsBufferSize() es un no-op con MIXER_ENABLE_PLUGINS=0
		// (devuelve basura), así que usamos un tamaño fijo conocido.
		const eng::u32 plugin_buffer_size = eng::audio::kPluginBufferBytes;
		m_buffer_block = backend.memory().chip.allocate(buffer_size, 4);
		m_plugin_buffer_block = backend.memory().slow.allocate(plugin_buffer_size, 4);
		m_plugin_data_block = backend.memory().slow.allocate(kPluginDataLen, 4);
		if (!m_buffer_block.valid() || !m_plugin_buffer_block.valid() || !m_plugin_data_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006804u);
			return;
		}

		eng::audio::mixer_amiga::setup(
			m_buffer_block.data, m_plugin_buffer_block.data, m_plugin_data_block.data,
			eng::audio::MixPal, static_cast<eng::u16>(kPluginDataLen));
		eng::audio::mixer_amiga::install_handler(nullptr, 0);
		eng::audio::mixer_amiga::start();

		g_effect.length = static_cast<eng::s32>(kSampleLen);
		g_effect.sample = static_cast<const eng::u8*>(m_sample_block.data);
		g_effect.loop = static_cast<eng::s16>(eng::audio::LoopMode::Loop);
		g_effect.priority = 1;
		g_effect.loop_offset = 0;
		g_effect.plugin = nullptr;
		m_channel = eng::audio::mixer_amiga::play_fx(g_effect, eng::audio::DmaAud2);
		if (m_channel < 0) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006805u);
			return;
		}
		eng::audio::mixer_amiga::reset_counter();

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		if (context.frame.frame_index >= 60u) {
			// Analiza el buffer de mezcla (lo que Paula reproduce).
			const eng::u8* buf = static_cast<const eng::u8*>(m_buffer_block.data);
			const eng::u32 n = m_buffer_block.size;
			eng::u8 vmin = 0xffu, vmax = 0u;
			eng::u32 changes = 0;
			for (eng::u32 i = 0; i < n; ++i) {
				const eng::u8 v = buf[i];
				if (v < vmin) vmin = v;
				if (v > vmax) vmax = v;
				if (i > 0u && ((buf[i - 1u] ^ v) & 0x80u) != 0u) ++changes;
			}
			// bits 31-24 = pico, 23-16 = valle, 15-0 = cruces.
			const eng::u32 stats = (static_cast<eng::u32>(vmax) << 24u) |
				(static_cast<eng::u32>(vmin) << 16u) | (changes & 0xffffu);
			eng::debug::mark_ready(g_eng_run_status, stats);
			g_eng_run_status.frame = eng::audio::mixer_amiga::get_counter();
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
	/// Onda cuadrada (mitad +120, mitad -120), 64 bytes.
	void build_square(eng::u8* dst) {
		for (eng::u32 i = 0; i < kSampleLen; ++i) {
			dst[i] = (i < kSampleLen / 2u) ? 120u : static_cast<eng::u8>(136u);
		}
	}

	void draw_scope() {
		const eng::u8* buf = static_cast<const eng::u8*>(m_buffer_block.data);
		const eng::u32 n = m_buffer_block.size;
		for (eng::u32 i = 0; i < kPlaneBytes; ++i) m_bitplanes[i] = 0;
		if (buf == nullptr || n == 0u) return;
		for (eng::u16 x = 0; x < 320u; ++x) {
			const eng::u32 si = (static_cast<eng::u32>(x) * n) / 320u;
			const eng::u16 y = static_cast<eng::u16>(255u - buf[si]);
			set_pixel(x, y);
			if (y + 1u < 256u) set_pixel(x, static_cast<eng::u16>(y + 1u));
		}
		for (eng::u16 x = 0; x < 320u; ++x) set_pixel(x, 127u); // referencia silencio
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
		sched.move(eng::copper::color_register(0), 0x000fu); // fondo azul
		sched.move(eng::copper::color_register(1), 0x0fffu); // trazo
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
	eng::s32 m_channel = -1;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_sample_block {};
	eng::MemoryBlock m_buffer_block {};
	eng::MemoryBlock m_plugin_buffer_block {};
	eng::MemoryBlock m_plugin_data_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	MixerRefDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
