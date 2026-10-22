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
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
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
	eng::util::StaticString<64> line;
	if (name != nullptr && *name != '\0') {
		(void)line.append(eng::util::StringView(name));
		(void)line.append(' ');
	}
	char label[24] {};
	const u8 vlen = version_label(version, label, sizeof(label));
	(void)line.append(eng::util::StringView(label, vlen));
	(void)line.append(' ');
	(void)line.append('F');
	(void)line.append(' ');
	(void)eng::util::to_chars(line, frame);
	const eng::Box b = screen.bounds();
	return screen.text_words(0, static_cast<s16>(b.bottom() - 7), line.c_str(), color);
}

} // namespace eng::debug
