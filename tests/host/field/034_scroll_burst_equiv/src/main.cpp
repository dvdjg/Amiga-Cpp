// ============================================================================
// Test HOST-034: equivalencia del avance en ráfaga (burst_right) con 1 px
// ============================================================================
//
// `ScrollEngine::burst_right(tiles)` debe emitir EXACTAMENTE los mismos blits,
// `save_word` y ajustes de estado que `tiles*tile_width` pasos de `scroll_right`,
// pero calculando la geometría del cruce una sola vez. Se compara con un sink de
// registro sobre varios escenarios (alineado, con stepy != 0, varias tiles,
// inversión de dirección previa).

#include <cstdio>

#include <eng/field/scroll_engine.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

struct Draw {
	eng::u16 x, y, mapx, mapy;
};

// Sink de pega del anillo XLimited que registra los blits y la costura.
struct MockSink {
	eng::u16 tile_w = 16, tile_h = 16, view_w = 320, view_h = 256;
	eng::u8 nplanes = 4;
	eng::u16 display_h = 288;
	eng::u16 bits_per_row = 432 / 8;
	eng::u16 bpr = 432 / 16;                    // 27
	eng::u16 bpc = 288 / 16;                    // 18
	eng::u16 map_w = 512, map_h = 512;          // anillo ancho (sin tope)
	eng::u16 wrap_x = 512, wrap_y = 1;
	bool one_dir = false;

	mutable Draw draws[256] {};
	mutable int n_draws = 0;
	mutable eng::u32 saves[256] {};
	mutable int n_saves = 0;
	mutable int restores = 0;

	eng::u16 tile_width() const { return tile_w; }
	eng::u16 tile_height() const { return tile_h; }
	eng::u8 planes() const { return nplanes; }
	eng::u16 viewport_w() const { return view_w; }
	eng::u16 viewport_h() const { return view_h; }
	eng::u16 display_height() const { return display_h; }
	eng::u16 display_planelines() const { return static_cast<eng::u16>(display_h * nplanes); }
	eng::u16 bitmap_blocks_per_row() const { return bpr; }
	eng::u16 bitmap_blocks_per_col() const { return bpc; }
	eng::u16 block_planes_lines() const { return static_cast<eng::u16>(tile_h * nplanes); }
	eng::u16 bytes_per_row() const { return bits_per_row; }
	eng::u16 bitmap_width() const { return static_cast<eng::u16>(bpr * tile_w); }
	eng::u16 map_width_blocks() const { return map_w; }
	eng::u16 map_height_blocks() const { return map_h; }
	eng::u16 map_wrap_x() const { return wrap_x; }
	eng::u16 map_wrap_y() const { return wrap_y; }
	bool one_direction() const { return one_dir; }
	bool finite_x() const { return false; }
	bool add_draw(eng::graphics::FramePlan&, eng::u16 x, eng::u16 y, eng::u16 mx, eng::u16 my) const {
		if (n_draws < 256) draws[n_draws] = Draw {x, y, mx, my};
		++n_draws;
		return true;
	}
	void save_word(eng::u32 o) const {
		if (n_saves < 256) saves[n_saves] = o;
		++n_saves;
	}
	void restore_saveword() const { ++restores; }
};

using Consts = eng::field::ScrollConsts;
using Engine = eng::field::ScrollEngine<MockSink, Consts{16, 16, 288, 1152, 4}>;

void seed(Engine& e, eng::s32 mx, eng::s32 my, eng::u8 prev) {
	e.state().mapposx = mx;
	e.state().videoposx = mx;
	e.state().mapposy = my;
	e.state().videoposy = my;
	e.state().previous_xdirection = prev;
}

void compare(const char* label, Engine& a, MockSink& sa, Engine& b, MockSink& sb) {
	bool eq = sa.n_draws == sb.n_draws && sa.n_saves == sb.n_saves && sa.restores == sb.restores;
	for (int i = 0; eq && i < sa.n_draws; ++i) {
		eq = sa.draws[i].x == sb.draws[i].x && sa.draws[i].y == sb.draws[i].y &&
		     sa.draws[i].mapx == sb.draws[i].mapx && sa.draws[i].mapy == sb.draws[i].mapy;
	}
	for (int i = 0; eq && i < sa.n_saves; ++i) eq = sa.saves[i] == sb.saves[i];
	eq = eq && a.state().mapposx == b.state().mapposx && a.state().videoposx == b.state().videoposx &&
	     a.state().previous_xdirection == b.state().previous_xdirection;
	check(eq, label);
	if (!eq) {
		std::printf("  draws %d/%d saves %d/%d restores %d/%d state (%d,%d,%d)/(%d,%d,%d)\n",
			sa.n_draws, sb.n_draws, sa.n_saves, sb.n_saves, sa.restores, sb.restores,
			a.state().mapposx, a.state().videoposx, a.state().previous_xdirection,
			b.state().mapposx, b.state().videoposx, b.state().previous_xdirection);
	}
}
} // namespace

int main() {
	eng::graphics::FramePlan plan {};

	// Caso 1: inicio alineado (stepy=0) y 1 tile.
	{
		Engine ea, eb; MockSink sa, sb;
		seed(ea, 32, 80, eng::field::ScrollDirNone);
		seed(eb, 32, 80, eng::field::ScrollDirNone);
		for (int i = 0; i < 16; ++i) check(ea.scroll_right(plan, sa), "px avanza");
		check(eb.burst_right(plan, sb, 1), "burst avanza");
		compare("burst(1) == 16 px (alineado)", ea, sa, eb, sb);
	}
	// Caso 2: stepy != 0 (fila del mapa desplazada -> rama de fillup con stepy).
	{
		Engine ea, eb; MockSink sa, sb;
		seed(ea, 48, 21, eng::field::ScrollDirNone);
		seed(eb, 48, 21, eng::field::ScrollDirNone);
		for (int i = 0; i < 16; ++i) check(ea.scroll_right(plan, sa), "px avanza");
		check(eb.burst_right(plan, sb, 1), "burst avanza");
		compare("burst(1) == 16 px (stepy!=0)", ea, sa, eb, sb);
	}
	// Caso 3: 2 tiles de golpe.
	{
		Engine ea, eb; MockSink sa, sb;
		seed(ea, 0, 96, eng::field::ScrollDirNone);
		seed(eb, 0, 96, eng::field::ScrollDirNone);
		for (int i = 0; i < 32; ++i) check(ea.scroll_right(plan, sa), "px avanza");
		check(eb.burst_right(plan, sb, 2), "burst avanza");
		compare("burst(2) == 32 px", ea, sa, eb, sb);
	}
	// Caso 4: venía de la izquierda (restore_saveword una vez).
	{
		Engine ea, eb; MockSink sa, sb;
		seed(ea, 128, 48, eng::field::ScrollDirLeft);
		seed(eb, 128, 48, eng::field::ScrollDirLeft);
		for (int i = 0; i < 16; ++i) check(ea.scroll_right(plan, sa), "px avanza");
		check(eb.burst_right(plan, sb, 1), "burst avanza");
		compare("burst(1) == 16 px (restore previo)", ea, sa, eb, sb);
		check(sa.restores == 1 && sb.restores == 1, "restore exacto (1)");
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: burst_right equivalente a los sub-pasos de 1 px.\n");
	return 0;
}
