// ============================================================================
// Demo 276: musica real (3 canales HW) + mixer de SFX (4 voces SW) — modo `Game`.
// ============================================================================
//
// Junta los dos subsistemas del modo `Game` de `eng::audio`:
//   - **musica**: `jazzcat-boogie_town.mod` (ProTracker real, 4 canales) por
//     `PtPlayer` (CIA), silenciando AUD0 (`set_music_channel_mask(0x0E)`) para que
//     el mixer lo use → la musica suena a **3 voces HW**.
//   - **SFX**: 4 samples de percusion reales (st-xx) por **una sola voz HW** con el
//     mixer de Photon (**4 voces SW**): bombo/caja/hihat/palmas con patron de 16 pasos.
//
// Evidencia: a frame 120, `detail` = nº de golpes de mixer disparados y `frame` =
// contadores por instrumento (kick/snare/hihat/claps), leidos por canal lateral.
//
// Build/run:
//   bash tools/build/build-demo.sh demos/amiga/276_music_mixer --debug
//   WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/276_music_mixer --warp

#include <eng/api/api.hpp>
#include <eng/audio/game_audio.hpp>
#include <eng/audio/sfx_mixer.hpp>
#include <eng/core/types/span.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/os/file.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

// Modulo incrustado en Chip RAM. Se elige con `-DMED_MOD=<n>` (por defecto P61 3-canales):
//   0 = testmod.p61 (5 KB, formato P61A)     -> MusicFormat::P61   (defecto)
//   1 = SneakyChick.mod (87 KB, M.K.)        -> MusicFormat::Protracker
//   2 = jazzcat-boogie_town.mod (241 KB)     -> MusicFormat::Protracker (estrangula el mixer)
#ifndef MED_MOD
#define MED_MOD 0
#endif

// La etiqueta va ANTES del incbin (si va despues apunta al final del modulo), y
// `g_mod_end` en la MISMA directiva para que la distancia sea correcta.
// Con `MED_FROM_DISK` NO se incrusta nada (el modulo se carga de disco).
#if defined(MED_FROM_DISK)
__asm__(".section snd_mod.MEMF_ANY, \"aw\"\n.globl g_mod\ng_mod:\n"
	".globl g_mod_end\ng_mod_end:\n.balign 4\n");
#elif MED_MOD == 2
#define MED_MOD_USE_PROTRACKER 1
__asm__(".section snd_mod.MEMF_CHIP, \"aw\"\n.balign 4\n"
	".globl g_mod\ng_mod:\n.incbin \"assets/amiga/audio/jazzcat-boogie_town.mod\"\n"
	".globl g_mod_end\ng_mod_end:\n.balign 4\n");
#elif MED_MOD == 1
#define MED_MOD_USE_PROTRACKER 1
__asm__(".section snd_mod.MEMF_CHIP, \"aw\"\n.balign 4\n"
	".globl g_mod\ng_mod:\n.incbin \"assets/amiga/audio/SneakyChick.mod\"\n"
	".globl g_mod_end\ng_mod_end:\n.balign 4\n");
#else
#define MED_MOD_USE_PROTRACKER 0
__asm__(".section snd_mod.MEMF_CHIP, \"aw\"\n.balign 4\n"
	".globl g_mod\ng_mod:\n.incbin \"assets/amiga/audio/testmod.p61\"\n"
	".globl g_mod_end\ng_mod_end:\n.balign 4\n");
#endif
__asm__(
	// SFX reales de st-xx (8-bit con signo, 11025 Hz, pico/4 para sumar 4 voces).
	".globl g_kick\ng_kick:\n.incbin \"out/assets/audio/kick_mix.raw\"\n"
	".globl g_kick_end\ng_kick_end:\n"
	".globl g_snare\ng_snare:\n.incbin \"out/assets/audio/snare_mix.raw\"\n"
	".globl g_snare_end\ng_snare_end:\n"
	".globl g_hihat\ng_hihat:\n.incbin \"out/assets/audio/hihat_mix.raw\"\n"
	".globl g_hihat_end\ng_hihat_end:\n"
	".globl g_claps\ng_claps:\n.incbin \"out/assets/audio/claps_mix.raw\"\n"
	".globl g_claps_end\ng_claps_end:\n"
	".balign 4\n");

