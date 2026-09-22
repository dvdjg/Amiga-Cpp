#pragma once

/// \file theme.hpp
/// **Tema de UI** (`eng::ui`): colores lógicos (índices de la paleta del playfield) y métricas.
/// Los widgets consultan `theme()`, nunca colores mágicos. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §7.
///
/// El rectángulo de la UI es **`eng::Box`** (el rect único del engine); aquí se aliasa como
/// `eng::ui::Rect` para el vocabulario de la GUI sin introducir un quinto rectángulo.

#include <eng/core/box.hpp>
#include <eng/core/types.hpp>

namespace eng::ui {

/// Rectángulo de UI: es el `eng::Box` del engine (origen + tamaño, `contains` inclusivo).
using Rect = eng::Box;

/// Estilo de marco de una superficie del tema.
enum class FrameStyle : eng::u8 {
	Flat,     ///< sin bisel
	Raised,   ///< relieve: shine arriba/izquierda, shadow abajo/derecha
	Recessed, ///< hundido: shadow arriba/izquierda, shine abajo/derecha
	Double,   ///< doble marco (shadow + shine interior)
};

/// Tema de UI: colores lógicos y métricas. Cambiar de tema = reasignar la struct y marcar todo
/// el árbol sucio. Es una struct pequeña y copiable (sin variables globales mutables).
struct UiTheme {
	// Colores lógicos (índices de la paleta del playfield).
	eng::u8 bg = 0u;          ///< fondo de panel
	eng::u8 shine = 1u;       ///< borde claro (relieve)
	eng::u8 shadow = 2u;      ///< borde oscuro (relieve)
	eng::u8 fill = 0u;        ///< relleno normal
	eng::u8 fill_active = 1u; ///< pulsado / seleccionado
	eng::u8 text = 3u;        ///< texto normal
	eng::u8 text_dim = 2u;    ///< texto deshabilitado
	eng::u8 edit_bg = 0u;     ///< fondo de campo de edición
	eng::u8 focus_ring = 1u;  ///< anillo/indicador de foco

	// Métricas.
	eng::u8 border = 1u;  ///< grosor del borde
	eng::u8 pad_x = 4u;
	eng::u8 pad_y = 2u;
	eng::u8 btn_h = 12u;  ///< alto de botón por defecto
	eng::u8 check_s = 9u; ///< lado del checkbox
	eng::u8 radio_s = 9u; ///< diámetro del radio button

	FrameStyle panel_frame = FrameStyle::Raised;
	FrameStyle button_frame = FrameStyle::Raised;
};

/// Workbench 1.3: grises sobre negro, relieve marcado.
inline constexpr UiTheme kThemeWb13 {
	.bg = 1u, .shine = 2u, .shadow = 0u, .fill = 1u, .fill_active = 2u,
	.text = 0u, .text_dim = 3u, .edit_bg = 1u, .focus_ring = 2u,
	.panel_frame = FrameStyle::Raised, .button_frame = FrameStyle::Raised,
};

/// Workbench 2.x/3.x: más contraste entre relleno y bordes.
inline constexpr UiTheme kThemeWb2 {
	.bg = 2u, .shine = 3u, .shadow = 0u, .fill = 2u, .fill_active = 3u,
	.text = 1u, .text_dim = 0u, .edit_bg = 2u, .focus_ring = 3u,
	.panel_frame = FrameStyle::Raised, .button_frame = FrameStyle::Raised,
};

/// Tema plano para juegos: sin relieve ni doble marco.
inline constexpr UiTheme kThemeFlat {
	.bg = 0u, .shine = 1u, .shadow = 0u, .fill = 0u, .fill_active = 1u,
	.text = 1u, .text_dim = 2u, .edit_bg = 0u, .focus_ring = 1u,
	.panel_frame = FrameStyle::Flat, .button_frame = FrameStyle::Flat,
};

} // namespace eng::ui
