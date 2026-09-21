// ============================================================================
// Demo 210 — Copper lanza blits + borde de scroll reparado por Blitter
// ============================================================================
//
// Tres cosas, todas con el Blitter:
//   1) **Tecnica A**: un `CopperIntentKind::BlitterJob` programa el Blitter y escribe
//      `BLTSIZE` en el borde inferior (blit sincronizado al haz); copia `src`->`dst`.
//   2) **Tecnica B**: `blitter_patch_copper_data` escribe los data words de una copperlist.
//   3) **Borde de scroll**: la pantalla (buffer anular de 21 words/fila, 320 px visibles) se
//      desplaza una columna a la izquierda con `blitter_blit_strided` y la **columna nueva**
//      entra por la derecha con `blitter_memcpy_strided`; se verifica el buffer resultante.
//
// El Copper solo puede tocar el Blitter si `COPCON` tiene `CDANG`; `takeover_display` lo
// activa (`docs/reference/emulators/winuae/copper.md`).
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

constexpr eng::u16 kWords = 256u;             // copia Copper (Tecnica A): 4 filas x 64
constexpr eng::u16 kBlitLine = 0x130;         // 304: borde inferior (fuera del area visible)
constexpr eng::u16 kDispWords = 20u;          // 320 px visibles
constexpr eng::u16 kBufWords = 21u;           // + 1 columna (16 px) para el borde de scroll
constexpr eng::u16 kRowBytes = kBufWords * 2u; // 42 B/fila
constexpr eng::u16 kRows = 256u;
constexpr eng::u32 kBufBytes = static_cast<eng::u32>(kRowBytes) * kRows;
constexpr eng::u16 kDispColWord = kDispWords; // word de la columna nueva (20)

