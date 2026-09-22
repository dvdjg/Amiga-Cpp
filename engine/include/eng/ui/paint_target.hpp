#pragma once

/// \file paint_target.hpp
/// **Destino de dibujo neutral** (`eng::ui::PaintTarget`): la `field::Surface` del engine o (con
/// `-DENG_UI_INTUITION`) el `RastPort` de una ventana de Intuition. Es el *seam* que deja que
/// `UiPainter` y los widgets no dependan del backend (ver
/// `docs/guides/roadmap/ROADMAP_WORKBENCH.md`, fases W0/W8).
///
/// Lleva el **recorte** (en píxeles de **destino**) y el **aspecto del píxel** `aspect_x` (8.8:
/// píxeles de destino por píxel lógico a lo ancho; `256` = píxel cuadrado). Un destino *hi-res* de
/// Workbench usa `512` (cada píxel lógico ocupa 2 de destino); en el engine, `256`.

#include <eng/core/box.hpp>
#include <eng/core/types.hpp>
#include <eng/field/surface.hpp>

namespace eng::ui {

/// Destino de dibujo. Hoy solo `Surface` (engine); el `RastPort` de Intuition se añade en el build
/// `-DENG_UI_INTUITION` (fase W4).
struct PaintTarget {
	enum class Kind : eng::u8 { Surface };

	Kind kind = Kind::Surface;
	eng::field::Surface* surface = nullptr; ///< destino del engine (`kind == Surface`)
	eng::Box clip {};                       ///< recorte, en píxeles de destino
	eng::u16 aspect_x = 256u;               ///< 8.8: píxeles de destino por píxel lógico a lo ancho

	/// Destino sobre una `Surface`: su recorte natural y píxel cuadrado (sin escalado).
	[[nodiscard]] static PaintTarget from_surface(eng::field::Surface& s) noexcept {
		return PaintTarget {Kind::Surface, &s, eng::field::box_of(s.clip()), 256u};
	}
};

} // namespace eng::ui
