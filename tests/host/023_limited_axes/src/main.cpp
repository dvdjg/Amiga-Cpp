// ============================================================================
// Test HOST-023: variantes de eje de XLimited/XYLimited (Ring / Finite / Off)
// ============================================================================
//
// Fija la semántica de la FAMILIA de scroll sobre el `ScrollEngine` real:
//   - Eje X `Ring` (XLimited): anillo con banda entrante; escribe con add_draw.
//   - Eje X `Finite`: rango acotado [0, mundo-viewport], SIN anillo ni guardas;
//     el puntero se mueve directamente y NO escribe (el contenido ya está).
//   - OneDirection: no restaura saveword.
//
// Escenario objetivo (shooter vertical): mundo 400 px de ancho × 10000 px de
// alto, viewport 320×256, tile 16. X `Finite` (recorrido 0..80), Y anillo
// (corkscrew) one-direction (solo la fila entrante).
//
//   bash tools/run-host-tests.sh tests/host/023_limited_axes

#include <cstdio>

#include <eng/field/scroll_engine.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

// Sink de pega que cumple `ScrollSink` y registra las llamadas de dibujo.
struct MockSink {
	// Geometría del escenario.
	eng::u16 tile_w = 16, tile_h = 16, view_w = 320, view_h = 256;
	eng::u8 nplanes = 4;
	eng::u16 display_h = 288;                       // 256 + 2*16 (anillo corkscrew)
	eng::u16 bits_per_row = 432 / 8;                // bitmap = mundo(400) + margen
	eng::u16 bpr = 432 / 16;                        // bloques por fila (27)
	eng::u16 bpc = 288 / 16;                        // bloques por columna (18)
	eng::u16 map_w = 25, map_h = 625;               // 400 × 10000 px en tiles
	eng::u16 wrap_x = 0, wrap_y = 1;                // X finito (sin wrap), Y toroidal
	bool finite = true;
	bool one_dir = true;

	// Registro.
	mutable int draws = 0;
	mutable int restores = 0;
	mutable eng::u32 last_y = 0xffffffffu;

	// --- contrato ScrollSink ---
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
	bool finite_x() const { return finite; }
	bool add_draw(eng::graphics::FramePlan&, eng::u16, eng::u16 y, eng::u16, eng::u16) const {
		++draws;
		last_y = y;
		return true;
	}
	void save_word(eng::u32) const {}
	void restore_saveword() const { ++restores; }
};

using Consts = eng::field::ScrollConsts;
using Engine = eng::field::ScrollEngine<MockSink, Consts{16, 16, 288, 1152, 4}>;

} // namespace

int main() {
	eng::graphics::FramePlan plan {};

	// 1) Eje X FINITE: mueve el puntero, NO escribe, clampa en [0, mundo-view].
	{
		MockSink sn;
		Engine eng;
		int ok = 0;
		for (int i = 0; i < 200; ++i) if (eng.scroll_right(plan, sn)) ++ok;
		check(ok == 80, "X finite: recorrido exacto 0..80");
		check(sn.draws == 0, "X finite: no escribe bandas (contenido precargado)");
		int back = 0;
		for (int i = 0; i < 200; ++i) if (eng.scroll_left(plan, sn)) ++back;
		check(back == 80, "X finite: vuelve a 0");
	}

	// 2) Eje X RING: escribe la banda entrante (comportamiento XLimited).
	{
		MockSink sn;
		sn.finite = false;
		sn.bpr = 256;                 // anillo ancho
		sn.map_w = 256;
		Engine eng;
		check(eng.scroll_right(plan, sn), "X ring: avanza");
		check(sn.draws > 0, "X ring: escribe banda entrante");
	}

	// 3) OneDirection: no restaura saveword al invertir.
	{
		MockSink sn;
		sn.finite = false; sn.bpr = 256; sn.map_w = 256; sn.one_dir = true;
		Engine eng;
		eng.scroll_right(plan, sn);
		eng.scroll_left(plan, sn);      // invertir NO debe restaurar
		check(sn.restores == 0, "one-direction: sin restore_saveword");
	}

	// 4) Y corkscrew con X finite: la fila entrante ocupa TODO el ancho y cae en
	//    el anillo (< display_planelines). One-direction: sin saveword.
	{
		MockSink sn;
		sn.finite = true; sn.one_dir = true;
		Engine eng;
		eng.state().mapposy = 5000;   // cámara a media altura (shooter)
		const int before = sn.draws;
		check(eng.scroll_up(plan, sn), "Y finite-X: avanza");
		check(sn.draws - before == sn.bpr, "Y finite-X: dibuja la fila entera (27 bloques)");
		check(sn.last_y < sn.display_planelines(), "Y finite-X: fila dentro del anillo");
		check(sn.restores == 0, "Y finite-X one-direction: sin restore_saveword");
	}

	// 5) Y anillo con X finite: el bucle vertical envuelve (videoposy) sin salir
	//    del anillo en muchos pasos (10000 px de alto, well dentro del tope).
	{
		MockSink sn;
		sn.finite = true; sn.wrap_y = 0;   // mapa acotado pero enorme
		Engine eng;
		eng.state().mapposy = 5000;
		int ok = 0;
		for (int i = 0; i < 2000; ++i) if (eng.scroll_up(plan, sn)) ++ok;
		check(ok == 2000, "Y anillo: avanza 2000 px");
		check(sn.last_y < sn.display_planelines(), "Y anillo: fila entrante siempre dentro del anillo");
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: ejes XLimited Ring/Finite + OneDirection (shooter vertical) validados.\n");
	return 0;
}
