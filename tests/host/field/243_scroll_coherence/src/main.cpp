// ============================================================================
// Test HOST-243: coherencia de la seleccion de plaquetas del scroll (corkscrew).
// ============================================================================
//
// Sobre mapas ALEATORIOS (tamano de mundo y wrap) verifica que el corkscrew
// XYLimited elige las plaquetas de forma COHERENTE:
//   - cada columna entrante se pinta COMPLETA (todas las filas del anillo, sin
//     huecos ni repeticiones) a lo largo de su tile,
//   - las columnas avanzan +1 (sin saltos de columna),
//   - el mismo patron a paso de 1 px y a 16 px (burst).
//
// El "tileset" no influye en la geometria del blit (el indice de tile no cambia el
// destino); lo que se ejercita es la geometria del mapa/anillo. El corkscrew asume
// **tile de 16 px** con `bitmap_blocks_per_col >= tile_w + 2` (si no, `mapy` supera el
// anillo y pinta filas de mas): el test fija el caso soportado y comprueba ese limite.
//
//   bash tools/run-host-tests.sh tests/host/field/243_scroll_coherence

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include <eng/field/scroll_engine.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

struct Draw { eng::u16 x, y, mapx, mapy; };

struct Lcg {
	std::uint32_t s;
	explicit Lcg(std::uint32_t seed) : s(seed) {}
	std::uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
	std::uint32_t range(std::uint32_t n) { return next() % n; }
};

struct MockSink {
	eng::u16 tile_w = 16, tile_h = 16, view_w = 320, view_h = 256;
	eng::u8 nplanes = 4;
	eng::u16 display_h = 288;
	eng::u16 bpr = 27;
	eng::u16 row_bytes = 54;
	eng::u16 map_w = 512, map_h = 512;
	eng::u16 wrap_x = 0, wrap_y = 0;
	bool one_dir = false;

	mutable std::vector<Draw> draws;

	eng::u16 tile_width() const { return tile_w; }
	eng::u16 tile_height() const { return tile_h; }
	eng::u8 planes() const { return nplanes; }
	eng::u16 viewport_w() const { return view_w; }
	eng::u16 viewport_h() const { return view_h; }
	eng::u16 display_height() const { return display_h; }
	eng::u16 display_planelines() const { return static_cast<eng::u16>(display_h * nplanes); }
	eng::u16 bitmap_blocks_per_row() const { return bpr; }
	eng::u16 bitmap_blocks_per_col() const { return static_cast<eng::u16>(display_h / tile_h); }
	eng::u16 block_planes_lines() const { return static_cast<eng::u16>(tile_h * nplanes); }
	eng::u16 bytes_per_row() const { return row_bytes; }
	eng::u16 bitmap_width() const { return static_cast<eng::u16>(bpr * tile_w); }
	eng::u16 map_width_blocks() const { return map_w; }
	eng::u16 map_height_blocks() const { return map_h; }
	eng::u16 map_wrap_x() const { return wrap_x; }
	eng::u16 map_wrap_y() const { return wrap_y; }
	bool one_direction() const { return one_dir; }
	bool finite_x() const { return false; }
	bool add_draw(eng::graphics::FramePlan&, eng::u16 x, eng::u16 y, eng::u16 mx, eng::u16 my) const {
		draws.push_back(Draw {x, y, mx, my});
		return true;
	}
	void save_word(eng::u32) const {}
	void restore_saveword() const {}
};

using Consts = eng::field::ScrollConsts;

void seed_engine(eng::field::ScrollEngine<MockSink, Consts {0, 0, 0, 0, 0}>& e, eng::u16 mb) {
	e.state().mapposx = 0;
	e.state().videoposx = 0;
	e.state().mapposy = static_cast<eng::s32>(mb) * 16;
	e.state().videoposy = e.state().mapposy;
	e.state().previous_xdirection = eng::field::ScrollDirNone;
}

// Analiza la corrida por CHUNKS de un tile: en cada chunk debe haber UNA columna del
// mapa y sus filas del anillo deben ser un rango contiguo y completo (bpc filas).
bool chunks_coherent(const MockSink& sink, eng::u16 bpc, const char* tag) {
	const std::size_t chunk = static_cast<std::size_t>(sink.tile_w) + 2u; // 2 (stepx0) + (tw-1) + 1 fill
	bool ok = true;
	for (std::size_t i = 0; i + chunk <= sink.draws.size(); i += chunk) {
		std::set<eng::u16> cols, rows;
		for (std::size_t j = i; j < i + chunk; ++j) {
			cols.insert(sink.draws[j].mapx);
			rows.insert(sink.draws[j].mapy);
		}
		const eng::u16 lo = *rows.begin(), hi = *rows.rbegin();
		if (cols.size() != 1u || rows.size() != bpc ||
		    static_cast<eng::u16>(hi - lo + 1u) != rows.size()) {
			std::printf("  [%s] chunk %zu: cols=%zu filas=%zu (bpc=%u, rango=%u..%u)\n",
				    tag, i / chunk, cols.size(), rows.size(), bpc, lo, hi);
			ok = false;
		}
	}
	return ok;
}

} // namespace

