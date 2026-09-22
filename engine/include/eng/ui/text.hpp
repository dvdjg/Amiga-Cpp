#pragma once

/// \file text.hpp
/// **Medida y recorte de texto** de UI (`eng::ui`). No añade fuente nueva: mide con `Font8`
/// (8×8, avance 8 px) y se apoya en `field::Surface` para pintar. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §6.

#include <eng/core/box.hpp>
#include <eng/core/types.hpp>
#include <eng/core/utf8.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/ui/theme.hpp>

namespace eng::ui {

/// Ancho en píxeles de `s` con la fuente por defecto (`Font8`, avance 8 px por **code point**,
/// no por byte: una `ñ` o un acento en UTF-8 ocupan 2 bytes pero un glifo).
[[nodiscard]] inline eng::u16 text_width(const char* s) noexcept {
	if (s == nullptr) {
		return 0u;
	}
	const eng::u8* p = reinterpret_cast<const eng::u8*>(s);
	eng::u16 n = 0u;
	for (;;) {
		const eng::u32 cp = eng::utf8::decode(p);
		if (cp == 0u) {
			break;
		}
		++n;
	}
	return static_cast<eng::u16>(n * 8u);
}

/// Dibuja `s` recortando a `clip` **sin partir glifos a medias**: solo pinta los glifos
/// enteramente dentro del clip y se detiene en el primero que no cabe. Se saltan los glifos
/// que quedan antes del borde izquierdo. `Painter` debe exponer
/// `codepoint(x, y, cp, fg)` (p. ej. `UiPainter`); al ser plantilla no acopla `text.hpp` a
/// `painter.hpp`.
template <class Painter>
void draw_text_clipped(Painter& p, Rect clip, eng::s16 x, eng::s16 y,
		       const char* s, eng::u8 fg) {
	if (s == nullptr || clip.empty()) {
		return;
	}
	const eng::s16 x0 = clip.x;
	const eng::s16 x1 = clip.right();
	const eng::u8* q = reinterpret_cast<const eng::u8*>(s);
	for (;;) {
		const eng::u32 cp = eng::utf8::decode(q);
		if (cp == 0u) {
			break;
		}
		if (x + 7 > x1) {
			break; // el glifo no cabe entero
		}
		if (x >= x0 && cp >= 32u) {
			p.codepoint(x, y, cp, fg);
		}
		x = static_cast<eng::s16>(x + 8);
	}
}

} // namespace eng::ui
