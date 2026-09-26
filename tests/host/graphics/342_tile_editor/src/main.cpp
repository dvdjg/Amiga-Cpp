// ============================================================================
// Test HOST-342: editor de tiles de juego (tilemap::TileEditor) - F7.2.
// ============================================================================
//
// Respalda `eng/graphics/tilemap/tile_editor.hpp`: vista mutable de alto nivel sobre una rejilla
// de `PackedTileCell` — `set_tile`/`tile`/`mark_all_dirty`/`dirty_rect`/`flush`. Da al juego el
// `set_tile`+`flush` de una capa de tiles sin tocar framebuffers.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/342_tile_editor

#include <cstdio>

#include <eng/graphics/tilemap/tile_editor.hpp>

namespace tm = eng::graphics::tilemap;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-342 tile_editor ==\n");

	tm::PackedTileCell cells[4u * 3u] {};
	tm::TileEditor ed {eng::Span<tm::PackedTileCell> {cells, 4u * 3u}, 4u, 3u};
	check(ed.valid(), "editor valido");

	check(!ed.dirty_rect(0u).valid(), "sin dirty al inicio");

	check(ed.set_tile(1u, 1u, 17u), "set_tile ok");
	check(ed.tile(1u, 1u) == 17u, "tile consultado");
	const tm::TileRect r1 = ed.dirty_rect(0u);
	check(r1.left == 1u && r1.top == 1u && r1.width == 1u && r1.height == 1u, "dirty 1x1");

	check(ed.set_tile(3u, 2u, 5u), "set_tile 2 ok");
	const tm::TileRect r2 = ed.dirty_rect(0u);
	check(r2.left == 1u && r2.top == 1u && r2.width == 3u && r2.height == 2u,
	      "dirty envolvente 3x2");

	// flush de un buffer no afecta al otro.
	ed.flush(0u);
	check(!ed.dirty_rect(0u).valid(), "buffer 0 limpio");
	check(ed.dirty_rect(1u).valid(), "buffer 1 sigue sucio");

	// fuera de rango.
	check(!ed.set_tile(9u, 9u, 1u), "coordenada fuera -> false");

	// marcar todo.
	tm::TileEditor ed2 {eng::Span<tm::PackedTileCell> {cells, 4u * 3u}, 4u, 3u};
	ed2.mark_all_dirty();
	const tm::TileRect all = ed2.dirty_rect(0u);
	check(all.left == 0u && all.top == 0u && all.width == 4u && all.height == 3u,
	      "mark_all_dirty cubre toda la rejilla");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: TileEditor (set_tile/dirty_rect/flush) validado.\n");
	return 0;
}
