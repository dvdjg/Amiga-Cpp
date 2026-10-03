#pragma once

/// \file tilemap_view.hpp
/// **Vista de un asset de tilemap** (`eng::playfield::TilemapView`): banco de tiles **contiguos**
/// (`[tile][line][plane]`) + mapa de ids + paleta, sin poseer memoria. Es lo que liga un **atlas**
/// generado por el pipeline (tiles + mapa + paleta) a la capa de scroll (`StripScrollLayer`) sin
/// que el juego escriba un adaptador a mano: el contrato que consume el controlador es
/// `tile_at(col, row) -> id de tile del banco`.
///
/// El **banco** ya empaquetado (tiles de `tile_h*tile_w/8*planes` palabras) lo produce el pipeline
/// de tiles (§4 de `ROADMAP_GAME_API`); aquí solo se describe.

#include <eng/core/types/types.hpp>
#include <eng/graphics/palette.hpp>

namespace eng::playfield {

/// Vista no propietaria de un tilemap: banco de tiles + mapa de ids + paleta.
struct TilemapView {
	const eng::u16* bank = nullptr;      ///< tiles contiguos `[tile][line][plane]`
	eng::u16 bank_stride_words = 0u;     ///< palabras por tile en `bank`
	const eng::u16* tiles = nullptr;     ///< ids de tile, `rows*cols` en orden de filas
	eng::u16 cols = 0u;                  ///< columnas del mapa (periodo del anillo si toroidal)
	eng::u16 rows = 0u;                  ///< filas del mapa
	eng::PaletteWords palette {};        ///< paleta del display (palabras Amiga)

	/// `true` si la vista tiene banco, mapa y dimensiones válidas.
	[[nodiscard]] constexpr bool valid() const noexcept {
		return bank != nullptr && tiles != nullptr && cols != 0u && rows != 0u &&
		       bank_stride_words != 0u;
	}

	/// Id del tile del banco en la celda `(col, row)`, con **wrap toroidal**.
	[[nodiscard]] constexpr eng::u16 tile_at(eng::u16 col, eng::u16 row) const noexcept {
		return tiles[(row % rows) * cols + (col % cols)];
	}
};

} // namespace eng::playfield
