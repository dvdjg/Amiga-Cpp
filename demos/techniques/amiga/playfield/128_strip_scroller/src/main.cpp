// Demo 128: scroller por tiras (Copper ring + incoming strip) — camino rápido a 50 fps.
//
// Usa el camino de `eng/field/strip_scroller.hpp` + `strip_composer.hpp`: cada frame planifica la
// tira entrante (`plan_strip_frame`), la compone desde el banco de tiles (`compose_column`, tiles
// SEPARADOS) solo al cruzar frontera de tile, la ejecuta por Blitter (`blitter_strip_column`) y
// parchea la copperlist (`strip_copper_values` + `StripComposer::patch`).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/128_strip_scroller --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/128_strip_scroller

#include <eng/api/api.hpp>
#include <eng/field/strip_composer.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

namespace {

// Anillo de 2 pantallas (640 px) + guarda/fetch; viewport 208 px (split/cabe en VPOS si hiciera).
using Geom = eng::field::StripScrollGeometry<320u, 256u, 5u, 16u, 16u, 2u, 1u, false, 43u>;

constexpr eng::u16 kTilesetTiles = 16u;
constexpr eng::u16 kTileWords = Geom::tile_h * Geom::planes;          // 80
constexpr eng::u16 kColumnWords = Geom::column_planelines;            // 1040
constexpr eng::u32 kRingBytes = static_cast<eng::u32>(Geom::ring_w_bytes) * Geom::planes *
				Geom::viewport_h;                          // 86*5*208
constexpr eng::u32 kBankBytes = static_cast<eng::u32>(kTilesetTiles) * kTileWords * 2u;
constexpr eng::u32 kColumnBytes = static_cast<eng::u32>(kColumnWords) * 2u;
constexpr eng::u16 kMapCols = 32u;                                     // mapa que se repite
constexpr eng::s32 kStepX = 2;                                         // px/frame

	struct StripGame {
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_ring {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bank {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_column {};
	eng::field::StripComposer<Geom> m_composer {};
	eng::amiga::AmigaBackend* m_backend = nullptr;
	eng::u16* m_ring_words = nullptr;
	eng::u16* m_bank_words = nullptr;
	eng::u16* m_column_words = nullptr;
	eng::s32 m_scroll = 0;
	bool m_ready = false;

	[[nodiscard]] eng::u16 map_tile(eng::u16 col, eng::u16 row) const {
		// **Objetos singulares**: bloques de 2x2 (32x32) cada 6 columnas, cada uno de un color
		// DISTINTO, sobre un fondo tenue. Asi su desplazamiento es inequivoco (vision + ojo).
		if ((col % 6u) < 2u && (row % 8u) >= 2u && (row % 8u) < 4u) {
			return static_cast<eng::u16>(1u + ((col / 6u) % 14u));
		}
		return 0u; // fondo
	}

	void fill_bank() {
		// tile 0 = fondo (color 1, tenue); tiles 1..15 = objetos (colores 2..16, brillantes).
		for (eng::u16 t = 0u; t < kTilesetTiles; ++t) {
			const eng::u16 color = (t == 0u) ? 1u : static_cast<eng::u16>(t + 1u);
			for (eng::u16 line = 0u; line < Geom::tile_h; ++line) {
				for (eng::u16 plane = 0u; plane < Geom::planes; ++plane) {
					m_bank_words[t * kTileWords + line * Geom::planes + plane] =
						static_cast<eng::u16>(((color >> plane) & 1u) ? 0xffffu : 0x0000u);
				}
			}
		}
	}

	/// Compone la columna `map_col` (tiles del mapa) y la blitea en el word `ring_word` del anillo.
	void paint_column(eng::u16 ring_word, eng::u16 map_col) {
		eng::u16 ids[Geom::column_tiles];
		for (eng::u16 r = 0u; r < Geom::column_tiles; ++r) {
			ids[r] = map_tile(map_col, r);
		}
		(void)eng::field::compose_column<Geom>(m_column_words, m_bank_words, ids, kTileWords);
		(void)m_backend->blitter_strip_column(m_column_words, m_ring_words + ring_word,
						      Geom::strip_words, Geom::bltdmod_col,
						      Geom::column_planelines, 0u);
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_backend = &backend;
		if (!backend.configure_memory({256u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012801u);
			return;
		}
		auto& mm = backend.memory_manager();
		m_ring = mm.chip().reserve<eng::PlaneTag>(kRingBytes, 16u);
		m_bank = mm.chip().reserve<eng::PlaneTag>(kBankBytes, 16u);
		m_column = mm.chip().reserve<eng::PlaneTag>(kColumnBytes, 16u);
		if (!m_ring.valid() || !m_bank.valid() || !m_column.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012802u);
			return;
		}
		m_ring_words = reinterpret_cast<eng::u16*>(m_ring.data());
		m_bank_words = reinterpret_cast<eng::u16*>(m_bank.data());
		m_column_words = reinterpret_cast<eng::u16*>(m_column.data());
		fill_bank();

		eng::Palette32 pal {};
		pal.color[0] = 0x000u; // negro (borde)
		pal.color[1] = 0x113u; // fondo azul oscuro
		pal.color[2] = 0xf00u;
		pal.color[3] = 0x0f0u;
		pal.color[4] = 0x00fu;
		pal.color[5] = 0xff0u;
		pal.color[6] = 0xf0fu;
		pal.color[7] = 0x0ffu;
		pal.color[8] = 0xfffu;
		pal.color[9] = 0xf80u;
		pal.color[10] = 0x8f0u;
		pal.color[11] = 0x0f8u;
		pal.color[12] = 0x08fu;
		pal.color[13] = 0x80fu;
		pal.color[14] = 0xf08u;
		pal.color[15] = 0x880u;
		pal.color[16] = 0xaaau;
		if (!m_composer.init(mm, pal.words(), 1536u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012803u);
			return;
		}
		m_composer.set_ring(m_ring_words);
		if (!m_composer.build()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012804u);
			return;
		}
		m_composer.takeover(backend);
		// Relleno inicial: las columnas de la ventana visible.
		for (eng::u16 w = 0u; w < Geom::visible_words; ++w) paint_column(w, w);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x12800000u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) return;
		const eng::s32 prev = m_scroll;
		m_scroll += kStepX;
		const auto fr = eng::field::plan_strip_frame<Geom>(m_scroll, 0, prev, 0);
		if (fr.column_crossed) {
			const eng::u16 map_col = static_cast<eng::u16>(((m_scroll / 16) + Geom::visible_words) % kMapCols);
			paint_column(fr.col_dest_word, map_col);
		}
		const auto sc = eng::field::strip_copper_values<Geom>(fr);
		(void)m_composer.patch(sc);
		m_composer.install(backend);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

StripGame g_game {};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	eng::Engine engine {backend, g_game};
	engine.run_frames_polling(0xffff);
	return 0;
}
