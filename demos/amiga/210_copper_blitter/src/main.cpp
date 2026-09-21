// ============================================================================
// Demo 210 — Copper lanza blits (Tecnica A): blit sincronizado al haz
// ============================================================================
//
// Un `CopperIntent` de tipo `BlitterJob` (`raster_intent.hpp`) programa el Blitter y
// escribe `BLTSIZE` en la linea 304 (borde inferior): un blit **sincronizado al haz** que
// copia `src` -> `dst` (256 words, `D = A`). La CPU limpia `dst` cada frame; el demo
// **verifica** la copia al frame siguiente y la publica en el overlay y en `RunStatus`.
//
// El Copper solo puede escribir los registros del Blitter si `COPCON` tiene `CDANG`;
// `takeover_display` lo activa. Ver `ROADMAP_BLITTER_COPPER.md` (Tecnica A).
//
//   bash ./tools/build/build-demo.sh demos/amiga/210_copper_blitter --debug
//   bash ./tools/run/run-demo.sh demos/amiga/210_copper_blitter --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

constexpr eng::u16 kWords = 256u;          // words a copiar (4 filas x 64)
constexpr eng::u16 kBlitLine = 0x130;      // 304: borde inferior (fuera del area visible)
constexpr eng::u16 kBytesPerRow = 40;      // 320 px / 8 (display de fondo)
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;

struct CopperBlitterDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 32u * 1024u, 4u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021001u);
			return;
		}
		m_src = backend.memory().chip.allocate_block<eng::SpriteTag>(kWords * 2u, 16);
		m_dst = backend.memory().chip.allocate_block<eng::SpriteTag>(kWords * 2u, 16);
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(4096u, 16);
		m_bitmap = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		if (!m_src.valid() || !m_dst.valid() || !m_copper.valid() || !m_bitmap.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021002u);
			return;
		}
		eng::Words<eng::SpriteTag> s = m_src.view.as_words();
		for (eng::u16 i = 0; i < kWords; ++i) {
			s[i] = static_cast<eng::u16>(0xa5a5u ^ (i * 0x1111u));
		}
		zero_dst();
		m_bitmap.view.fill(0u);

		const eng::u16* cl = build_copper();
		if (cl == nullptr) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021003u);
			return;
		}
		backend.takeover_display(cl);
		eng::debug::mark_ready(g_eng_run_status, 0x00021000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		// 1) Verifica la copia que dejo el blit del Copper en el frame anterior.
		m_checked = true;
		m_last_ok = copied();
		eng::debug::mark_ready(g_eng_run_status, m_last_ok ? 0x00021f00u : 0x00021000u);
		// 2) Limpia el destino (CPU) y publica la lista con el blit del Copper.
		zero_dst();
		const eng::u16* cl = build_copper();
		if (cl != nullptr) {
			backend.install_copper_list(cl);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 220, 0x00082030);
		d.rect(40, 40, 720, 220, 0x00ffffff);
		d.text(64, 60, "AMG 210 - Copper lanza blits (BlitterJob, ventana segura)", 0x00ffffff);
		const bool ok = m_checked && m_last_ok;
		d.text(64, 100, ok ? "copper blit: OK (256 words copiadas por el Copper)"
				   : (m_checked ? "copper blit: FAIL" : "copper blit: esperando..."),
		       ok ? 0x0000ff80 : (m_checked ? 0x00ff6060 : 0x00ffff00));
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void zero_dst() {
		eng::Words<eng::SpriteTag> d = m_dst.view.as_words();
		for (eng::u16 i = 0; i < kWords; ++i) {
			d[i] = 0u;
		}
	}

	[[nodiscard]] bool copied() const {
		const eng::Words<eng::SpriteTag> s = m_src.view.as_words();
		const eng::Words<eng::SpriteTag> d = m_dst.view.as_words();
		for (eng::u16 i = 0; i < kWords; ++i) {
			if (d[i] != s[i]) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] const eng::u16* build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0,
					  kBytesPerRow, 0x1200, 1u, m_bitmap.view, kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaBlitter));
		sched.emit_palette(kPalette.color);

		eng::graphics::BlitterJob job {};
		job.bltcon0 = static_cast<eng::u16>(eng::graphics::kBlitterUseA |
						    eng::graphics::kBlitterUseD |
						    eng::graphics::kBlitterMintermCopyA);
		job.bltcon1 = 0;
		job.bltamod = 0;
		job.bltdmod = 0;
		job.bltapt = m_src.view.data();
		job.bltdpt = m_dst.view.data();
		job.bltsize = static_cast<eng::u16>((4u << 6u) | (kWords / 4u));

		eng::graphics::CopperIntent intent {};
		intent.kind = eng::graphics::CopperIntentKind::BlitterJob;
		intent.top = kBlitLine;
		intent.blitter_job = &job;

		sched.set_blitter_window(eng::graphics::BlitterWindow {kBlitLine, static_cast<eng::u16>(kBlitLine + 4u)});
		sched.emit_copper_intents(&intent, 1u);
		sched.wait_line_safe(static_cast<eng::u16>(kBlitLine + 8u));
		sched.end();

		if (!sched.ok()) {
			return nullptr;
		}
		return sched.data();
	}

	static constexpr eng::Palette32 kPalette {{
		0x013, 0xfff,
	}};

	eng::Block<eng::SpriteTag> m_src {};
	eng::Block<eng::SpriteTag> m_dst {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::PlaneTag> m_bitmap {};
	bool m_checked = false;
	bool m_last_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CopperBlitterDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
