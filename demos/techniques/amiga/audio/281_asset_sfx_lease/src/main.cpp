// Lanzar:
//   node tools/fs/make-volume.mjs --no-adf
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/281_asset_sfx_lease --debug --clean
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/281_asset_sfx_lease --keep-running

// Demo 281: una voz SFX retiene su AssetCache owner hasta completar la muestra por CPU.
#include <eng/api/api.hpp>
#include <eng/audio/audio_system.hpp>
#include <eng/os/file.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga/backend.hpp>

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

constexpr eng::u32 kSampleBytes = 16u * 1024u;
constexpr eng::u32 kChipBytes = 128u * 1024u;
constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;

struct AssetSfxLeaseDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({kChipBytes, 8u * 1024u, 4u * 1024u})) {
			fail(0x00028101u);
			return;
		}
		m_planes = backend.memory_manager().chip().reserve<eng::PlaneTag>(kPlaneBytes, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(1024u, 16u);
		if (!m_planes.valid() || !m_copper.valid() || !build_copper()) {
			fail(0x00028102u);
			return;
		}
		m_sample_id = backend.assets().load("data/audio/lease_sfx.raw", kSampleBytes,
						   eng::res::MemoryRequest::Chip, 220u);
		if (m_sample_id == 0u) {
			fail(0x00028103u);
			return;
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_failed || m_ready) return;
		(void)eng::os::file_pump();
		eng::os::Msg message {};
		while (eng::os::system_port().pop(message)) {
			(void)backend.assets().on_msg(message);
		}
		if (!m_started && backend.assets().state(m_sample_id) == eng::res::AssetState::Ready) {
			backend.takeover_display(m_copper_words);
			if (!backend.audio().init(backend.memory_manager())) {
				fail(0x00028104u);
				return;
			}
			const eng::audio::SfxChannel channel = backend.audio().play_sfx_asset(
				eng::audio::MixCh0, backend.assets().lease(m_sample_id), 2,
				eng::audio::LoopMode::Once, 0u);
			if (channel < 0) {
				fail(0x00028106u);
				return;
			}
			m_voice_started = backend.audio().sfx_playing(channel);
			if (!m_voice_started) {
				fail(0x00028107u);
				return;
			}
			m_started = true;
			m_start_frame = context.frame.frame_index;
		}

		if (m_started) backend.audio().tick_frame(m_events);
		if (m_started && !m_voice_finished) {
			m_voice_finished = !backend.audio().sfx_playing(eng::audio::MixCh0);
			if (!m_voice_finished) {
				// El runtime debe rechazar teardown mientras la voz conserva su lease CPU.
				if (backend.assets().shutdown() ||
				    backend.assets().state(m_sample_id) != eng::res::AssetState::Ready) {
					fail(0x00028108u);
					return;
				}
			}
		}

		if (m_voice_finished) {
			if (!backend.assets().shutdown() || backend.assets().used_chip() != 0u ||
			    context.frame.frame_index - m_start_frame < 20u) {
				fail(0x00028109u);
				return;
			}
			m_ready = true;
			eng::debug::mark_ready(g_eng_run_status,
					       ((context.frame.frame_index - m_start_frame) << 16u) | 2u);
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		eng::copper::SchedulerT<false> scheduler {m_copper};
		scheduler.emit_planes_display(0x2c81u, 0x2cc1u, 0x0038u, 0x00d0u, kBytesPerRow,
				      0x6200u, 1u, m_planes.mem_view(), kPlaneBytes);
		scheduler.move(eng::copper::Register::COLOR00, 0x001u);
		scheduler.move(eng::copper::Register::COLOR01, 0x0ffu);
		scheduler.wait_line(0xf8u);
		scheduler.move(eng::copper::Register::COLOR00, 0x001u);
		scheduler.end();
		m_copper_words = scheduler.data();
		return scheduler.ok();
	}

	void fail(eng::u32 code) {
		m_failed = true;
		eng::debug::mark_failed(g_eng_run_status, code);
	}

	eng::os::MsgPort<4u> m_events {};
	eng::u16 m_sample_id = 0u;
	eng::u32 m_start_frame = 0u;
	const eng::u16* m_copper_words = nullptr;
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::CopperTag, eng::MemoryKind::Chip> m_copper {};
	bool m_voice_started = false;
	bool m_started = false;
	bool m_voice_finished = false;
	bool m_ready = false;
	bool m_failed = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	AssetSfxLeaseDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
