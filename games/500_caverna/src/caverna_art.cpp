#include "caverna_art.hpp"

namespace caverna_art {
namespace {

constexpr eng::u16 kFloorRow = 10u;

/// Paleta del display. PF1 = cueva de fondo (0..7); PF2 = terreno (8..15);
/// sprites = 16..31 (cada canal usa su grupo de cuatro).
constexpr eng::u16 kPalette[32] {
	0x000, 0x122, 0x233, 0x345, 0x468, 0x58b, 0x9cf, 0xfff,
	0x000, 0x141, 0x2b2, 0x421, 0x632, 0x853, 0x0dd, 0xfff,
	0x000, 0xf80, 0xf33, 0xfff, 0xf40, 0xf66, 0xfff, 0xaaa,
	0x421, 0x742, 0x085, 0x0aa, 0x4dd, 0xf6c, 0xf3a, 0x221,
};

eng::u16 g_cave_rows[16][4][kPlanes][kTile] {};
eng::u16 g_terrain_rows[16][4][kPlanes][kTile] {};
eng::u16 g_cells[kMapWidth * kMapHeight] {};
eng::u16 g_bg_cells[kMapWidth * kMapHeight] {};
eng::u16 g_actor_art[4][2][32] {};

/// Píxel de la cueva de fondo: muro de piedra con bloques, grietas, columnas,
/// cristales y estalactitas. El color 0 es la grieta oscura; 6/7 son brillos.
eng::u8 cave_pixel(eng::u16 tile, eng::u8 x, eng::u8 y) {
	const eng::u8 bx = static_cast<eng::u8>((x + (((y >> 2u) & 1u) * 8u)) & 15u);
	eng::u8 wall = (((y & 3u) == 0u) || (bx == 0u)) ? 2u : 1u;
	if (((x * 5u + y * 3u) & 15u) == 0u) wall = 3u;
	const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
	if (glyph == 0u) return wall;
	if (glyph == 1u) {
		return ((x + y * 2u) % 13u == 0u) ? 0u : wall;
	}
	if (glyph == 2u) {
		if (x >= 5u && x <= 10u) {
			if (y < 2u || y > 13u) return 7u;
			return ((x & 1u) == 0u) ? 5u : 3u;
		}
		return wall;
	}
	if (glyph == 3u) {
		if (y >= 5u && y <= 12u) {
			const eng::u8 half = static_cast<eng::u8>(3u - ((y - 5u) >> 1u));
			if (x >= static_cast<eng::u8>(7u - half) &&
			    x <= static_cast<eng::u8>(8u + half)) {
				if (y <= 6u && (x == 7u || x == 8u)) return 7u;
				return ((x + y) & 1u) == 0u ? 5u : 6u;
			}
		}
		return wall;
	}
	if (glyph == 4u) {
		const eng::u8 half = static_cast<eng::u8>(7u - (y >> 1u));
		if (y < 10u && x >= static_cast<eng::u8>(7u - half) &&
		    x <= static_cast<eng::u8>(8u + half)) {
			return (y < 2u) ? 2u : (y < 8u ? 3u : 5u);
		}
		return wall;
	}
	if (glyph == 5u) {
		if (y >= 6u && y <= 8u) return (y == 6u) ? 6u : 4u;
		return wall;
	}
	return wall;
}

/// Píxel del terreno de PF2: hierba, tierra, roca, plataformas y un hongo de luz
/// decorativo. El color 0 es transparente y deja ver la cueva de PF1.
eng::u8 terrain_pixel(eng::u16 tile, eng::u8 x, eng::u8 y) {
	const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
	if (glyph == 0u) return 0u;
	if (glyph == 1u) {
		if (y >= 9u && y <= 14u && (x == 7u || x == 8u)) return 2u;
		if (y >= 5u && y <= 8u && x >= 5u && x <= 10u) {
			return (y == 5u || x == 8u) ? 7u : 6u;
		}
		return 0u;
	}
	if (glyph == 2u || glyph == 3u) {
		if (y <= 1u) return ((x + glyph) & 3u) == 0u ? 2u : 1u;
		if (y == 2u) return 1u;
		eng::u8 dirt = ((x + y + glyph) & 7u) == 0u ? 4u : 3u;
		if (((x * 3u + y) & 15u) == 0u) dirt = 5u;
		return dirt;
	}
	if (glyph >= 4u && glyph <= 7u) {
		eng::u8 dirt = ((x * 3u + y + glyph) & 7u) == 0u ? 5u : 3u;
		if ((y & 3u) == 0u) dirt = 4u;
		const eng::u8 boulder = static_cast<eng::u8>((x + glyph * 3u) & 7u);
		if (y >= 4u && y <= 7u && boulder >= 2u && boulder <= 4u) dirt = 5u;
		if (y >= 9u && y <= 11u && boulder == 6u) dirt = 4u;
		return dirt;
	}
	if (glyph >= 8u && glyph <= 11u) {
		eng::u8 rock = ((x + y * 3u + glyph) & 7u) == 0u ? 5u : 4u;
		if ((x == 7u || x == 8u) && (y == 6u || y == 7u)) rock = 6u;
		if ((x == 7u || x == 8u) && (y == 8u || y == 9u)) rock = 2u;
		if (((x * 5u + glyph) & 15u) == 0u) rock = 3u;
		return rock;
	}
	if (glyph == 12u || glyph == 13u) {
		if (y <= 1u) return (x & 3u) == 0u ? 2u : 1u;
		if (y <= 4u) return 5u;
		return ((x + y + glyph) & 7u) == 0u ? 4u : 3u;
	}
	return 0u;
}

/// Relieve jugable: suelo con colinas suaves (filas 8..10), hongos decorativos y
/// muro de cueva detrás. Las colinas usan el mismo tilemap y son sólidas.
void build_map() {
	for (eng::u16 y = 0u; y < kMapHeight; ++y) {
		for (eng::u16 x = 0u; x < kMapWidth; ++x) {
			const eng::u16 seg = static_cast<eng::u16>((x / 16u) & 7u);
			eng::u16 top = kFloorRow;
			if (seg == 2u || seg == 6u) top = static_cast<eng::u16>(kFloorRow - 1u);
			else if (seg == 3u || seg == 7u) top = static_cast<eng::u16>(kFloorRow - 2u);
			eng::u16 tile = 0u;
			if (y >= top) {
				if (y == top) {
					tile = static_cast<eng::u16>((seg == 3u || seg == 7u ? 12u : 2u) +
								     ((x / 4u) & 1u));
				} else if (y < 13u) {
					tile = static_cast<eng::u16>(4u + ((x / 3u + y) & 3u));
				} else {
					tile = static_cast<eng::u16>(8u + ((x / 2u + y * 3u) & 3u));
				}
			}
			g_cells[static_cast<eng::u32>(y) * kMapWidth + x] = tile;

			eng::u16 bg = 0u;
			const eng::u16 col = static_cast<eng::u16>(x % 32u);
			if (y == 0u && (x & 7u) == 3u) {
				bg = 4u;
			} else if (col < 2u && y >= 1u && y <= 11u) {
				bg = 2u;
			} else if (col >= 2u && col <= 3u && y >= 6u && y <= 10u) {
				bg = 3u;
			} else if (((x * 7u + y * 11u) % 13u) == 0u) {
				bg = 1u;
			}
			g_bg_cells[static_cast<eng::u32>(y) * kMapWidth + x] = bg;
		}
	}
}

/// Precalcula las filas invariantes de ambos playfields. El callback del driver
/// solo indexa la tabla; convertir píxel a planos se hace una vez en setup.
void build_tile_rows() {
	for (eng::u8 glyph = 0u; glyph < 16u; ++glyph) {
		for (eng::u8 variant = 0u; variant < 4u; ++variant) {
			for (eng::u8 row = 0u; row < kTile; ++row) {
				for (eng::u8 plane = 0u; plane < kPlanes; ++plane) {
					eng::u16 cave = 0u;
					eng::u16 terrain = 0u;
					for (eng::u8 x = 0u; x < kTile; ++x) {
						const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
						if ((cave_pixel(glyph, x, row) & (1u << plane)) != 0u) {
							cave = static_cast<eng::u16>(cave | bit);
						}
						if ((terrain_pixel(glyph, x, row) & (1u << plane)) != 0u) {
							terrain = static_cast<eng::u16>(terrain | bit);
						}
					}
					g_cave_rows[glyph][variant][plane][row] = cave;
					g_terrain_rows[glyph][variant][plane][row] = terrain;
				}
			}
		}
	}
}

/// Píxel de un actor de 16x16 y 2 planos. Tipo 0 = jugador; 1/2/3 = murciélago,
/// limo y cangrejo. `phase` alterna la pose de arte; la posición se mueve aparte
/// en cada frame.
eng::u8 actor_pixel(eng::u8 type, eng::u8 phase, eng::u8 x, eng::u8 y) {
	if (type == 0u) {
		if (y >= 1u && y <= 6u && x >= 4u && x <= 11u) {
			if (x == 4u || x == 11u || y == 1u || y == 6u) return 2u;
			if (y >= 3u && y <= 4u && x >= 6u && x <= 9u) return 3u;
			return 1u;
		}
		if (y >= 7u && y <= 12u && x >= 4u && x <= 11u) {
			return (x == 4u || x == 11u) ? 2u : 1u;
		}
		if (y >= 8u && y <= 10u && (x == 2u || x == 3u || x == 12u || x == 13u)) return 1u;
		if (y >= 13u && y <= 15u && ((x >= 4u && x <= 6u) || (x >= 9u && x <= 11u))) {
			return (y == 15u || x == 4u || x == 11u) ? 2u : 1u;
		}
		return 0u;
	}
	if (type == 1u) {
		if (y >= 4u && y <= 9u && x >= 6u && x <= 9u) {
			if (y == 5u && (x == 6u || x == 9u)) return 3u;
			return (x == 6u || x == 9u || y == 9u) ? 2u : 1u;
		}
		const eng::u8 wy = static_cast<eng::u8>(y + (phase & 1u));
		if (wy >= 3u && wy <= 8u) {
			const eng::u8 edge = static_cast<eng::u8>((wy <= 5u) ? (wy - 2u) : (9u - wy));
			const eng::u8 reach = static_cast<eng::u8>(2u + edge * 2u);
			if ((x <= 5u && x >= static_cast<eng::u8>(6u - reach)) ||
			    (x >= 10u && x <= static_cast<eng::u8>(9u + reach))) return 2u;
		}
		return 0u;
	}
	if (type == 2u) {
		if (y >= 4u && y <= 14u) {
			eng::u8 half = static_cast<eng::u8>(2u + ((y - 4u) >> 1u));
			if (half > 7u) half = 7u;
			if (x >= static_cast<eng::u8>(8u - half) && x < static_cast<eng::u8>(8u + half)) {
				if (y == 4u || x == static_cast<eng::u8>(8u - half) ||
				    x == static_cast<eng::u8>(7u + half)) return 2u;
				if (y >= 7u && y <= 8u && (x == 5u || x == 10u)) return 3u;
				return 1u;
			}
		}
		return 0u;
	}
	if (type == 3u) {
		if (y >= 5u && y <= 10u && x >= 3u && x <= 12u) {
			if (x == 3u || x == 12u || y == 5u || y == 10u) return 2u;
			if (y == 7u && (x == 5u || x == 10u)) return 3u;
			return 1u;
		}
		if (y >= 6u && y <= 8u && ((x >= 1u && x <= 3u) || (x >= 12u && x <= 14u))) return 2u;
		if (y >= 11u && y <= 14u && ((x + y + phase) & 1u) == 0u &&
		    (x <= 4u || x >= 11u)) return 1u;
		return 0u;
	}
	return 0u;
}

/// Rellena la caché de arte de actores una vez en setup.
void build_actor_art_cache() {
	for (eng::u8 type = 0u; type < 4u; ++type) {
		for (eng::u8 phase = 0u; phase < 2u; ++phase) {
			for (eng::u8 y = 0u; y < 16u; ++y) {
				eng::u16 dat = 0u;
				eng::u16 datb = 0u;
				for (eng::u8 x = 0u; x < 16u; ++x) {
					const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
					const eng::u8 color = actor_pixel(type, phase, x, y);
					if ((color & 1u) != 0u) dat = static_cast<eng::u16>(dat | bit);
					if ((color & 2u) != 0u) datb = static_cast<eng::u16>(datb | bit);
				}
				g_actor_art[type][phase][static_cast<eng::usize>(y) * 2u] = dat;
				g_actor_art[type][phase][static_cast<eng::usize>(y) * 2u + 1u] = datb;
			}
		}
	}
}

} // namespace

void build() {
	build_tile_rows();
	build_map();
	build_actor_art_cache();
}

eng::u16 cave_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return g_cave_rows[glyph & 15u][variant & 3u][plane][row & 15u];
}

eng::u16 terrain_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return g_terrain_rows[glyph & 15u][variant & 3u][plane][row & 15u];
}

eng::Span<const eng::u16> map_cells() {
	return eng::Span<const eng::u16> {g_cells, kMapWidth * kMapHeight};
}

eng::Span<const eng::u16> background_cells() {
	return eng::Span<const eng::u16> {g_bg_cells, kMapWidth * kMapHeight};
}

eng::Span<const eng::u16> palette() {
	return eng::Span<const eng::u16> {kPalette, 32u};
}

void write_actor_art(eng::Span<eng::u16> words, eng::u8 slot, eng::u8 type,
		     eng::u8 phase) {
	const eng::u16* src = g_actor_art[type & 3u][phase & 1u];
	const eng::usize base = static_cast<eng::usize>(slot) * 32u;
	for (eng::u8 i = 0u; i < 32u; ++i) {
		words[base + i] = src[i];
	}
}

} // namespace caverna_art
