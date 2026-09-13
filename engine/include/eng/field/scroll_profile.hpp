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

#include <eng/core/types.hpp>

namespace eng::field {

/// Relleno progresivo (clásico): sub-pasos de 1 px; la guarda la fija el modo de
/// fetch. `tiles == 0` = "no impone paso" (se usa el `max_step` de la config).
struct ProgressiveFill {
	static constexpr eng::u8 tiles = 0;
	static constexpr bool prefill = false;
};

/// Relleno por ráfagas: hasta `N` columnas/filas completas por frame.
template <eng::u8 N>
struct TileBurstFill {
	static_assert(N >= 1u, "TileBurstFill necesita al menos 1 tile");
	static constexpr eng::u8 tiles = N;
	static constexpr bool prefill = true;
};

/// Pre-renderiza `C` columnas/filas y solo mueve punteros (>2-3 tiles/frame).
template <eng::u8 C>
struct StripPrerenderFill {
	static_assert(C >= 2u, "StripPrerenderFill necesita al menos 2 columnas");
	static constexpr eng::u8 tiles = C;
	static constexpr bool prefill = true;
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

	using Fill = FillT;
	using Guard = GuardT;
	static constexpr eng::u8 fill_tiles = FillT::tiles;
	static constexpr eng::u8 guard_tiles = GuardT::tiles;
	static constexpr bool prefill = FillT::prefill;
	static constexpr bool direction_latched = DirectionLatched;

	/// Paso máximo de cámara por eje (px/frame) para este perfil y tamaño de tile.
	/// 0 = "no impone paso" (el llamador usa su `max_step`).
	static constexpr eng::u32 max_step_px(eng::u32 tile) {
		return static_cast<eng::u32>(fill_tiles) * tile;
	}
	/// Ancho/alto de guarda pedido (px) para `tile`; 0 = sin override.
	static constexpr eng::u32 guard_px(eng::u32 tile) {
		return static_cast<eng::u32>(guard_tiles) * tile;
	}
};

/// Selecciones listas para usar (una línea en la demo).
using ScrollProgressive = ScrollProfile<ProgressiveFill, GuardTiles<0>>;
using ScrollFast1 = ScrollProfile<TileBurstFill<1>, GuardTiles<2>>;
using ScrollFast2 = ScrollProfile<TileBurstFill<2>, GuardTiles<3>>;
using ScrollFast4 = ScrollProfile<TileBurstFill<4>, GuardTiles<5>>;

} // namespace eng::field
