#pragma once

/// \file theme.hpp
/// **Tema y estilos del HUD** (`eng::hud`): estilos lógicos (`MeterStyle`, `HudFont`), descriptor
/// del **atlas de iconos** (memoria Chip tipada) y `HudTheme` (paleta de banda, índices lógicos y
/// métricas). El tema enlaza el tipo lógico con la presentación; el modelo no cambia al cambiar de
/// tema. Diseño: `docs/engine/architecture/HUD_SYSTEM.md` §8.
///
/// Verificación: tipos cubiertos por **HOST-467**; paleta de banda en hardware con la demo
/// `302_hud_band` (blanco/amarillo/rojo propios de la franja).

#include <eng/core/types/domains.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>

namespace eng::hud {

/// Estilo de presentación de un medidor: el mismo `Meter` se materializa como relleno lineal,
/// segmentos, *pips* de sprite o barra raster según el tema.
enum class MeterStyle : eng::u8 {
	Linear = 0, ///< barra continua (tiles, Blitter o Copper de un color)
	Segmented,  ///< segmentos discretos (energía por celdas)
	Pips,       ///< iconos repetidos (vidas, munición)
	Raster,     ///< frontera de color por Copper (vertical, un `MOVE` por frame)
};

/// Fuente lógica del HUD; el presentador resuelve la fuente concreta (`Font8`/`Font5x7`/`Font3x5`).
enum class HudFont : eng::u8 {
	Font5x7 = 0, ///< compacta, la canónica del HUD
	Font8,       ///< texto legible
	Font3x5,     ///< micro-fuente para etiquetas densas
};

/// Tag de dominio del **atlas de iconos** (memoria Chip que lee el Blitter o el Blitter-copy).
struct HudAtlasTag;

/// Descriptor del atlas de iconos del HUD: un bitmap planar en **Chip** con una tabla de frames.
/// El presentador calcula el offset del frame; se prefieren dimensiones potencia de dos para usar
/// desplazamientos. La generación (cuantización → planar → tabla) es del pipeline de host
/// (`HUD_SYSTEM.md` §8.2); el runtime solo referencia.
struct IconAtlas {
	eng::ChipView<HudAtlasTag> words {}; ///< memoria del atlas (Chip; planos interleaved por filas)
	eng::u16 frame_w = 16u;              ///< ancho de frame en píxeles
	eng::u16 frame_h = 16u;              ///< alto de frame en píxeles
	eng::u16 count = 0u;                 ///< número de frames
	eng::u16 stride_words = 0u;          ///< palabras por frame y plano (offset del frame)
	eng::u8 planes = 1u;                 ///< planos del atlas (1..6)
	bool masked = false;                 ///< true si cada frame lleva máscara (cookie-cut)

	/// ¿El descriptor puede direccionarse? (memoria, frames y geometría presentes).
	[[nodiscard]] constexpr bool valid() const noexcept {
		return !words.empty() && count != 0u && frame_w != 0u && frame_h != 0u && stride_words != 0u;
	}
};

/// Tema del HUD: roles de color lógicos, métricas y atlas. Los índices se resuelven contra la
/// paleta de la región (banda con paleta propia) o contra la paleta del juego (overlay).
struct HudTheme {
	eng::PaletteWords band_palette {}; ///< palabras COLOR de la banda si la región tiene paleta propia
	IconAtlas atlas {};                ///< atlas de iconos por valor (la app es la dueña)
	bool has_atlas = false;            ///< true si `atlas` es válido y debe usarse
	HudFont font = HudFont::Font5x7;   ///< fuente lógica de contadores y etiquetas
	eng::u8 bg = 0u;                   ///< fondo de panel/franja
	eng::u8 fg = 1u;                   ///< texto principal
	eng::u8 accent = 2u;               ///< realce (selección, foco)
	eng::u8 warn = 3u;                 ///< aviso (vida baja, error)
	eng::u8 meter_fill = 2u;           ///< relleno del medidor
	eng::u8 meter_back = 0u;           ///< fondo del medidor
	eng::u8 border = 1u;               ///< marco de paneles y botones
	eng::u8 gap = 1u;                  ///< separación entre elementos (px)
	eng::u8 meter_h = 8u;              ///< alto por defecto del medidor (px)
};

/// Preset **clásico** (blanco/amarillo/rojo sobre negro): el reparto canónico (302/303/306).
[[nodiscard]] constexpr HudTheme hud_theme_classic() noexcept {
	HudTheme t {};
	t.bg = 0u;
	t.fg = 1u;
	t.accent = 2u;
	t.warn = 3u;
	t.meter_fill = 2u;
	t.meter_back = 0u;
	t.border = 1u;
	return t;
}

/// Preset **ámbar** (naranja/rojo/amarillo sobre negro): intercambia el reparto de roles
/// (fg/accent/warn) para paletas cálidas; el modelo no cambia, solo los índices que pinta.
[[nodiscard]] constexpr HudTheme hud_theme_amber() noexcept {
	HudTheme t {};
	t.bg = 0u;
	t.fg = 2u;
	t.accent = 3u;
	t.warn = 1u;
	t.meter_fill = 3u;
	t.meter_back = 0u;
	t.border = 2u;
	return t;
}

/// ¿Los roles de color del tema caben en una paleta de `colors` entradas? (validación barata
/// antes de aplicar un tema a una región con paleta propia).
[[nodiscard]] constexpr bool hud_theme_fits(const HudTheme& t, eng::u8 colors) noexcept {
	return t.bg < colors && t.fg < colors && t.accent < colors && t.warn < colors &&
	       t.meter_fill < colors && t.meter_back < colors && t.border < colors;
}

} // namespace eng::hud
