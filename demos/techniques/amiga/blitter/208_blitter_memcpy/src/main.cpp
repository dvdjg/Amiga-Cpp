// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --keep-running

// ============================================================================
// Demo 208 — self-test de copia lineal por Blitter (blitter_memcpy)
// ============================================================================
//
// Valida en hardware `AmigaBackend::blitter_memcpy`:
//   1) **Síncrona**: copia un buffer conocido y compara byte a byte.
//   2) **Asíncrona**: `blitter_memcpy_async` + IRQ BLIT que publica `BlitDone` en un
//      `eng::os::MsgPort`; el bucle consume el mensaje y compara.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/208_blitter_memcpy --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/os/port.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

constexpr eng::u32 kBytes = 2048u;

eng::os::MsgPort<8> g_port {};

struct PostDone {
	eng::os::MsgPort<8>* port;
};
void post_done(PostDone& s, eng::u16) {
	eng::os::Msg msg {};
	msg.type = eng::os::MsgType::BlitDone;
	s.port->post(msg);
}

struct DemoGame {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 64u * 1024u, 4u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020801u);
			return;
		}
		m_src = backend.memory().chip.allocate_block<eng::PlaneTag>(kBytes, 16);
		m_dst = backend.memory().chip.allocate_block<eng::PlaneTag>(kBytes, 16);
		m_dst2 = backend.memory().chip.allocate_block<eng::PlaneTag>(kBytes, 16);
		if (!m_src.valid() || !m_dst.valid() || !m_dst2.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020802u);
			return;
		}
		for (eng::u32 i = 0; i < kBytes; ++i) {
			m_src.view[i] = static_cast<eng::u8>((i * 7u + 3u) & 0xffu);
		}

		// 1) Síncrona.
		const eng::Span<eng::u8> d1 {m_dst.view.data(), m_dst.view.size()};
		const eng::Span<const eng::u8> s {m_src.view.data(), m_src.view.size()};
		m_sync_ok = backend.blitter_memcpy(d1, s, true) &&
			    equal(eng::Span<const eng::u8> {d1.data(), d1.size()}, s);

		// 2) Asíncrona + notificación por IRQ BLIT al puerto.
		const eng::Span<eng::u8> d2 {m_dst2.view.data(), m_dst2.view.size()};
		m_async_started = backend.blitter_memcpy_async(d2, s, post_done, m_svc);

		eng::debug::mark_ready(g_eng_run_status, 0x00020800u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!m_async_started || m_async_done) {
			return;
		}
		eng::os::Msg m;
		while (g_port.pop(m)) {
			if (m.type == eng::os::MsgType::BlitDone) {
				m_async_done = true;
			}
		}
		if (m_async_done) {
			const eng::Span<const eng::u8> d2 {m_dst2.view.data(), m_dst2.view.size()};
			const eng::Span<const eng::u8> s {m_src.view.data(), m_src.view.size()};
			m_async_ok = equal(d2, s);
			backend.clear_blit_service();
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 300, 0x00082030);
		d.rect(40, 40, 720, 300, 0x00ffffff);
		d.text(64, 60, "AMG 208 - blitter_memcpy self-test", 0x00ffffff);
		d.text(64, 96, m_sync_ok ? "sync: OK" : "sync: FAIL",
		       m_sync_ok ? 0x0000ff80 : 0x00ff6060);
		const bool async_pass = m_async_started && m_async_done && m_async_ok;
		d.text(64, 126, async_pass ? "async (IRQ BLIT -> MsgPort): OK"
					   : (m_async_done ? "async: FAIL" : "async: esperando IRQ..."),
		       async_pass ? 0x0000ff80 : 0x00ffff00);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static bool equal(eng::Span<const eng::u8> a, eng::Span<const eng::u8> b) {
		if (a.size() != b.size()) {
			return false;
		}
		for (eng::usize i = 0; i < a.size(); ++i) {
			if (a[i] != b[i]) {
				return false;
			}
		}
		return true;
	}

	bool m_sync_ok = false;
	bool m_async_started = false;
	bool m_async_done = false;
	bool m_async_ok = false;
	PostDone m_svc {&g_port};
	eng::Block<eng::PlaneTag> m_src {};
	eng::Block<eng::PlaneTag> m_dst {};
	eng::Block<eng::PlaneTag> m_dst2 {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
