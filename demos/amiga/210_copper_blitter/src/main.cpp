// ============================================================================
// Demo 210 — borde de scroll reparado por Blitter (+ Técnica B)
// ============================================================================
//
// La pantalla (320 px, 1 plano) es un buffer anular: cada frame se desplaza una
// columna a la izquierda con un `BlitJob` (`CopyRect` 19x256, mods 2) y la
// **columna nueva** entra por la derecha con otro `BlitJob` (1x256). El buffer
// resultante se verifica contra el patrón procedural.
//
// Tambien valida la **Tecnica B** (Blitter -> copperlist): un `BlitJob` (1 word de
// ancho, `dst_mod = 2`) parchea los data words de 8 MOVEs consecutivos.
//
// Los blits sueltos se envian con `blitter_submit(BlitJob)` (un job); el mismo
// camino que `execute_frame_plan` (encadena varios).
//
// Nota: lanzar un blit **desde el Copper** (`CopperIntentKind::BlitterJob`) en la
// lista de display rompe el render de bitplanes del emulador (el scroll deja de
// verse); esa via se valida en `tests/host/260_copper_blitter` (emision + ventana
// segura) y en `docs/reference/emulators/winuae/copper.md` (`CDANG`).
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

constexpr eng::u16 kPlaneWords = 20u;            // 320 px / 16
constexpr eng::u16 kRowBytes = kPlaneWords * 2u; // 40 B/fila
constexpr eng::u16 kRows = 256u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kRowBytes) * kRows;
constexpr eng::u16 kLastWord = kPlaneWords - 1u; // 19: ultima columna (entra la nueva)
constexpr eng::u16 kBlitLine = 0x130;            // 304: borde inferior (tras el blit de CPU)

