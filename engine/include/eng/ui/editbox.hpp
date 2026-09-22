#pragma once

/// \file editbox.hpp
/// **`eng::ui::EditBox`** (G4): campo de texto sobre un **buffer externo** (`buf`/`cap`/`len`/
/// `caret`), **sin heap**, en **UTF-8** (un carácter = 1-2 bytes; `len`/`caret`/`view` son
/// **desplazamientos de byte** y la edición avanza por **code point**). Inserta/borra, mueve el
/// caret y desplaza la **vista horizontal** (`view`). Ver `docs/engine/architecture/GUI_LIBRARY.md`
/// §11.

#include <eng/core/types.hpp>
#include <eng/core/utf8.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/keys.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

struct EditBox : Widget {
	/// Campo de texto vacío (el llamador enlaza `buf`/`cap`); acepta foco.
	EditBox() noexcept {
		type = WidgetType::Edit;
		set_flag(WfAcceptsFocus);
	}

	char* buf = nullptr; ///< buffer externo (`cap` chars; se mantiene NUL en `len`)
	eng::u16 cap = 0u;   ///< capacidad en chars (se reserva 1 para el NUL)
	eng::u16 len = 0u;   ///< longitud actual
	eng::u16 caret = 0u; ///< posición del caret (0..len)
	eng::u16 view = 0u;  ///< primer caracter visible (scroll horizontal)
	void (*on_change)(void* user) = nullptr;
	void* user = nullptr;

	/// Columnas de texto visibles (mínimo 1).
	[[nodiscard]] eng::u16 cols() const noexcept {
		const eng::u16 usable =
			static_cast<eng::u16>(bounds.w > 8u ? bounds.w - 8u : 8u);
		const eng::u16 c = static_cast<eng::u16>(usable / 8u);
		return c != 0u ? c : 1u;
	}

	/// Byte de inicio del code point que contiene `pos` (retrocede sobre bytes de continuación).
	[[nodiscard]] eng::u16 cp_start(eng::u16 pos) const noexcept {
		while (pos > 0u && buf != nullptr &&
		       (static_cast<eng::u8>(buf[pos - 1u]) & 0xc0u) == 0x80u) {
			--pos;
		}
		return pos;
	}

	/// Byte de inicio del code point ANTERIOR a `pos` (el que termina en `pos`). `0` si `pos` = 0.
	[[nodiscard]] eng::u16 prev_cp_start(eng::u16 pos) const noexcept {
		if (pos == 0u || buf == nullptr) {
			return 0u;
		}
		eng::u16 p = static_cast<eng::u16>(pos - 1u);
		while (p > 0u && (static_cast<eng::u8>(buf[p]) & 0xc0u) == 0x80u) {
			--p;
		}
		return p;
	}

	/// Longitud en bytes del code point que empieza en `pos` (1 o 2; 0 fuera de rango).
	[[nodiscard]] eng::u16 cp_len(eng::u16 pos) const noexcept {
		if (buf == nullptr || pos >= len) {
			return 0u;
		}
		const eng::u8 b = static_cast<eng::u8>(buf[pos]);
		if ((b & 0x80u) == 0u) {
			return 1u;
		}
		if ((b & 0xe0u) == 0xc0u) {
			return 2u;
		}
		return 1u; // byte suelto (defensivo)
	}

	/// Inserta el code point `cp` (codificado en UTF-8) en el caret (si cabe). `true` si cambió.
	bool insert_cp(eng::u32 cp) noexcept {
		if (buf == nullptr) {
			return false;
		}
		eng::u8 enc[4];
		const eng::u8 n = eng::utf8::encode(cp, enc);
		if (n == 0u || static_cast<eng::u32>(len) + n >= cap) {
			return false;
		}
		for (eng::u16 i = len; i > caret; --i) {
			buf[static_cast<eng::u16>(i + n - 1u)] = buf[i - 1u];
		}
		for (eng::u8 k = 0u; k < n; ++k) {
			buf[static_cast<eng::u16>(caret + k)] = static_cast<char>(enc[k]);
		}
		caret = static_cast<eng::u16>(caret + n);
		len = static_cast<eng::u16>(len + n);
		buf[len] = '\0';
		ensure_caret_visible();
		return true;
	}

	/// Inserta el carácter `c` (ASCII/Latin-1; se codifica en UTF-8). `true` si cambió.
	bool insert(char c) noexcept { return insert_cp(static_cast<eng::u8>(c)); }

	/// Borra el code point a la izquierda del caret. `true` si cambió.
	bool backspace() noexcept {
		if (buf == nullptr || caret == 0u) {
			return false;
		}
		const eng::u16 start = prev_cp_start(caret);
		const eng::u16 n = static_cast<eng::u16>(caret - start);
		for (eng::u16 i = start; i + n < len; ++i) {
			buf[i] = buf[static_cast<eng::u16>(i + n)];
		}
		caret = start;
		len = static_cast<eng::u16>(len - n);
		buf[len] = '\0';
		ensure_caret_visible();
		return true;
	}

