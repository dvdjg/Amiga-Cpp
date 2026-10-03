#pragma once

/// \file scroll_plan.hpp
/// **Plan declarativo de una capa de scroll** (`eng::playfield::ScrollPlan`): el **vocabulario
/// común** con el que un juego describe su capa —geometría, política y contenido— sin conocer los
/// motores. Cada motor consume lo que admite y **comparte la cámara** (`track_camera`/`follow_camera`):
///
///   - el **camino de tiras** (`StripScrollLayer`) usa el **contenido** (`tilemap`, que liga con
///     `set_tilemap` y le deriva los tamaños) y respeta la geometría fijada en su tipo `Geom`;
///   - el **corcóscru** (`XlimitedScene`) usa la **geometría** y la **paleta** (lo demás es
///     específico de su técnica: `MapT`, row fns, DPF) — se siembra con `apply_scroll_plan`.
///
/// El objetivo es que **el juego escriba un plan, no dos configuraciones distintas**, y que el
/// **planner** (cámara/tilemap de juego, §7 de `ROADMAP_GAME_API`) pueda consumirlo cuando elija el
/// motor.

#include <eng/core/types/types.hpp>
#include <eng/field/tilemap_view.hpp>

namespace eng::playfield {

/// Plan declarativo de una capa de scroll (ver doc del fichero).
struct ScrollPlan {
	// --- Geometría del campo (la usa el motor que la admita a runtime) --------
	eng::u16 viewport_w = 320u;
	eng::u16 viewport_h = 256u;
	eng::u16 tile_w = 16u;
	eng::u16 tile_h = 16u;
	eng::u8 planes = 3u;
	eng::u16 display_height = 0u; ///< alto del anillo vertical (`0` = lo deriva el motor)

	// --- Política de scroll ---------------------------------------------------
	eng::u16 map_period_words = 0u; ///< período del mapa toroidal (words); `0` = mapa acotado
	eng::u8 speed_px = 4u;          ///< velocidad máxima de scroll (px/frame)

	// --- Contenido (asset de tiles: banco + mapa de ids + paleta) -------------
	TilemapView tilemap {}; ///< lo consume el camino de tiras (`set_tilemap`)
};

} // namespace eng::playfield
