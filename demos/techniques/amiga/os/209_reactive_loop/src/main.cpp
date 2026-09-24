// ============================================================================
// Demo 209 — bucle reactivo del mini-SO sobre `eng::App`
// ============================================================================
//
// El `App` publica el **VBlank** en su puerto de mensajes mediante el hook del `Engine`
// (sin abrir un segundo servicio de VBlank) y un **blit asincrono** publica `BlitDone` al
// terminar la IRQ BLIT. El juego drena `app.port()` en `update` y reacciona: verifica la
// copia al recibir `BlitDone` y vigila los contadores `vblank_count`/`blitdone_count`.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/os/209_reactive_loop --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/os/209_reactive_loop --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/api/game.hpp>
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
constexpr eng::u16 kBlitPeriod = 30u; // frames entre blits asincronos

struct ReactiveDemo {
	// --- init(App&): reserva buffers por la API del App ---------------------
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_src = app.memory().chip.template allocate_block<eng::PlaneTag>(kBytes, 16);
		m_dst = app.memory().chip.template allocate_block<eng::PlaneTag>(kBytes, 16);
		if (!m_src.valid() || !m_dst.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020901u);
			return;
		}
		for (eng::u32 i = 0; i < kBytes; ++i) {
			m_src.view[i] = static_cast<eng::u8>((i * 7u + 3u) & 0xffu);
			m_dst.view[i] = 0u;
		}
		eng::debug::mark_ready(g_eng_run_status, 0x00020900u);
	}

	// --- update(App&): consume el puerto y reacciona ------------------------
	void update(auto& app) {
		// Drena el puerto: VBlank (latido de frame) y BlitDone (fin de copia).
		eng::os::Msg m;
		while (app.port().pop(m)) {
			if (m.type == eng::os::MsgType::VBlank) {
				++m_vblank_seen;
			} else if (m.type == eng::os::MsgType::BlitDone) {
				++m_blit_seen;
				m_pending = false;
				m_blit_ok = equal(eng::Span<const eng::u8> {m_dst.view.data(), kBytes},
						  eng::Span<const eng::u8> {m_src.view.data(), kBytes});
			}
		}

		// Lanza un blit asincrono cada `kBlitPeriod` frames (el primero tras el arranque).
		const eng::u32 frame = app.frame();
		if (frame >= 2u && ((frame - 2u) % kBlitPeriod) == 0u && !m_pending) {
			m_pending = app.blitter_memcpy_async(
				eng::Span<eng::u8> {m_dst.view.data(), kBytes},
				eng::Span<const eng::u8> {m_src.view.data(), kBytes});
		}
	}

	// --- render(App&): pinta el estado del bucle reactivo -------------------
	void render(auto& app) {
		auto& d = app.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 300, 0x00082030);
		d.rect(40, 40, 720, 300, 0x00ffffff);
		d.text(64, 60, "AMG 209 - reactive loop (App port: VBlank + BlitDone)", 0x00ffffff);

		constexpr eng::u32 green = 0x0000ff80;
		constexpr eng::u32 yellow = 0x00ffff00;
		constexpr eng::u32 red = 0x00ff6060;

		const bool vblank_ok = m_vblank_seen > 0u && m_vblank_seen == app.vblank_count();
		const bool blit_seen = m_blit_seen > 0u;
		const bool blit_ok = blit_seen && m_blit_seen == app.blitdone_count() && m_blit_ok;

		d.text(64, 104, vblank_ok ? "VBlank: OK (Engine hook -> App port)"
					  : "VBlank: esperando...",
		       vblank_ok ? green : yellow);
		d.text(64, 134, blit_ok ? "BlitDone: OK (async IRQ BLIT -> port)"
					: (blit_seen ? "BlitDone: FAIL (copia)" : "BlitDone: esperando..."),
		       blit_ok ? green : (blit_seen ? red : yellow));
		d.text(64, 176, vblank_ok && blit_ok ? "reactive loop: OK" : "reactive loop: ...",
		       vblank_ok && blit_ok ? green : yellow);

		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
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

	eng::Block<eng::PlaneTag> m_src {};
	eng::Block<eng::PlaneTag> m_dst {};
	eng::u32 m_vblank_seen = 0;
	eng::u32 m_blit_seen = 0;
	bool m_blit_ok = false;
	bool m_pending = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({ 64u * 1024u, 4u * 1024u, 4u * 1024u })) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020902u);
		return 0;
	}
	ReactiveDemo game {};
	eng::App app {backend, game};
	app.run(0xffffu);

	return 0;
}
