#pragma once

/// \file text.hpp
/// **Medida y recorte de texto** de UI (`eng::ui`). No añade fuente nueva: mide con `Font8`
/// (8×8, avance 8 px) y se apoya en `field::Surface` para pintar. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §6.

#include <eng/core/types/box.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/data/utf8.hpp>
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

/// Política de ajuste de texto (wrapping) para un ancho dado. Se usa en `text_wrap_lines` y en
/// el layout adaptable de etiquetas.
enum class WrapMode : eng::u8 {
	None,       ///< una sola línea; no se corta (puede exceder el ancho)
	Char,       ///< corta por carácter al llegar al ancho (útil para celdas monoespaciadas)
	Word,       ///< corta por palabra (no parte palabras; una palabra larga se parte por carácter)
};

/// Nº de líneas que ocupa `s` con `Font8` (avance 8 px) en un ancho `max_w` y la política `mode`.
/// Sin heap: recorre los code points contando el avance acumulado. `None` o ancho 0 ⇒ 1 línea.
[[nodiscard]] inline eng::u16 text_wrap_lines(const char* s, eng::u16 max_w,
					      WrapMode mode = WrapMode::Word) noexcept {
	if (s == nullptr || max_w == 0u || mode == WrapMode::None) {
		return 1u;
	}
	const eng::u16 cols = static_cast<eng::u16>(max_w / 8u); // glifos por línea
	if (cols == 0u) {
		return 1u;
	}
	const eng::u8* p = reinterpret_cast<const eng::u8*>(s);
	eng::u16 lines = 1u;
	eng::u16 line_cols = 0u;   // glifos usados en la línea actual
	eng::u16 word_cols = 0u;   // glifos de la palabra en curso (para `Word`)
	eng::u16 last_break = 0u;  // columna del último espacio (punto de corte por palabra)
	for (;;) {
		const eng::u32 cp = eng::utf8::decode(p);
		if (cp == 0u) {
			break;
		}
		if (cp == '\n') {
			++lines;
			line_cols = 0u;
			word_cols = 0u;
			continue;
		}
		if (cp == ' ' && mode == WrapMode::Word) {
			word_cols = 0u;
			last_break = line_cols;
		} else {
			++word_cols;
		}
		++line_cols;
		if (line_cols > cols) {
			// Desbordó: nueva línea. En `Word`, retrocede hasta el último espacio si lo hay.
			++lines;
			if (mode == WrapMode::Word && last_break > 0u) {
				line_cols = static_cast<eng::u16>(word_cols);
			} else {
				line_cols = 1u; // el carácter actual pasa a la línea nueva
			}
			last_break = 0u;
		}
	}
	return lines;
}

/// Ancho (px) de la **línea más larga** de `s` si se ajusta a `max_w` con `mode`. Para medir el
/// tamaño preferido de una etiqueta con wrapping (ancho efectivo = mínimo(ancho real, max_w)).
[[nodiscard]] inline eng::u16 text_wrapped_width(const char* s, eng::u16 max_w,
						 WrapMode mode = WrapMode::Word) noexcept {
	const eng::u16 full = text_width(s);
	if (mode == WrapMode::None || max_w == 0u || full <= max_w) {
		return full;
	}
	return max_w;
}

/// Dibuja `s` **ajustado** a `max_w` con `mode`, con salto de línea cada 8 px de alto. `Painter`
/// debe exponer `codepoint(x, y, cp, fg)`. Misma lógica de corte que `text_wrap_lines` (coherente
/// con `measure`). Devuelve el número de líneas dibujadas.
template <class Painter>
eng::u16 draw_text_wrapped(Painter& p, eng::s16 x, eng::s16 y, const char* s, eng::u8 fg,
			   eng::u16 max_w, WrapMode mode = WrapMode::Word) noexcept {
	if (s == nullptr) {
		return 0u;
	}
	if (mode == WrapMode::None || max_w == 0u) {
		const eng::u8* q = reinterpret_cast<const eng::u8*>(s);
		eng::s16 cx = x;
		for (;;) {
			const eng::u32 cp = eng::utf8::decode(q);
			if (cp == 0u) break;
			if (cp >= 32u) p.codepoint(cx, y, cp, fg);
			cx = static_cast<eng::s16>(cx + 8);
		}
		return 1u;
	}
	const eng::u16 cols = static_cast<eng::u16>(max_w / 8u);
	if (cols == 0u) {
		return 0u;
	}
	const eng::u8* q = reinterpret_cast<const eng::u8*>(s);
	// Materializa los code points en dos líneas de trabajo (la anterior y la actual) para poder
	// retroceder una palabra al hacer *wrap*. Capacidad fija.
	constexpr eng::u16 kMaxCols = 128u;
	eng::u32 line[kMaxCols];
	eng::u16 n = 0u;
	eng::s16 line_y = y;
	eng::u16 lines = 0u;
	auto flush = [&]() {
		for (eng::u16 i = 0u; i < n; ++i) {
			p.codepoint(static_cast<eng::s16>(x + static_cast<eng::s16>(i * 8u)), line_y,
				    line[i], fg);
		}
		++lines;
		n = 0u;
		line_y = static_cast<eng::s16>(line_y + 8);
	};
	for (;;) {
		const eng::u32 cp = eng::utf8::decode(q);
		if (cp == 0u) break;
		if (cp == '\n') { flush(); continue; }
		if (n >= cols) {
			// Busca el último espacio para cortar por palabra.
			eng::u16 cut = n;
			if (mode == WrapMode::Word) {
				for (eng::u16 i = n; i > 0u; --i) {
					if (line[i - 1u] == ' ') { cut = static_cast<eng::u16>(i - 1u); break; }
				}
			}
			if (cut == 0u) cut = n; // palabra larga: parte por carácter
			// Pinta hasta `cut` y desplaza el resto (incluida la palabra tras el corte).
			for (eng::u16 i = 0u; i < cut; ++i) {
				p.codepoint(static_cast<eng::s16>(x + static_cast<eng::s16>(i * 8u)), line_y,
					    line[i], fg);
			}
			++lines;
			line_y = static_cast<eng::s16>(line_y + 8);
			eng::u16 rest = static_cast<eng::u16>(n - cut);
			eng::u16 j = 0u;
			for (eng::u16 i = cut; i < n; ++i, ++j) line[j] = line[i];
			n = rest;
			if (n > 0u && line[0] == ' ') {
				for (eng::u16 i = 0u; i + 1u < n; ++i) line[i] = line[i + 1u];
				--n;
			}
		}
		if (n < kMaxCols) {
			line[n++] = cp;
		}
	}
	if (n > 0u) {
		flush();
	}
	return lines;
}

} // namespace eng::ui
