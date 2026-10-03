#pragma once

/// \file scroll_variant.hpp
/// **Selección nombrada de variante de scroll** (paso 2/3 de `SCROLL_VARIANTS.md`): traduce los
/// nombres de la referencia **ScrollingTricks** (XLimited, XUnlimited, YUnlimited(2), XYLimited,
/// XYUnlimited(2)) a los campos de `XlimitedConfigT` que ya implementan el algoritmo. La variante
/// `_64` (fetch 1x4x) se pide con `wide_fetch = true`.
///
/// No sustituye al modelo de velocidad (`ScrollProfile`/`FAST_SCROLL.md`, ortogonal) ni al
/// algoritmo (`ScrollEngine`): solo fija **ejes, wrap y ancho de fetch**. El vídeo-splitting de las
/// variantes `2` (Y) y las optimizaciones de paso en tiles de 32×32 quedan como trabajo de
/// algoritmo (ver `SCROLL_VARIANTS.md` §3).
///
/// ```cpp
/// eng::field::XlimitedConfig cfg {};
/// cfg.viewport_w = 320; cfg.viewport_h = 256;
/// cfg.map.width = 64; cfg.map.height = 18;          // dims del mapa ANTES de aplicar
/// eng::field::apply_scroll_variant(cfg, eng::field::ScrollVariant::XYLimited);
/// ```

#include <eng/field/xlimited_base.hpp>

namespace eng::field {

/// Variantes de la referencia ScrollingTricks (`Docs/algorithms-uk.html`).
enum class ScrollVariant : u8 {
	XLimited,     ///< ←/→ acotado: anillo X, mapa finito (sin wrap).
	XUnlimited,   ///< ←/→ sin fin: X toroidal (wrap X).
	YUnlimited,   ///< ↑/↓ sin fin: Y toroidal, sin split (bitmap duplicado).
	YUnlimited2,  ///< ↑/↓ sin fin con video-splitting.
	XYLimited,    ///< 8-way acotado (corkscrew).
	XYUnlimited,  ///< 8-way sin fin (wrap X e Y).
	XYUnlimited2, ///< 8-way sin fin con video-splitting.
};

/// ¿La variante scrollea el eje X? ¿Y? ¿Envuelve (mapa toroidal, "unlimited")?
[[nodiscard]] constexpr bool variant_scrolls_x(ScrollVariant v) noexcept {
	return v != ScrollVariant::YUnlimited && v != ScrollVariant::YUnlimited2;
}
[[nodiscard]] constexpr bool variant_scrolls_y(ScrollVariant v) noexcept {
	return v != ScrollVariant::XLimited && v != ScrollVariant::XUnlimited;
}
/// "Unlimited" = el mapa se recorre sin fin en ese eje (wrap = dimensión del mapa).
[[nodiscard]] constexpr bool variant_wraps_x(ScrollVariant v) noexcept {
	return v == ScrollVariant::XUnlimited || v == ScrollVariant::XYUnlimited ||
	       v == ScrollVariant::XYUnlimited2;
}
[[nodiscard]] constexpr bool variant_wraps_y(ScrollVariant v) noexcept {
	return v == ScrollVariant::YUnlimited || v == ScrollVariant::YUnlimited2 ||
	       v == ScrollVariant::XYUnlimited || v == ScrollVariant::XYUnlimited2;
}
/// Las variantes `2` usan **video-splitting** (Y); el engine lo modela con `y_mode = Ring` +
/// `display_height` (ver `AMIGA_8WAY_SCROLLING.md`).
[[nodiscard]] constexpr bool variant_video_split(ScrollVariant v) noexcept {
	return v == ScrollVariant::YUnlimited2 || v == ScrollVariant::XYUnlimited2;
}

/// Aplica los **ejes**, el **wrap** y el **ancho de fetch** de `v` a `cfg`. El llamador debe haber
/// fijado `cfg.map.width`/`height`, `viewport_w`, `tile_width/h`. `wide_fetch` = variante `_64`
/// (fetch 1x4x, `bitmap_width = viewport_w + 64`); si no, fetch clásico (16 px, `bitmap_width`
/// automático). No toca `max_step`/perfil ni el vídeo-splitting (trabajo de algoritmo aparte).
template <class MapT>
constexpr void apply_scroll_variant(XlimitedConfigT<MapT>& cfg, ScrollVariant v,
				    bool wide_fetch = false) noexcept {
	cfg.x_mode = variant_scrolls_x(v) ? AxisPolicy::Ring : AxisPolicy::Off;
	cfg.y_mode = variant_scrolls_y(v) ? AxisPolicy::Ring : AxisPolicy::Off;
	cfg.map.wrap_x = variant_wraps_x(v) ? cfg.map.width : 0u;
	cfg.map.wrap_y = variant_wraps_y(v) ? cfg.map.height : 0u;
	cfg.fetch_mode = wide_fetch ? 3u : 0u; // 3 = BPL32+BPAGEM (1x4x)
	cfg.bitmap_width = wide_fetch
				   ? static_cast<u16>(static_cast<u32>(cfg.viewport_w) + 64u)
				   : 0u; // 0 = auto (viewport_w + EXTRAWIDTH según fetch)
}

/// **XLimited con Y mayor que el viewport** (scroll horizontal con mapa alto): el anillo X usa un
/// `display_height` mayor que el alto visible (p. ej. HUD que reduce `viewport_h`), para que el walk
/// plane-shifted no colisione `mapy`. Devuelve el `display_height` mínimo (`viewport_h + 2*tile_h`).
[[nodiscard]] constexpr u16 xlimited_tall_y_display(u16 viewport_h, u16 tile_h) noexcept {
	return static_cast<u16>(viewport_h + 2u * tile_h);
}

/// **YLimited con X más ancha que la pantalla** (arcades verticales): banda de guarda ancha
/// (fetch 1x4x → `bitmap_width = viewport_w + 64`; con viewport 320 da 384) y X **acotado** a la
/// banda (no toroidal) sobre un scroll principal en Y.
template <class MapT>
constexpr void apply_ylimited_wide_x(XlimitedConfigT<MapT>& cfg) noexcept {
	apply_scroll_variant(cfg, ScrollVariant::YUnlimited, /*wide_fetch=*/true);
	cfg.map.wrap_x = 0u; // X acotado a la banda (no toroidal)
}

} // namespace eng::field
