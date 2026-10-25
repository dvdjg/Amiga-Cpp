#pragma once

/// \file caverna_art.hpp
/// **Vistas de assets de 500 Caverna**: filas de tiles, mapas y constantes de
/// geometría. El arte se genera en host (`tools/assets/gen-caverna.mjs`) y la
/// demo lo carga por manifiesto; aquí no se calcula ningún píxel.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace caverna_art {

/// Lado de un tile en píxeles.
inline constexpr eng::u16 kTile = 16u;
/// Ancho del mapa en tiles (potencia de dos).
inline constexpr eng::u16 kMapWidth = 256u;
/// Alto del mapa en tiles.
inline constexpr eng::u16 kMapHeight = 20u;
/// Planos de cada playfield (DPF 3+3).
inline constexpr eng::u8 kPlanes = 3u;
/// Tamaño de una tabla de filas de tiles: 16 glyph × 4 variantes × planos × tile.
inline constexpr eng::usize kTileRowsWords =
	static_cast<eng::usize>(16u * 4u * kPlanes * kTile);

/// Liga las vistas cargadas del manifiesto. No copia; el `Assets` debe sobrevivir.
/// \param cave_rows  filas de la cueva (PF1), `kTileRowsWords`.
/// \param terrain_rows filas del terreno (PF2), `kTileRowsWords`.
/// \param cells  celdas del terreno (PF2), `kMapWidth*kMapHeight`.
/// \param bg_cells celdas de la cueva (PF1), `kMapWidth*kMapHeight`.
/// \return `false` si algún tamaño no coincide (no se liga nada).
[[nodiscard]] bool bind(eng::Span<const eng::u16> cave_rows,
			eng::Span<const eng::u16> terrain_rows,
			eng::Span<const eng::u16> cells,
			eng::Span<const eng::u16> bg_cells);

/// Fila de un tile de la cueva ya en planos.
[[nodiscard]] eng::u16 cave_row(eng::u8 glyph, eng::u8 variant, eng::u8 row,
				eng::u8 plane);

/// Fila de un tile del terreno ya en planos.
[[nodiscard]] eng::u16 terrain_row(eng::u8 glyph, eng::u8 variant, eng::u8 row,
				   eng::u8 plane);

/// Celdas del terreno jugable (PF2), fila-mayor.
[[nodiscard]] eng::Span<const eng::u16> map_cells();

/// Celdas de la cueva de fondo (PF1), fila-mayor.
[[nodiscard]] eng::Span<const eng::u16> background_cells();

} // namespace caverna_art
