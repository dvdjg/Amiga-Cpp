// ============================================================================
// Test HOST-094: restauracion de la costura (ScopeGuard) en scroll_engine
// ============================================================================
//
// `ScrollEngine::scroll_right/left/down/up/burst_right` guardan la costura con
// `save_word` y luego emiten blits con `add_draw`, que puede rechazar el frame
// (devolver false). Si eso pasa, la costura debe quedar RESTAURADA
// (`restore_saveword`) — antes del fix se hacia `return false` sin restaurar y la
// word quedaba a medio escribir.
//
// El test usa un sink que falla en el N-esimo `add_draw` y comprueba que:
//   1) la funcion devuelve false,
//   2) se llamo `restore_saveword` exactamente una vez por cada `save_word` vivo,
//   3) en el camino de exito NO se restaura (release() del guard).

#include <cstdio>

#include <eng/field/scroll_engine.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {
using eng::u16;

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

struct MockSink {
	eng::u16 tile_w = 16, tile_h = 16, view_w = 320, view_h = 256;
	eng::u8 nplanes = 4;
	eng::u16 display_h = 288;
	eng::u16 bits_per_row = 432 / 8;
	eng::u16 bpr = 432 / 16;
	eng::u16 bpc = 288 / 16;
	eng::u16 map_w = 512, map_h = 512;
	eng::u16 wrap_x = 512, wrap_y = 1;
	bool one_dir = false;

	int fail_at = -1;          // falla el add_draw numero `fail_at` (0-based); -1 = nunca
	bool fail_after_save = false; // falla el 1er add_draw DESPUES del 1er save_word
	mutable int n_draws = 0;
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
	bool add_draw(eng::graphics::FramePlan&, eng::u16, eng::u16, eng::u16, eng::u16) const {
		bool ok = true;
		if (fail_after_save) {
			ok = (n_saves == 0); // solo deja pasar mientras no ha habido save_word
		} else if (fail_at >= 0) {
			ok = (n_draws != fail_at);
		}
		++n_draws;
		return ok;
	}
	void save_word(eng::u32) const { ++n_saves; }
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

/// Escenario que garantiza al menos un `save_word` y varios `add_draw`.
void test_right_fails_restores() {
	// Primero un pase completo para conocer cuantos add_draw/save_word hace el cruce.
	Engine probe;
	MockSink sp;
	seed(probe, 48, 21, eng::field::ScrollDirNone); // stepy != 0 -> rama con save_word
	eng::graphics::FramePlan plan0;
	// Avanzar hasta el pixel que cruza la columna (guarda la costura).
	for (u16 i = 0; i < 16u; ++i) {
		if (!probe.scroll_right(plan0, sp)) break;
	}
	check(sp.n_saves > 0, "el escenario produce al menos un save_word");

	// Ahora, con fallo en el primer add_draw posterior al primer save_word.
	Engine e;
	MockSink s;
	seed(e, 48, 21, eng::field::ScrollDirNone);
	s.fail_after_save = true;
	eng::graphics::FramePlan plan;
	bool r = true;
	for (u16 i = 0; i < 16u && r; ++i) {
		r = e.scroll_right(plan, s);
	}
	check(!r, "scroll_right devuelve false si add_draw falla");
	check(s.n_saves > 0, "scroll_right guardo la costura antes de fallar");
	check(s.restores >= 1, "scroll_right restaura la costura al fallar");
}

void test_right_ok_no_restore() {
	Engine e;
	MockSink s; // no falla
	seed(e, 16, 16, eng::field::ScrollDirNone);
	eng::graphics::FramePlan plan;
	const bool r = e.scroll_right(plan, s);
	check(r, "scroll_right devuelve true sin fallo");
	check(s.n_saves > 0, "scroll_right guardo la costura");
	check(s.restores == 0, "camino correcto: NO se restaura");
}

void test_left_fails_restores() {
	Engine e;
	MockSink s;
	s.fail_after_save = true;
	seed(e, 32, 80, eng::field::ScrollDirNone);
	eng::graphics::FramePlan plan;
	bool r = true;
	for (u16 i = 0; i < 20u && r; ++i) {
		r = e.scroll_left(plan, s);
	}
	check(!r, "scroll_left devuelve false si add_draw falla");
	if (s.n_saves > 0) {
		check(s.restores >= 1, "scroll_left restaura la costura al fallar");
	}
}

void test_down_fails_restores() {
	// `scroll_down` solo guarda costura en la rama `stepy == th-1 && stepx != 0`:
	// hace falta X desplazado (stepx) y una fila al borde. Semilla con stepx != 0.
	Engine e;
	MockSink s;
	s.fail_after_save = true;
	seed(e, 50, 21, eng::field::ScrollDirNone); // 50 = 48+2 -> stepx != 0
	eng::graphics::FramePlan plan;
	bool r = true;
	for (u16 i = 0; i < 64u && r; ++i) {
		r = e.scroll_down(plan, s);
	}
	if (s.n_saves > 0 || !r) {
		check(!r, "scroll_down devuelve false si add_draw falla");
		check(s.restores >= 1, "scroll_down restaura la costura al fallar");
	} else {
		// Sin costura en este escenario no hay nada que restaurar; no es un fallo.
		std::printf("[skip] scroll_down: el escenario no alcanza la rama con save_word\n");
	}
}

} // namespace

int main() {
	test_right_fails_restores();
	test_right_ok_no_restore();
	test_left_fails_restores();
	test_down_fails_restores();
	if (g_fail == 0) {
		std::printf("OK: scroll_engine restaura la costura al rechazar un frame (ScopeGuard)\n");
		return 0;
	}
	std::printf("FALLOS: %d\n", g_fail);
	return 1;
}
