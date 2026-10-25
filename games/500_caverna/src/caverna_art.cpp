#include "caverna_art.hpp"

namespace caverna_art {
namespace {

eng::Span<const eng::u16> g_cave_rows {};
eng::Span<const eng::u16> g_terrain_rows {};
eng::Span<const eng::u16> g_cells {};
eng::Span<const eng::u16> g_bg_cells {};

} // namespace

bool bind(eng::Span<const eng::u16> cave_rows, eng::Span<const eng::u16> terrain_rows,
	  eng::Span<const eng::u16> cells, eng::Span<const eng::u16> bg_cells) {
	if (cave_rows.size() != kTileRowsWords || terrain_rows.size() != kTileRowsWords ||
	    cells.size() != static_cast<eng::usize>(kMapWidth) * kMapHeight ||
	    bg_cells.size() != static_cast<eng::usize>(kMapWidth) * kMapHeight) {
		return false;
	}
	g_cave_rows = cave_rows;
	g_terrain_rows = terrain_rows;
	g_cells = cells;
	g_bg_cells = bg_cells;
	return true;
}

eng::u16 cave_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	if (plane >= kPlanes) return 0u;
	const eng::usize index =
		((static_cast<eng::usize>(glyph & 15u) * 4u + (variant & 3u)) * kPlanes +
		 plane) * kTile + (row & 15u);
	return g_cave_rows[index];
}

eng::u16 terrain_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	if (plane >= kPlanes) return 0u;
	const eng::usize index =
		((static_cast<eng::usize>(glyph & 15u) * 4u + (variant & 3u)) * kPlanes +
		 plane) * kTile + (row & 15u);
	return g_terrain_rows[index];
}

eng::Span<const eng::u16> map_cells() { return g_cells; }

eng::Span<const eng::u16> background_cells() { return g_bg_cells; }

} // namespace caverna_art
