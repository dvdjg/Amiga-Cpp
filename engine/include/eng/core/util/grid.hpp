#pragma once

/// \file grid.hpp
/// **Coordenadas de rejilla** (`eng::util`): tile↔píxel, proyección isométrica
/// (diamante) y vecinos hexagonales. Aritmética entera, sin `float` y sin divisiones
/// libgcc: las conversiones inversas requieren tamaño/tile potencia de dos (NTTP) y se
/// resuelven con desplazamientos.
///
/// Uso:
///   const eng::Point2s px = eng::util::grid_to_world({3, 2}, 16, 16);
///   const auto t = eng::util::world_to_grid<16, 16>(px.x, px.y);
///   const eng::Point2s iso = eng::util::iso_to_screen({3, 2}, 8, 4);
///   eng::util::Hex vecinos[6];
///   eng::util::hex_neighbors({2, 1}, vecinos);

#include <eng/core/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/arith.hpp>

namespace eng::util {

/// Coordenada de tile (enteros con signo).
struct TileCoord {
	s16 x = 0;
	s16 y = 0;
	[[nodiscard]] constexpr bool operator==(const TileCoord& other) const noexcept {
		return x == other.x && y == other.y;
	}
};

/// Origen en píxeles de un tile: `(x·tile_w, y·tile_h)`.
[[nodiscard]] constexpr Point2s grid_to_world(TileCoord t, u16 tile_w, u16 tile_h) noexcept {
	const s32 wx = eng::math::mul_wide(t.x, static_cast<s16>(tile_w));
	const s32 wy = eng::math::mul_wide(t.y, static_cast<s16>(tile_h));
	return Point2s {static_cast<s16>(wx), static_cast<s16>(wy)};
}

/// Tile que contiene el píxel `(x,y)`, con división **floor** (correcta con negativos).
/// `TileW`/`TileH` deben ser potencia de dos: se resuelve con desplazamiento.
template <u16 TileW, u16 TileH>
[[nodiscard]] constexpr TileCoord world_to_grid(s32 x, s32 y) noexcept {
	static_assert(TileW > 0u && TileH > 0u, "world_to_grid: tamaño de tile > 0");
	static_assert(has_single_bit(TileW) && has_single_bit(TileH),
		      "world_to_grid: TileW/TileH potencia de dos");
	return TileCoord {static_cast<s16>(x >> static_cast<s32>(bit_width(TileW) - 1)),
			  static_cast<s16>(y >> static_cast<s32>(bit_width(TileH) - 1))};
}

/// Proyección **isométrica**: el tile `(0,0)` en el origen; `half_w`/`half_h` son la
/// mitad del ancho/alto del diamante. `sx = (x−y)·half_w`, `sy = (x+y)·half_h`.
[[nodiscard]] constexpr Point2s iso_to_screen(TileCoord t, u16 half_w, u16 half_h) noexcept {
	const s32 sx = eng::math::mul_wide(static_cast<s16>(t.x - t.y), static_cast<s16>(half_w));
	const s32 sy = eng::math::mul_wide(static_cast<s16>(t.x + t.y), static_cast<s16>(half_h));
	return Point2s {static_cast<s16>(sx), static_cast<s16>(sy)};
}

/// Inversa de `iso_to_screen` para mitades potencia de dos (desplazamientos):
/// `tx = (sx/half_w + sy/half_h)/2`, `ty = (sy/half_h − sx/half_w)/2`.
template <u16 HalfW, u16 HalfH>
[[nodiscard]] constexpr TileCoord screen_to_iso(s32 sx, s32 sy) noexcept {
	static_assert(has_single_bit(HalfW) && has_single_bit(HalfH),
		      "screen_to_iso: HalfW/HalfH potencia de dos");
	const s32 u = sx >> static_cast<s32>(bit_width(HalfW) - 1);
	const s32 v = sy >> static_cast<s32>(bit_width(HalfH) - 1);
	return TileCoord {static_cast<s16>((u + v) >> 1), static_cast<s16>((v - u) >> 1)};
}

/// Coordenada **hexagonal axial** (dos ejes a 60°).
struct Hex {
	s16 q = 0;
	s16 r = 0;
	[[nodiscard]] constexpr bool operator==(const Hex& other) const noexcept {
		return q == other.q && r == other.r;
	}
};

/// Las seis direcciones axiales (E, NE, NW, W, SW, SE).
inline constexpr Hex kHexDirections[6] = {{1, 0},  {1, -1}, {0, -1},
					  {-1, 0}, {-1, 1}, {0, 1}};

/// Vecino de `h` en la dirección `dir` (0..5).
[[nodiscard]] constexpr Hex hex_neighbor(Hex h, u8 dir) noexcept {
	const Hex d = kHexDirections[dir % 6u];
	return Hex {static_cast<s16>(h.q + d.q), static_cast<s16>(h.r + d.r)};
}

/// Escribe los seis vecinos de `h`.
constexpr void hex_neighbors(Hex h, Hex (&out)[6]) noexcept {
	for (u8 i = 0; i < 6u; ++i) {
		out[i] = hex_neighbor(h, i);
	}
}

/// Distancia hexagonal (pasos entre dos hexágonos), vía coordenadas cúbicas.
[[nodiscard]] constexpr s16 hex_distance(Hex a, Hex b) noexcept {
	const s16 dq = static_cast<s16>(a.q - b.q);
	const s16 dr = static_cast<s16>(a.r - b.r);
	const s16 ds = static_cast<s16>(-dq - dr);
	const s16 abs_q = dq < 0 ? static_cast<s16>(-dq) : dq;
	const s16 abs_r = dr < 0 ? static_cast<s16>(-dr) : dr;
	const s16 abs_s = ds < 0 ? static_cast<s16>(-ds) : ds;
	return static_cast<s16>((abs_q + abs_r + abs_s) >> 1);
}

} // namespace eng::util
