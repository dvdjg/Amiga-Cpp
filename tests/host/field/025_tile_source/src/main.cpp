// ============================================================================
// Test HOST-025: accesor de tiles `TileSource` (denso y disperso) + skip de empty
// ============================================================================
//
// Valida el concepto `eng::field::TileSource` y las dos implementaciones:
//   - `TileLayerMap` (mapa DENSO con wrap toroidal/acotado).
//   - `SparseTileMap<Chunk>` (mapa DISPERSO por chunks; compatible con los "mapas
//     infinitos" de Tiled).
// Y la semántica que usa el scroll: los tiles `empty_tile` NO se pintan.

#include <cstdio>

#include <eng/field/tile_map.hpp>
#include <eng/field/tile_source.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

using eng::s32;
using eng::u16;

} // namespace

int main() {
	// El concepto lo cumplen ambas implementaciones.
	static_assert(eng::field::TileSource<eng::field::TileLayerMap>);
	static_assert(eng::field::TileSource<eng::field::SparseTileMap<16>>);

	// Denso 4x4: tile = indice+1, la celda 5 vacía.
	{
		u16 cells[16];
		for (u16 i = 0; i < 16u; ++i) cells[i] = (i == 5u) ? 0xFFFFu : static_cast<u16>(i + 1u);
		eng::field::TileLayerMap m {};
		m.cells = eng::Span<const u16>(cells, 16);
		m.width = 4; m.height = 4; m.empty_tile = 0xFFFFu;
		check(m.tile_at(0, 0) == 1u, "denso (0,0)");
		check(m.tile_at(1, 1) == 0xFFFFu, "denso (1,1) vacio");
		check(m.is_empty(m.tile_at(1, 1)), "denso is_empty");
		check(m.tile_at(-1, 0) == 0u, "denso fuera -> edge_tile");
	}

	// Disperso: chunk 4x4 en (0,0) y (2,-1).
	{
		u16 c0[16], c1[16];
		for (u16 i = 0; i < 16u; ++i) {
			c0[i] = static_cast<u16>(100u + i);
			c1[i] = static_cast<u16>(200u + i);
		}
		using Sparse = eng::field::SparseTileMap<4>;
		Sparse::Chunk chunks[2] {
			{ 0, 0, eng::Span<const u16>(c0, 16) },
			{ 2, -1, eng::Span<const u16>(c1, 16) },
		};
		Sparse sm {};
		sm.chunks = eng::Span<const Sparse::Chunk>(chunks, 2);
		sm.empty_tile = 0xFFFFu;
		check(sm.tile_at(0, 0) == 100u, "sparse (0,0)");
		check(sm.tile_at(3, 3) == 115u, "sparse (3,3)");
		check(sm.tile_at(4, 0) == 0xFFFFu, "sparse chunk ausente -> empty");
		check(sm.tile_at(8, -4) == 200u, "sparse chunk (2,-1)");
		check(sm.tile_at(-1, -1) == 0xFFFFu, "sparse negativo ausente");
	}

	// Skip de empty: el scroll pinta sólo los tiles poblados.
	{
		u16 cells[64];
		int expected = 0;
		for (int i = 0; i < 64; ++i) {
			const bool empty = (i % 5 == 0);
			cells[i] = empty ? 0xFFFFu : 1u;
			if (!empty) ++expected;
		}
		eng::field::TileLayerMap m {};
		m.cells = eng::Span<const u16>(cells, 64);
		m.width = 8; m.height = 8; m.empty_tile = 0xFFFFu;
		int drawn = 0;
		for (s32 y = 0; y < 8; ++y) {
			for (s32 x = 0; x < 8; ++x) {
				if (!m.is_empty(m.tile_at(x, y))) ++drawn;
			}
		}
		check(drawn == expected, "skip empty: pinta solo poblados");
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: TileSource (denso + disperso) y skip de empty validados.\n");
	return 0;
}
