#pragma once

/// \file runtime_scroll_geometry.hpp
/// **Geometría del anillo de tiras en RUNTIME** (`eng::playfield::RuntimeScrollGeometry`): los mismos
/// campos que `StripScrollGeometry<…>` (que es NTTP) pero calculados por `runtime_scroll_geometry(…)`
/// a partir de parámetros de runtime, con las **mismas invariantes** como *checks*.
///
/// Es el **primer paso** hacia que el planner elija la geometría **en runtime** (§7 de
/// `ROADMAP_GAME_API.md`), p. ej. para un editor que carga un formato no conocido a priori. El motor
/// NTTP (`StripScrollLayer`) sigue siendo el rápido; esta versión permite **conocer la geometría al
/// cargar** (no en compilación). Ver HOST-412 (equivalencia con el NTTP).

#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>

namespace eng::playfield {

/// Causa de fallo de `runtime_scroll_geometry`.
enum class ScrollGeomError : eng::u8 {
	BadTile,          ///< tile distinto de 16 o 32
	BadViewport,      ///< viewport no múltiplo del tile
	BadGuard,         ///< guarda < 2 palabras
	RingTooSmall,     ///< el anillo no cabe (pantalla + guarda + fetch)
	MapNotMultiple,   ///< el período del mapa no divide la span del anillo
};

/// Geometría del anillo calculada en **runtime** (ver doc del fichero).
struct RuntimeScrollGeometry {
	eng::u16 viewport_w = 0u;
	eng::u16 viewport_h = 0u;
	eng::u8 planes = 0u;
	eng::u16 tile_w = 0u;
	eng::u16 tile_h = 0u;
	bool split_vertical = false;
	eng::u16 split_line = 0u;
	bool split_crosses_255 = false;
	eng::u16 visible_words = 0u;
	eng::u16 ring_w_words = 0u;
	eng::u16 ring_w_bytes = 0u;
	eng::u16 fetch_words = 0u;
	eng::u16 bpl_mod = 0u;
	eng::u16 ring_h = 0u;
	eng::u16 column_tiles = 0u;
	eng::u16 row_tiles = 0u;
	eng::u16 column_planelines = 0u;
	eng::u16 max_blt_h = 1024u;
	eng::u8 column_blits = 0u;
	eng::u16 strip_row_words = 0u;
	eng::u16 row_planelines = 0u;
	eng::u16 bltdmod_row = 0u;
	eng::u16 strip_words = 0u;
	eng::u16 bltdmod_col = 0u;
};

/// Construye la geometría del anillo de tiras con parámetros de **runtime**. `nullopt`/error si no
/// cumple las invariantes que el NTTP verifica por `static_assert`.
[[nodiscard]] inline eng::util::Expected<RuntimeScrollGeometry, ScrollGeomError>
runtime_scroll_geometry(eng::u16 viewport_w, eng::u16 viewport_h, eng::u8 planes, eng::u16 tile_w,
			eng::u16 tile_h, eng::u16 guard_words = 2u, eng::u16 fetch_extra_words = 1u,
			bool split_vertical = false, eng::u16 ring_words = 0u, eng::u16 ring_lines = 0u,
			eng::u16 map_words = 0u) noexcept {
	if (tile_w != 16u && tile_w != 32u) return eng::util::unexpected(ScrollGeomError::BadTile);
	if (tile_h != 16u && tile_h != 32u) return eng::util::unexpected(ScrollGeomError::BadTile);
	if (viewport_w % tile_w != 0u || viewport_h % tile_h != 0u) {
		return eng::util::unexpected(ScrollGeomError::BadViewport);
	}
	if (guard_words < 2u) return eng::util::unexpected(ScrollGeomError::BadGuard);
	if (split_vertical && viewport_h > 256u) return eng::util::unexpected(ScrollGeomError::BadViewport);

	RuntimeScrollGeometry g {};
	g.viewport_w = viewport_w;
	g.viewport_h = viewport_h;
	g.planes = planes;
	g.tile_w = tile_w;
	g.tile_h = tile_h;
	g.split_vertical = split_vertical;
	g.split_line = static_cast<eng::u16>(0x2cu + viewport_h);
	g.split_crosses_255 = g.split_line > 255u;
	g.visible_words = static_cast<eng::u16>(viewport_w / 16u);
	g.ring_w_words = (map_words != 0u)
				 ? static_cast<eng::u16>(g.visible_words + map_words)
				 : ((ring_words != 0u)
					    ? ring_words
					    : static_cast<eng::u16>(g.visible_words + guard_words +
								    fetch_extra_words));
	if (g.ring_w_words < static_cast<eng::u16>(g.visible_words + guard_words + fetch_extra_words)) {
		return eng::util::unexpected(ScrollGeomError::RingTooSmall);
	}
	if (map_words != 0u && (g.ring_w_words - g.visible_words) % map_words != 0u) {
		return eng::util::unexpected(ScrollGeomError::MapNotMultiple);
	}
	g.ring_w_bytes = static_cast<eng::u16>(g.ring_w_words * 2u);
	g.fetch_words = static_cast<eng::u16>(g.visible_words + fetch_extra_words);
	g.bpl_mod = static_cast<eng::u16>(static_cast<eng::u32>(planes) * g.ring_w_bytes -
					  g.fetch_words * 2u);
	g.ring_h = (ring_lines != 0u)
			   ? ring_lines
			   : static_cast<eng::u16>(viewport_h + (split_vertical ? 2u * tile_h : 0u));
	g.column_tiles = static_cast<eng::u16>(g.ring_h / tile_h);
	g.row_tiles = g.ring_w_words;
	g.column_planelines = static_cast<eng::u16>(g.ring_h * planes);
	g.column_blits = static_cast<eng::u8>((g.column_planelines + g.max_blt_h - 1u) / g.max_blt_h);
	g.strip_row_words = g.ring_w_words;
	g.row_planelines = static_cast<eng::u16>(tile_h * planes);
	g.bltdmod_row = static_cast<eng::u16>(g.ring_w_bytes - g.ring_w_words * 2u);
	g.strip_words = static_cast<eng::u16>(tile_w / 16u);
	g.bltdmod_col = static_cast<eng::u16>(g.ring_w_bytes - g.strip_words * 2u);
	return g;
}

} // namespace eng::playfield
