// ============================================================================
// Demo 059: "music player" — SFX (Audio Mixer) + música (P61) conviviendo.
// ============================================================================
//
// Demuestra la integración del `MusicPlayer` (reproductor P61 de Photon/Scoopex,
// `support/music/p61.asm`) junto al SFX mixer. La capa de juego usa
// `eng::audio::SfxMixer` y `eng::audio::P61Player`, ambas sin punteros crudos
// (la memoria se expresa como `Span<const u8>`).
//
// Qué suena: una "alarma" en bucle por el SFX mixer (canal AUD0). El reproductor
// P61 queda enlazado y listo; para reproducir música real hay que incrustar un
// módulo `.p61` (ver README). La demo valida que ambos objetos enlazan y que
// P61Player falla limpiamente sin módulo.
//
// Evidencia: `mark_ready` guarda DMACONR (bits altos) + nº de canales del mixer
// (bits bajos). `P61_Init` sin módulo devuelve error, que se refleja en el bit 8.

#include <eng/audio/music_player.hpp>
#include <eng/audio/sfx_mixer.hpp>
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

constexpr eng::u32 kAlarmLen = 1024; // múltiplo de 4

struct MusicPlayerDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005901u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_alarm_block = backend.memory().chip.allocate(kAlarmLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_alarm_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005902u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		gen_square(static_cast<eng::u8*>(m_alarm_block.data), kAlarmLen, 64);

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005903u);
			return;
		}

		backend.takeover_display(m_copper_ptr);

		if (!m_sfx.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005904u);
			return;
		}
		m_sfx.play_on(eng::audio::MixCh0, alarm_sample(), 1, eng::audio::LoopMode::Loop);

		// P61 sin módulo: falla limpiamente (bit 8 del detail a 0).
		eng::audio::MusicModule empty {};
		m_music_ok = m_music.play(empty);

		const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
		const eng::u32 flags = (m_music_ok ? 0x100u : 0u) | m_sfx.total_channels();
		eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(dmaconr) << 16u) | flags);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// La música (si hubiera módulo) avanza aquí, una vez por frame.
		m_music.update();
		(void)context;
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::audio::SfxSample alarm_sample() {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_alarm_block.data), kAlarmLen) };
	}

	void gen_square(eng::u8* dst, eng::u32 len, eng::u32 half) {
		for (eng::u32 i = 0; i < len; ++i) {
			dst[i] = ((i / half) & 1u) ? 24u : static_cast<eng::u8>(256u - 24u);
		}
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
	bool m_music_ok = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_alarm_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::SfxMixer m_sfx {};
	eng::audio::P61Player m_music {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	MusicPlayerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
