// ============================================================================
// Demo 217: ejemplo de juego completo (A6) — boot -> título -> gameplay -> pausa.
// ============================================================================
//
// Recorre el ciclo de vida del audio con la **superficie estable**: `GameAudio` (capa de juego:
// banco de SFX + política) sobre el `AudioSystem` del backend, más los **modos** (`AudioMode`).
//
//   BOOT      : silencio (aún sin música)
//   TITLE     : música Protracker en bucle (modo `Game`)
//   GAMEPLAY  : música + SFX por input (beep con cooldown; alarma con ducking)
//   PAUSE     : `AudioMode::Silent` (corte ordenado)
//
// Evidencia: `mark_ready` al cerrar el ciclo con el DMA de audio activo; el `detail` guarda
// DMACONR. Ver docs/engine/architecture/GAME_AUDIO.md §2/§7 y ROADMAP_AUDIO.md (A6).

#include <eng/audio/game_audio.hpp>
#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/input_poll.hpp>

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

constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u8  kPlanes = 1u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;

constexpr eng::u32 kModSize = 2364;
constexpr eng::u32 kSampleOffset = 2108;
constexpr eng::u32 kSampleLen = 256;
constexpr eng::u32 kAlarmLen = 1024;
constexpr eng::u32 kBeepLen = 128;

constexpr eng::u8 kSfxBeep = 0;
constexpr eng::u8 kSfxAlarm = 1;

/// Fases del ejemplo (frames).
constexpr eng::u32 kTitleAt = 20u;
constexpr eng::u32 kGameplayAt = 60u;
constexpr eng::u32 kPauseAt = 100u;
constexpr eng::u32 kConfirmAt = 120u;

struct GameExample {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021701u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(2048, 16);
		m_mod_block = backend.memory().chip.allocate_block<eng::MusicTag>(kModSize, 4);
		m_alarm_block = backend.memory().chip.allocate_block<eng::AudioTag>(kAlarmLen, 4);
		m_beep_block = backend.memory().chip.allocate_block<eng::AudioTag>(kBeepLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid() ||
			!m_alarm_block.valid() || !m_beep_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021702u);
			return;
		}
		build_mod(m_mod_block.view.data());
		gen_square(m_alarm_block.view.data(), kAlarmLen, 64);
		gen_square(m_beep_block.view.data(), kBeepLen, 4);
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021703u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		// Superficie estable: GameAudio sobre el AudioSystem del backend.
		// Orden canónico (=062): música primero, luego reservar AUD0, luego el mixer, luego el banco.
		m_audio.attach(backend.audio());
		m_audio.set_music_volume(40);
		m_audio.set_duck_volume(12);

		eng::audio::MusicModule mod {eng::Span<const eng::u8>(m_mod_block.view.as_const().data(), kModSize)};
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
		if (!m_music_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021704u);
			return;
		}
		if (!m_audio.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021705u);
			return;
		}
		m_audio.set_music_channel_mask(0x01u); // AUD0 al mixer, AUD1..AUD3 música

		m_audio.bank().add(kSfxBeep,
				   {eng::Span<const eng::u8>(m_beep_block.view.as_const().data(), kBeepLen), 3, 2, 4, false});
		m_audio.bank().add(kSfxAlarm,
				   {eng::Span<const eng::u8>(m_alarm_block.view.as_const().data(), kAlarmLen), 1, 1, 0, true});
		m_alarm_ch = m_audio.play(kSfxAlarm, 0);

		m_init_ok = true;
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		const eng::u32 f = context.frame.frame_index;

		// BOOT -> TITLE: (la música ya arrancó en init) modo Game.
		if (f == kTitleAt) {
			backend.audio().set_mode(eng::audio::AudioMode::Game);
		}

		// TITLE -> GAMEPLAY: SFX por input (el input solo dispara en esta fase).
		if (f >= kGameplayAt && f < kPauseAt) {
			eng::amiga::GameInput gin;
			eng::amiga::poll_input(gin);
			if (gin.port0 != 0u) {
				m_audio.play(kSfxBeep, f);
			}
		}

		// GAMEPLAY -> PAUSE: corte ordenado.
		if (f == kPauseAt) {
			m_audio.stop_music();
			backend.audio().set_mode(eng::audio::AudioMode::Silent);
		}

		m_audio.update(f);      // poda voces + ducking
		m_audio.update_music(); // no-op para Protracker (CIA)

		if (f == kConfirmAt) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			eng::debug::mark_ready(g_eng_run_status,
					       (static_cast<eng::u32>(dmaconr) << 16u) | 1u);
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void gen_square(eng::u8* dst, eng::u32 len, eng::u32 half) {
		for (eng::u32 i = 0; i < len; ++i) {
			dst[i] = ((i / half) & 1u) ? 24u : static_cast<eng::u8>(256u - 24u);
		}
	}
	/// Módulo Protracker mínimo: nota C-2 en el CANAL 1 (AUD1), dejando AUD0 al mixer.
	void build_mod(eng::u8* m) {
		for (eng::u32 i = 0; i < kModSize; ++i) m[i] = 0;
		const eng::u32 h = 20;
		m[h + 22] = 0x00; m[h + 23] = 0x80;
		m[h + 25] = 40;
		m[h + 28] = 0x00; m[h + 29] = 0x80;
		m[950] = 1; m[951] = 127; m[952] = 0;
		m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';
		for (eng::u32 row = 0; row < 64; ++row) {
			const eng::u32 base = 1084 + row * 16 + 4;
			m[base + 0] = 0x01; m[base + 1] = 0xAC; m[base + 2] = 0x10; m[base + 3] = 0x00;
		}
		for (eng::u32 i = 0; i < kSampleLen; ++i) {
			m[kSampleOffset + i] = (i < kSampleLen / 2) ? 48u : static_cast<eng::u8>(256u - 48u);
		}
	}
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x1200, kPlanes,
					  m_bitplane_block.view, kPlaneBytes);
		sched.move(eng::copper::color_register(0), 0x000u);
		sched.move(eng::copper::color_register(1), 0x002u);
		sched.end();
		m_copper_ptr = sched.data();
		return sched.ok();
	}

	bool m_init_ok = false;
	bool m_music_ok = false;
	eng::audio::SfxChannel m_alarm_ch = -1;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::MusicTag> m_mod_block {};
	eng::Block<eng::AudioTag> m_alarm_block {};
	eng::Block<eng::AudioTag> m_beep_block {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::audio::GameAudio m_audio {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	GameExample game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