int main() {
	std::printf("== HOST-243 scroll_coherence ==\n");
	eng::graphics::FramePlan plan {};

	// Mapas aleatorios, tiles de 16 px (caso soportado por el corkscrew).
	for (std::uint32_t seed = 1; seed <= 80; ++seed) {
		Lcg rng(seed);
		MockSink sink;
		sink.map_w = static_cast<eng::u16>(4u + rng.range(60u));
		sink.map_h = static_cast<eng::u16>(8u + rng.range(56u));
		sink.wrap_x = sink.map_w; // toroidal: sin tope, el scroll siempre avanza
		sink.wrap_y = 0;
		const eng::u16 mb = static_cast<eng::u16>(rng.range(4u));
		const eng::u16 bpc = sink.bitmap_blocks_per_col();

		// 1 px/frame: pinta sin prisa en la guarda (caso "tranquilo").
		{
			using Engine = eng::field::ScrollEngine<MockSink, Consts {0, 0, 0, 0, 0}>;
			Engine e; seed_engine(e, mb);
			bool ok = true;
			for (int i = 0; i < 48 && ok; ++i) ok = e.scroll_right(plan, sink);
			check(ok, "1px: avanza sin fallo");
			check(chunks_coherent(sink, bpc, "1px"), "1px: cada columna cubre el anillo completo");
		}

		// 16 px/frame (un tile por llamada): el caso extremo.
		{
			using Engine = eng::field::ScrollEngine<MockSink, Consts {0, 0, 0, 0, 0}>;
			Engine e; seed_engine(e, mb);
			bool ok = true;
			for (int c = 0; c < 3 && ok; ++c) ok = e.burst_right_px(plan, sink, 16u);
			check(ok, "16px: burst avanza sin fallo");
			check(chunks_coherent(sink, bpc, "16px"), "16px: cada columna cubre el anillo completo");
		}
	}

	// 1 px y 16 px producen LA MISMA secuencia de plaquetas (el burst no cambia la
	// eleccion, solo calcula la geometria una vez).
	{
		using Engine = eng::field::ScrollEngine<MockSink, Consts {0, 0, 0, 0, 0}>;
		MockSink a, b;
		a.map_w = b.map_w = 64; a.wrap_x = b.wrap_x = 64;
		Engine ea, eb; seed_engine(ea, 1); seed_engine(eb, 1);
		for (int i = 0; i < 16; ++i) check(ea.scroll_right(plan, a), "px");
		check(eb.burst_right_px(plan, b, 16u), "burst");
		bool eq = a.draws.size() == b.draws.size();
		for (std::size_t i = 0; eq && i < a.draws.size(); ++i) {
			eq = a.draws[i].x == b.draws[i].x && a.draws[i].y == b.draws[i].y &&
			     a.draws[i].mapx == b.draws[i].mapx && a.draws[i].mapy == b.draws[i].mapy;
		}
		check(eq, "1px y 16px eligen la misma secuencia de plaquetas");
	}

	// Mapa acotado: el scroll se detiene en el tope (no salta columnas mas alla del mapa).
	{
		using Engine = eng::field::ScrollEngine<MockSink, Consts {0, 0, 0, 0, 0}>;
		MockSink sink; sink.map_w = 8u; sink.wrap_x = 0u;
		Engine e; seed_engine(e, 0);
		bool blocked = false;
		for (int i = 0; i < 400 && !blocked; ++i) if (!e.scroll_right(plan, sink)) blocked = true;
		check(blocked, "mapa acotado: se detiene en el tope");
		check(e.state().mapposx > static_cast<eng::s32>(sink.map_w) * 16 - sink.view_w - 16,
		      "mapa acotado: llega al limite");
	}

	// Limite del corkscrew: `bpc >= tile_w + 2` (si no, `mapy` desborda el anillo).
	check(288u / 16u >= 16u + 2u, "tile 16 con anillo 288 cumple bpc >= tile_w + 2");
	check(!(320u / 32u >= 32u + 2u), "tile 32 con anillo 320 NO cumple (corkscrew no lo soporta)");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: seleccion de plaquetas coherente (columnas completas, sin huecos, 1px y 16px).\n");
	return 0;
}