	/// Borra el code point bajo el caret. `true` si cambió.
	bool del() noexcept {
		if (buf == nullptr || caret >= len) {
			return false;
		}
		const eng::u16 n = cp_len(caret);
		if (n == 0u) {
			return false;
		}
		for (eng::u16 i = caret; i + n < len; ++i) {
			buf[i] = buf[static_cast<eng::u16>(i + n)];
		}
		len = static_cast<eng::u16>(len - n);
		buf[len] = '\0';
		ensure_caret_visible();
		return true;
	}

	/// Mueve el caret al code point anterior.
	void move_left() noexcept {
		if (caret > 0u) {
			caret = prev_cp_start(caret);
			ensure_caret_visible();
		}
	}

	/// Mueve el caret al code point siguiente.
	void move_right() noexcept {
		if (caret < len) {
			caret = static_cast<eng::u16>(caret + cp_len(caret));
			ensure_caret_visible();
		}
	}

	/// Ajusta `view` para que el caret quede dentro de las columnas visibles (alineado a code point).
	void ensure_caret_visible() noexcept {
		const eng::u16 c = cols();
		if (caret < view) {
			view = caret;
		} else if (caret >= static_cast<eng::u16>(view + c)) {
			view = static_cast<eng::u16>(caret - c + 1u);
		}
		view = cp_start(view);
	}
};

/// Dibuja el campo: caja hundida + texto visible + caret si tiene el foco.
inline void draw_edit(EditBox& e, UiPainter& p) {
	if (!e.has(WfVisible)) {
		return;
	}
	p.fill(e.bounds, p.theme().edit_bg);
	p.bevel_in(e.bounds);
	if (e.buf == nullptr) {
		return;
	}
	const eng::s16 x0 = static_cast<eng::s16>(e.bounds.x + 4);
	const eng::s16 ty = static_cast<eng::s16>(
		e.bounds.y + (e.bounds.h > 8u ? (e.bounds.h - 8u) / 2u : 0u));
	// Dibuja los code points visibles decodificando UTF-8 desde `view`.
	const eng::u16 c = e.cols();
	const eng::u8* const base = reinterpret_cast<const eng::u8*>(e.buf);
	const eng::u8* q = base + e.view;
	for (eng::u16 i = 0u; i < c; ++i) {
		const eng::u8* save = q;
		const eng::u32 cp = eng::utf8::decode(q);
		if (cp == 0u) {
			q = save; // fin o byte inválido
			break;
		}
		p.codepoint(static_cast<eng::s16>(x0 + i * 8u), ty, cp, p.theme().text);
		if (q >= base + e.len) {
			break;
		}
	}
	// Columna del caret = code points entre `view` y `caret`.
	if (e.has(WfFocused) && e.caret >= e.view) {
		eng::u16 col = 0u;
		const eng::u8* r = base + e.view;
		while (r < base + e.caret) {
			const eng::u8* save = r;
			if (eng::utf8::decode(r) == 0u) {
				r = save;
				break;
			}
			++col;
		}
		if (col <= c) {
			const eng::s16 cx = static_cast<eng::s16>(x0 + col * 8u);
			p.vline(cx, static_cast<eng::s16>(e.bounds.y + 2),
				static_cast<eng::s16>(e.bounds.bottom() - 2), p.theme().focus_ring);
		}
	}
}

/// Evento del campo: click coloca el caret; con foco, edita con las teclas lógicas.
inline bool event_edit(EditBox& e, const UiEvent& ev) {
	if (!e.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && e.bounds.contains(ev.x, ev.y)) {
		e.set_flag(WfPressed);
		e.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && e.has(WfPressed)) {
		if (e.bounds.contains(ev.x, ev.y)) {
			const eng::s16 rel = static_cast<eng::s16>(ev.x - e.bounds.x - 4);
			eng::s16 pos = static_cast<eng::s16>(e.view) + static_cast<eng::s16>(rel / 8);
			if (pos < 0) {
				pos = 0;
			}
			if (static_cast<eng::u32>(pos) > e.len) {
				pos = static_cast<eng::s16>(e.len);
			}
			e.caret = e.cp_start(static_cast<eng::u16>(pos)); // alinea a code point
			e.ensure_caret_visible();
		}
		e.clear_flag(WfPressed);
		e.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::KeyDown && e.has(WfFocused)) {
		bool changed = false;
		bool consumed = true;
		switch (ev.key) {
		case kKeyBackspace:
			changed = e.backspace();
			break;
		case kKeyDelete:
			changed = e.del();
			break;
		case kKeyLeft:
			e.move_left();
			break;
		case kKeyRight:
			e.move_right();
			break;
		case kKeyHome:
			e.caret = 0u;
			e.ensure_caret_visible();
			break;
		case kKeyEnd:
			e.caret = e.len;
			e.ensure_caret_visible();
			break;
		default:
			if (is_printable_key(ev.key)) {
				changed = e.insert_cp(ev.key);
			} else {
				consumed = false;
			}
			break;
		}
		if (changed) {
			e.mark_dirty();
			if (e.on_change != nullptr) {
				e.on_change(e.user);
			}
		}
		return consumed;
	}
	return false;
}

} // namespace eng::ui
