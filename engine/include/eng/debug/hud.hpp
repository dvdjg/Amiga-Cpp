#pragma once

/// \file hud.hpp
/// **Franja de depuración** reutilizable: `draw_hud(screen, "NOMBRE", version, frame)` pinta
/// en la línea inferior del contexto la etiqueta `NOMBRE vX.Y.build F <frames>`.
///
/// Es el HUD mínimo que exige la norma del repo (un solo `text_words`: dos glifos de Font8
/// por palabra de 16 px, una escritura por fila y plano; sin bucle por píxel). El juego solo
/// reserva la franja escribiendo el HUD **después** de su render; el helper no borra fondo
/// (si la figura llega a la franja, el llamador puede `clear`/`fill_rect` antes).
///
/// ```cpp
/// auto s = app.screen();
/// // ... dibujo del frame ...
/// eng::debug::draw_hud(s, "CIRCLES", kVersion, app.frame());
/// app.present();
/// ```
///
/// El color por defecto es el índice 1 (válido en 1 bpp y en paletas de juego que reservan
/// ese índice); un display con otra paleta pasa su `color` explícito.

#include <eng/core/types/box.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/version.hpp>

namespace eng::debug {

/// Pinta `NOMBRE vX.Y.build F <frame>` en la línea inferior de `screen`.
/// \param screen   contexto de dibujo (`eng::Screen` o cualquier tipo con `bounds()` y
///                 `text_words(x, y, texto, color)`).
/// \param name     nombre corto del juego/demo (se separa de la versión con un espacio).
/// \param version  versión `major.minor.build` (el build lo inyecta el build script).
/// \param frame    contador de frames de juego.
/// \param color    índice de paleta del texto (1 por defecto).
/// \return el resultado de `text_words` (`false` si la franja no cabe en el clip).
template <class ScreenT>
bool draw_hud(ScreenT& screen, const char* name, const Version& version, u32 frame,
	      u8 color = 1u) {
	char line[64] {};
	u8 n = 0u;
	for (const char* p = name; p != nullptr && *p != '\0' && n + 1u < sizeof(line); ++p) {
		line[n++] = *p;
	}
	if (n != 0u && n + 1u < sizeof(line)) {
		line[n++] = ' ';
	}
	char label[24] {};
	const u8 vlen = version_label(version, label, sizeof(label));
	for (u8 i = 0u; i < vlen && n + 1u < sizeof(line); ++i) {
		line[n++] = label[i];
	}
	if (n + 1u < sizeof(line)) {
		line[n++] = ' ';
	}
	if (n + 1u < sizeof(line)) {
		line[n++] = 'F';
	}
	if (n + 1u < sizeof(line)) {
		line[n++] = ' ';
	}
	char digits[12] {};
	u8 d = 0u;
	u32 v = frame;
	do {
		digits[d++] = static_cast<char>('0' + (v % 10u));
		v /= 10u;
	} while (v != 0u && d < sizeof(digits));
	while (d > 0u && n + 1u < sizeof(line)) {
		line[n++] = digits[--d];
	}
	line[n] = '\0';
	const eng::Box b = screen.bounds();
	return screen.text_words(0, static_cast<s16>(b.bottom() - 7), line, color);
}

} // namespace eng::debug