struct ScrollEdgeDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 32u * 1024u, 4u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021001u);
			return;
		}
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(4096u, 16);
		m_bitmap = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		m_src = backend.memory().chip.allocate_block<eng::SpriteTag>(512u, 16);
		m_dst = backend.memory().chip.allocate_block<eng::SpriteTag>(512u, 16);
		m_patch_cl = backend.memory().chip.allocate_block<eng::CopperTag>(64u, 16);
		m_patch_vals = backend.memory().chip.allocate_block<eng::SpriteTag>(32u, 16);
		m_col_vals = backend.memory().chip.allocate_block<eng::SpriteTag>(kRows * 2u, 16);
		if (!m_copper.valid() || !m_bitmap.valid() || !m_src.valid() || !m_dst.valid() ||
		    !m_patch_cl.valid() || !m_patch_vals.valid() || !m_col_vals.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021002u);
			return;
		}

		// Patron inicial: columna w = columna absoluta w.
		eng::u16* plane = plane_words();
		for (eng::u16 w = 0; w < kPlaneWords; ++w) {
			for (eng::u16 row = 0; row < kRows; ++row) {
				plane[static_cast<eng::u32>(row) * (kRowBytes / 2u) + w] = col_value(w, row);
			}
		}
		m_col = kLastWord;

		// `src`/`dst` para el blit del Copper; `dst` a cero.
		eng::Words<eng::SpriteTag> s = m_src.view.as_words();
		for (eng::u16 i = 0; i < 256u; ++i) {
			s[i] = static_cast<eng::u16>(0xa5a5u ^ (i * 0x1111u));
		}
		zero_dst();

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
		// Blit del Copper (frame anterior): verifica la copia y limpia el destino.
		m_last_ok = copied();
		zero_dst();
		m_scroll_ok = scroll_step(backend);
		const eng::u16* cl = build_copper();
		if (cl != nullptr) {
			backend.install_copper_list(cl);
		}
		eng::debug::mark_ready(g_eng_run_status,
				       (m_last_ok && m_patch_ok && m_scroll_ok) ? 0x00021fffu
										: 0x00021000u);
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		// Sin overlay: en modo takeover su `clear()` tapa el display del demo.
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
	/// columna nueva a la derecha. Verifica que `word(w,row) == col_value(m_col-19+w, row)`.
	bool scroll_step(eng::amiga::MinimalBackend& backend) {
		eng::u16* plane = plane_words();
		// 1) Shift: words 0..18 = words 1..19 (una columna a la izquierda).
		eng::graphics::BlitJob shift {};
		shift.kind = eng::graphics::BlitJobKind::CopyRect;
		shift.source = plane + 1;
		shift.destination = plane;
		shift.words_per_row = kLastWord;
		shift.height = kRows;
		shift.source_modulo_bytes = 2;
		shift.destination_modulo_bytes = 2;
		shift.bitplane_count = 1u;
		if (!backend.blitter_submit(shift, true)) {
			return false;
		}
		// 2) Columna nueva en el word 19 (la absoluta `m_col+1`).
		++m_col;
		eng::Words<eng::SpriteTag> col = m_col_vals.view.as_words();
		for (eng::u16 r = 0; r < kRows; ++r) {
			col[r] = col_value(m_col, r);
		}
		eng::graphics::BlitJob draw {};
		draw.kind = eng::graphics::BlitJobKind::CopyRect;
		draw.source = col.data();
		draw.destination = plane + kLastWord;
		draw.words_per_row = 1u;
		draw.height = kRows;
		draw.source_modulo_bytes = 0;
		draw.destination_modulo_bytes = static_cast<eng::s16>(kRowBytes - 2u);
		draw.bitplane_count = 1u;
		if (!backend.blitter_submit(draw, true)) {
			return false;
		}
		// 3) Verificacion: word(w) = columna (m_col - 19 + w).
		for (eng::u16 w = 0; w < kPlaneWords; ++w) {
			const eng::u16 abs = static_cast<eng::u16>(m_col - kLastWord + w);
			for (eng::u16 row = 0; row < kRows; row += 32u) {
				if (plane[static_cast<eng::u32>(row) * (kRowBytes / 2u) + w] !=
				    col_value(abs, row)) {
					return false;
				}
			}
		}
		return true;
	}

	void zero_dst() {
		eng::Words<eng::SpriteTag> d = m_dst.view.as_words();
		for (eng::u16 i = 0; i < 256u; ++i) {
			d[i] = 0u;
		}
	}
	[[nodiscard]] bool copied() const {
		const eng::Words<eng::SpriteTag> s = m_src.view.as_words();
		const eng::Words<eng::SpriteTag> d = m_dst.view.as_words();
		for (eng::u16 i = 0; i < 256u; ++i) {
			if (d[i] != s[i]) {
				return false;
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
		eng::graphics::BlitJob job {};
		job.kind = eng::graphics::BlitJobKind::CopyRect;
		job.source = vals.data();
		job.destination = &cl[1];
		job.words_per_row = 1u;
		job.height = n;
		job.source_modulo_bytes = 0;
		job.destination_modulo_bytes = 2;
		job.bitplane_count = 1u;
		if (!backend.blitter_submit(job, true)) {
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

	[[nodiscard]] const eng::u16* build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0,
					  kRowBytes, 0x1200, 1u, m_bitmap.view, kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaBlitter));
		sched.emit_palette(kPalette.color);

		// TEST: blit lanzado por el Copper (Tecnica A) en el borde superior.
		eng::graphics::BlitterJob job {};
		job.bltcon0 = static_cast<eng::u16>(eng::graphics::kBlitterUseA |
						    eng::graphics::kBlitterUseD |
						    eng::graphics::kBlitterMintermCopyA);
		job.bltapt = m_src.view.data();
		job.bltdpt = m_dst.view.data();
		job.bltsize = static_cast<eng::u16>((4u << 6u) | 64u);
		eng::graphics::CopperIntent it {};
		it.kind = eng::graphics::CopperIntentKind::BlitterJob;
		it.top = kBlitLine;
		it.blitter_job = &job;
		sched.set_blitter_window(eng::graphics::BlitterWindow {kBlitLine, static_cast<eng::u16>(kBlitLine + 4u)});
		sched.emit_copper_intents(&it, 1u);
		sched.wait_line(0xf8u);
		sched.end();
		if (!sched.ok()) {
			return nullptr;
		}
		return sched.data();
	}

	static constexpr eng::Palette32 kPalette {{
		0x013, 0xfff, 0x0f0, 0xf00, 0x00f, 0xff0, 0x0ff, 0xf0f,
	}};

	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::PlaneTag> m_bitmap {};
	eng::Block<eng::SpriteTag> m_src {};
	eng::Block<eng::SpriteTag> m_dst {};
	eng::Block<eng::CopperTag> m_patch_cl {};
	eng::Block<eng::SpriteTag> m_patch_vals {};
	eng::Block<eng::SpriteTag> m_col_vals {};
	eng::u16 m_col = 0;
	bool m_last_ok = false;
	bool m_patch_ok = false;
	bool m_scroll_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	ScrollEdgeDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