extern "C" const eng::u8 g_mod[], g_mod_end[];
extern "C" const eng::u8 g_kick[], g_kick_end[];
extern "C" const eng::u8 g_snare[], g_snare_end[];
extern "C" const eng::u8 g_hihat[], g_hihat_end[];
extern "C" const eng::u8 g_claps[], g_claps_end[];

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

constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u8 kPlanes = 4u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;

constexpr eng::u16 kStepTicks = 6u;   // ~122 BPM (semicorcheas) a ~49 Hz
constexpr eng::u16 kTickHz = 49u;
constexpr eng::u32 kSampleRate = 11025u;

struct MusicMixerDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: planos+copper+mixer + el modulo cargado de disco (hasta ~250 KB con jazzcat).
#if defined(MED_FROM_DISK)
		constexpr eng::u32 kChipBytes = 320u * 1024u;
#else
		constexpr eng::u32 kChipBytes = 96u * 1024u;
#endif
		if (!backend.configure_memory({kChipBytes, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027601u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes * kPlanes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(1024, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027602u);
			return;
		}
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027603u);
			return;
		}

		// 0) Cargar el modulo desde DISCO **ANTES del takeover**: `dos.library` necesita
		// interrupciones/`DoIO`, que el takeover apaga. El fichero lo publica
		// `tools/fs/make-volume.mjs` en `data/audio/`.
#if defined(MED_FROM_DISK)
		const eng::os::FileHandle h =
			eng::os::file_open(MED_FROM_DISK, eng::os::FileMode::Read);
		if (h == 0u) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027607u);
			return;
		}
		const eng::u32 bytes = eng::os::file_size(h);
		m_disk_mod = backend.memory().chip.allocate_block<eng::AudioTag>(bytes, 4);
		if (!m_disk_mod.valid() ||
		    eng::os::file_read_sync(h, eng::Span<eng::u8>(m_disk_mod.view.data(), bytes), 0u) < 0) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027608u);
			return;
		}
		eng::os::file_close(h);
#else
		const eng::u32 bytes = static_cast<eng::u32>(g_mod_end - g_mod);
		m_disk_mod = backend.memory().chip.allocate_block<eng::AudioTag>(bytes, 4);
		for (eng::u32 i = 0; i < bytes; ++i) {
			m_disk_mod.view.data()[i] = g_mod[i];
		}
#endif

		backend.takeover_display(m_copper_ptr);
		m_audio.attach(backend.audio());

		// 1) Musica PRIMERO (el playroutine inicializa los 4 canales). Modulo en Chip.
		eng::audio::MusicModule mod {
			eng::Span<const eng::u8>(m_disk_mod.view.as_const().data(), bytes)};
