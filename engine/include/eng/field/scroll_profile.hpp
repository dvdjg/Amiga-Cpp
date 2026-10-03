#pragma once

/// \file scroll_profile.hpp
/// **Perfil de scroll estático**: el programador elige, con un tipo, cómo se
/// reparte el trabajo de Blitter por frame y cuán ancha es la banda de guarda,
/// sin cambiar el núcleo XYLimited ni pagar `switch`/indirección en runtime.
///
/// Modelo y regímenes: `docs/engine/architecture/FAST_SCROLL.md`.
///
/// Uso (una sola selección):
///   using Scroll = eng::field::ScrollFast2;   // o ScrollProgressive, ScrollFast1...
///   eng::field::XlimitedScene<kScrollConsts, eng::field::TileLayerMap, Scroll> scene {};
///
/// `ScrollProgressive` (por defecto) reproduce el comportamiento clásico de
/// 1 px/sub-paso con la guarda del modo de fetch; los perfiles `ScrollFastN`
/// pre-pintan por delante y avanzan hasta N tiles por frame.

#include <eng/core/types/types.hpp>

namespace eng::field {

/// Redondea `v` hacia cero a múltiplos de `tile` (avance en fronteras de tile).
/// Lo usan los perfiles con `prefill`: la cámara solo se detiene en límites de
/// tile, así el cambio de dirección (`direction_latched`) cae siempre en frontera.
constexpr eng::s32 snap_to_tiles(eng::s32 v, eng::u32 tile) {
	if (tile == 0u) return v;
	const eng::s32 t = static_cast<eng::s32>(tile);
	return (v / t) * t;
}

/// Relleno progresivo (clásico): sub-pasos de 1 px; la guarda la fija el modo de
/// fetch. `tiles == 0` = "no impone paso" (se usa el `max_step` de la config).
struct ProgressiveFill {
	static constexpr eng::u8 tiles = 0;
	static constexpr bool prefill = false;
	static constexpr eng::u8 sub_px = 0;
};

/// Relleno por ráfagas: hasta `N` columnas/filas completas por frame.
template <eng::u8 N>
struct TileBurstFill {
	static_assert(N >= 1u, "TileBurstFill necesita al menos 1 tile");
	static constexpr eng::u8 tiles = N;
	static constexpr bool prefill = true;
	static constexpr eng::u8 sub_px = 0;
};

/// Relleno **sub-tile**: paso de `Px` px por frame, menor que el tile (p. ej. tiles de 32×32 con
/// pasos de hasta 16 px). No ancla a frontera de tile (el `plane-shift` no es 0), así que usa el
/// camino progresivo con `max_step = Px` (correcto: pinta cada px revelado antes de avanzar). La
/// **optimización** de Blitter para este caso (pintar la tira en menos operaciones) es trabajo de
/// algoritmo aparte; ver `SCROLL_VARIANTS.md` §3.1.
template <eng::u8 Px>
struct SubTileFill {
	static_assert(Px >= 1u, "SubTileFill necesita al menos 1 px");
	static constexpr eng::u8 tiles = 0;
	static constexpr bool prefill = false;
	static constexpr eng::u8 sub_px = Px;
};

/// Pre-renderiza `C` columnas/filas y solo mueve punteros (>2-3 tiles/frame).
template <eng::u8 C>
struct StripPrerenderFill {
	static_assert(C >= 2u, "StripPrerenderFill necesita al menos 2 columnas");
	static constexpr eng::u8 tiles = C;
	static constexpr bool prefill = true;
	static constexpr eng::u8 sub_px = 0;
};

/// Guarda de la banda: `Tiles` de lookahead pre-pintados por delante de la ventana.
/// `Tiles == 0` = sin override (la guarda la decide el fetch: 32/64 px).
template <eng::u8 Tiles>
struct GuardTiles {
	static constexpr eng::u8 tiles = Tiles;
};

/// Perfil estático de scroll. `Fill` decide el trabajo por frame, `Guard` el
/// ancho de la banda de guarda; `DirectionLatched` limita el cambio de dirección
/// a fronteras de tile (simplifica la costura del 8-way).
///
/// Invariante (compile-time): con relleno por ráfagas, la guarda debe cubrir el
/// lookahead más un tile (`guard >= fill + 1`).
template <class FillT = ProgressiveFill, class GuardT = GuardTiles<0>, bool DirectionLatched = false>
struct ScrollProfile {
	static_assert(FillT::tiles == 0u || GuardT::tiles >= FillT::tiles + 1u,
	              "guarda insuficiente: necesita al menos fill+1 tiles de lookahead");
	static_assert(!DirectionLatched || FillT::tiles != 0u,
	              "direction_latched requiere un perfil con relleno por tiles (prefill)");

	using Fill = FillT;
	using Guard = GuardT;
	static constexpr eng::u8 fill_tiles = FillT::tiles;
	static constexpr eng::u8 guard_tiles = GuardT::tiles;
	static constexpr eng::u8 sub_px = FillT::sub_px; ///< paso sub-tile en px (0 = no aplica)
	static constexpr bool prefill = FillT::prefill;
	static constexpr bool direction_latched = DirectionLatched;

	/// Paso máximo de cámara por eje (px/frame) para este perfil y tamaño de tile.
	/// 0 = "no impone paso" (el llamador usa su `max_step`). Con relleno sub-tile
	/// (`SubTileFill`) el paso es `sub_px` (independiente del tamaño de tile).
	static constexpr eng::u32 max_step_px(eng::u32 tile) {
		if constexpr (sub_px != 0u) return sub_px;
		return static_cast<eng::u32>(fill_tiles) * tile;
	}
	/// Ancho/alto de guarda pedido (px) para `tile`; 0 = sin override.
	static constexpr eng::u32 guard_px(eng::u32 tile) {
		return static_cast<eng::u32>(guard_tiles) * tile;
	}
	/// Staging vertical del corkscrew en bloques. El clásico usa 2; un perfil
	/// rápido pide `guard_tiles` (>=2) para pre-pintar varias filas por delante.
	static constexpr eng::u8 y_staging_tiles() {
		return guard_tiles < 2u ? 2u : guard_tiles;
	}
};

/// Selecciones listas para usar (una línea en la demo). Los perfiles rápidos
/// llevan la dirección laceda a frontera de tile (avance por tiles completos).
using ScrollProgressive = ScrollProfile<ProgressiveFill, GuardTiles<0>>;
using ScrollFast1 = ScrollProfile<TileBurstFill<1>, GuardTiles<2>, true>;
using ScrollFast2 = ScrollProfile<TileBurstFill<2>, GuardTiles<3>, true>;
using ScrollFast4 = ScrollProfile<TileBurstFill<4>, GuardTiles<5>, true>;
/// Paso sub-tile (progresivo) para **tiles grandes** (32×32) con avance ≤ 16 px/frame.
using ScrollSubTile8 = ScrollProfile<SubTileFill<8>, GuardTiles<0>>;
using ScrollSubTile16 = ScrollProfile<SubTileFill<16>, GuardTiles<0>>;

} // namespace eng::field