struct CopperBlitterDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 48u * 1024u, 4u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021001u);
			return;
		}
		m_src = backend.memory().chip.allocate_block<eng::SpriteTag>(kWords * 2u, 16);
		m_dst = backend.memory().chip.allocate_block<eng::SpriteTag>(kWords * 2u, 16);
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(4096u, 16);
		m_bitmap = backend.memory().chip.allocate_block<eng::PlaneTag>(kBufBytes, 16);
		m_patch_cl = backend.memory().chip.allocate_block<eng::CopperTag>(64u, 16);
		m_patch_vals = backend.memory().chip.allocate_block<eng::SpriteTag>(32u, 16);
		m_col_vals = backend.memory().chip.allocate_block<eng::SpriteTag>(kRows * 2u, 16);
		if (!m_src.valid() || !m_dst.valid() || !m_copper.valid() || !m_bitmap.valid() ||
		    !m_patch_cl.valid() || !m_patch_vals.valid() || !m_col_vals.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021002u);
			return;
		}
		eng::Words<eng::SpriteTag> s = m_src.view.as_words();
		for (eng::u16 i = 0; i < kWords; ++i) {
			s[i] = static_cast<eng::u16>(0xa5a5u ^ (i * 0x1111u));
		}
		zero_dst();

		// Buffer inicial: word w = columna absoluta w (m_col = 20 = word de la ultima col).
		eng::u16* plane = plane_words();
		for (eng::u16 w = 0; w < kBufWords; ++w) {
			for (eng::u16 row = 0; row < kRows; ++row) {
				plane[static_cast<eng::u32>(row) * (kRowBytes / 2u) + w] = col_value(w, row);
			}
		}
		m_col = static_cast<eng::u16>(kBufWords - 1u);

		m_patch_ok = patch_selfcheck(backend);

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
		// 2) Avanza el borde de scroll (shift + columna nueva) y verifica el buffer.
		m_scroll_ok = scroll_step(backend);
		// 3) Limpia el destino y publica la lista (display + blit del Copper).
		zero_dst();
		const eng::u16* cl = build_copper();
		if (cl != nullptr) {
			backend.install_copper_list(cl);
		}
		eng::debug::mark_ready(g_eng_run_status,
				       (m_last_ok && m_patch_ok && m_scroll_ok) ? 0x00021fffu
										: 0x00021000u);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 760, 200, 0x00082030);
		d.rect(40, 40, 760, 200, 0x00ffffff);
		d.text(56, 56, "AMG 210 - Copper blits + scroll edge (Blitter)", 0x00ffffff);
		const bool blit_ok = m_checked && m_last_ok;
		d.text(56, 92, blit_ok ? "copper blit (Tecnica A): OK" : "copper blit: FAIL/esperando",
		       blit_ok ? 0x0000ff80 : 0x00ffff00);
		d.text(56, 116, m_patch_ok ? "copperlist patch (Tecnica B): OK"
					   : "copperlist patch: FAIL",
		       m_patch_ok ? 0x0000ff80 : 0x00ff6060);
		d.text(56, 140, m_scroll_ok ? "scroll edge (shift + columna): OK"
					    : "scroll edge: FAIL",
		       m_scroll_ok ? 0x0000ff80 : 0x00ff6060);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	[[nodiscard]] eng::u16* plane_words() const {
		return reinterpret_cast<eng::u16*>(m_bitmap.view.data());
	}

	/// Patron de la columna absoluta `abs_col`: banda diagonal (word de 16 px).
	[[nodiscard]] static eng::u16 col_value(eng::u16 abs_col, eng::u16 row) {
		return (((row + abs_col * 4u) & 63u) < 16u) ? 0xffffu : 0x0000u;
	}

	/// **Borde de scroll**: desplaza la pantalla una columna a la izquierda y escribe la
	/// columna nueva a la derecha. Verifica que `word(w,row) == col_value(m_col-20+w, row)`.
	bool scroll_step(eng::amiga::MinimalBackend& backend) {
		eng::u16* plane = plane_words();
		// 1) Shift: words 0..19 = words 1..20 (una columna a la izquierda).
		if (!backend.blitter_blit_strided(plane, 2, plane + 1, 2, kDispWords, kRows, true)) {
			return false;
		}
		// 2) Columna nueva en el word 20 (la absoluta `m_col+1`).
		++m_col;
		eng::Words<eng::SpriteTag> col = m_col_vals.view.as_words();
		for (eng::u16 r = 0; r < kRows; ++r) {
			col[r] = col_value(m_col, r);
		}
		if (!backend.blitter_memcpy_strided(plane + kDispColWord, kRowBytes - 2, col.data(), 0,
						    kRows, true)) {
			return false;
		}
		// 3) Verificacion: el buffer queda coherente con el scroll.
		for (eng::u16 w = 0; w < kBufWords; ++w) {
			const eng::u16 abs = static_cast<eng::u16>(m_col - kDispWords + w);
			for (eng::u16 row = 0; row < kRows; row += 64u) {
				if (plane[static_cast<eng::u32>(row) * (kRowBytes / 2u) + w] !=
				    col_value(abs, row)) {
					return false;
				}
			}
		}
		return true;
	}

	/// **Tecnica B** (Blitter -> copperlist): parchea con el Blitter los data words de 8
	/// MOVEs consecutivos y verifica que solo cambian los datos (los registros, no).
	bool patch_selfcheck(eng::amiga::MinimalBackend& backend) {
		constexpr eng::u16 n = 8;
		eng::Words<eng::CopperTag> cl = m_patch_cl.view.as_words();
		eng::Words<eng::SpriteTag> vals = m_patch_vals.view.as_words();
		for (eng::u16 i = 0; i < n; ++i) {
			cl[i * 2u + 0u] = static_cast<eng::u16>(
				static_cast<eng::u16>(eng::copper::Register::COLOR00) + i * 2u); // registro
			cl[i * 2u + 1u] = 0u;                                                     // dato (a parchear)
			vals[i] = static_cast<eng::u16>(0x1111u * static_cast<eng::u16>(i + 1u));
		}
		if (!backend.blitter_patch_copper_data(&cl[1], vals.data(), n, true)) {
			return false;
		}
		for (eng::u16 i = 0; i < n; ++i) {
			if (cl[i * 2u + 0u] != static_cast<eng::u16>(
						 static_cast<eng::u16>(eng::copper::Register::COLOR00) + i * 2u)) {
				return false; // el registro no debe tocarse
			}
			if (cl[i * 2u + 1u] != vals[i]) {
				return false; // el dato debe quedar parcheado
			}
		}
		return true;
	}

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
					  kRowBytes, 0x1200, 1u, m_bitmap.view, kBufBytes);
		// La fila del buffer mide 42 B; el DDF fetchoa 40 B -> BPL1MOD = 2.
		sched.move(eng::copper::Register::BPL1MOD, static_cast<eng::u16>(kRowBytes - 40u));
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaBlitter));
		sched.emit_palette(kPalette.color);

		// Tecnica A: el Copper lanza un blit `src` -> `dst` en el borde inferior.
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
	eng::Block<eng::CopperTag> m_patch_cl {};
	eng::Block<eng::SpriteTag> m_patch_vals {};
	eng::Block<eng::SpriteTag> m_col_vals {};
	eng::u16 m_col = 0;
	bool m_checked = false;
	bool m_last_ok = false;
	bool m_patch_ok = false;
	bool m_scroll_ok = false;
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