#if defined(MED_FROM_DISK)
#define MED_DISK_IS_PROTRACKER 1
#else
#define MED_DISK_IS_PROTRACKER MED_MOD_USE_PROTRACKER
#endif
#if MED_DISK_IS_PROTRACKER
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
#else
		// P61: si el modulo trae los samples empaquetados (bit 6 del byte 3), P61_Init
		// exige un buffer de descompresion. Su tamano esta en el offset 4 del modulo.
		eng::Span<eng::u8> mod_buf {};
		if (eng::audio::p61_needs_sample_buffer(mod.data)) {
			const eng::u32 need = eng::audio::p61_sample_buffer_size(mod.data);
			m_mod_buf = backend.memory().chip.allocate_block<eng::AudioTag>(need, 4);
			if (!m_mod_buf.valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00027606u);
				return;
			}
			mod_buf = eng::Span<eng::u8>(m_mod_buf.view.data(), need);
		}
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::P61, mod_buf);
#endif
		if (!m_music_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027604u);
			return;
		}

		// 2) El mixer (SFX) reserva AUD0; la musica sigue en AUD1..AUD3 (3 voces HW).
		if (!m_audio.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027605u);
			return;
		}
		m_audio.set_music_channel_mask(0x0Eu); // AUD1..AUD3 = musica; AUD0 = mixer

		m_data[0] = g_kick;  m_len[0] = static_cast<eng::u32>(g_kick_end - g_kick);
		m_data[1] = g_snare; m_len[1] = static_cast<eng::u32>(g_snare_end - g_snare);
		m_data[2] = g_hihat; m_len[2] = static_cast<eng::u32>(g_hihat_end - g_hihat);
		m_data[3] = g_claps; m_len[3] = static_cast<eng::u32>(g_claps_end - g_claps);
		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		const eng::u16 now = m_audio.system().sfx().counter();
		eng::u16 elapsed = static_cast<eng::u16>(now - m_last);
		if (elapsed > 32u) {
			elapsed = 32u;
		}
		for (eng::u16 i = 0; i < elapsed; ++i) {
			if (++m_tick >= kStepTicks) {
				m_tick = 0;
				const eng::u32 s = m_step % 16u;
				if ((s & 3u) == 0u)      hit(0u, eng::audio::MixCh0); // 0,4,8,12
				if (s == 4u || s == 12u) hit(1u, eng::audio::MixCh1);
				if ((s & 1u) == 0u)      hit(2u, eng::audio::MixCh2);
				if (s == 12u)            hit(3u, eng::audio::MixCh3);
				++m_step;
			}
		}
		m_last = now;

		if (!m_confirmed && context.frame.frame_index >= 120u) {
			// DIAG: contador del mixer (si avanza) y DMACONR (DMA de audio vivo).
			const eng::u16 cnt = m_audio.system().sfx().counter();
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			eng::debug::mark_ready(g_eng_run_status, m_triggers);
			g_eng_run_status.frame = ((static_cast<eng::u32>(cnt) & 0xffu) << 24u) |
						 ((static_cast<eng::u32>(dmaconr) & 0xffu) << 16u) |
						 ((m_count[0] & 0xffu) << 8u) | (m_count[1] & 0xffu);
			m_confirmed = true;
		}
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void hit(eng::u32 k, eng::u16 channel) {
		const eng::audio::SfxSample s {eng::Span<const eng::u8>(m_data[k], m_len[k])};
		if (m_audio.system().sfx().play_on(channel, s, 1, eng::audio::LoopMode::Once) >= 0) {
			++m_triggers;
			++m_count[k];
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched {m_copper_block};
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.view, kPlaneBytes);
		sched.move(eng::copper::color_register(0), 0x001u);
		sched.move(eng::copper::color_register(1), 0x00Fu);
		sched.end();
		m_copper_ptr = sched.data();
		return sched.ok();
	}

	bool m_init_ok = false;
	bool m_music_ok = false;
	bool m_confirmed = false;
	eng::u32 m_triggers = 0;
	eng::u32 m_step = 0;
	eng::u16 m_tick = 0;
	eng::u16 m_last = 0;
	eng::u32 m_count[4] = {0u, 0u, 0u, 0u};
	const eng::u8* m_data[4] = {nullptr, nullptr, nullptr, nullptr};
	eng::u32 m_len[4] = {0u, 0u, 0u, 0u};
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::AudioTag> m_mod_buf {}; ///< samples empaquetados P61 (si el modulo los trae)
	eng::Block<eng::AudioTag> m_disk_mod {}; ///< modulo cargado desde disco (MED_FROM_DISK)
	eng::audio::GameAudio m_audio {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	MusicMixerDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
