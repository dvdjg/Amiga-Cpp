#pragma once

/// \file editbox.hpp
/// **`eng::ui::EditBox`** (G4): campo de texto sobre un **buffer externo** (`buf`/`cap`/`len`/
/// `caret`), **sin heap**. Inserta/borra, mueve el caret y desplaza la **vista horizontal**
/// (`view`). Ver `docs/engine/architecture/GUI_LIBRARY.md` §11.

#include <eng/core/types.hpp>
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

	/// Inserta `c` en el caret (si cabe). `true` si cambió.
	bool insert(char c) noexcept {
		if (buf == nullptr || static_cast<eng::u32>(len) + 1u >= cap) {
			return false;
		}
		for (eng::u16 i = len; i > caret; --i) {
			buf[i] = buf[i - 1u];
		}
		buf[caret] = c;
		++caret;
		++len;
		buf[len] = '\0';
		ensure_caret_visible();
		return true;
	}

	/// Borra el caracter a la izquierda del caret. `true` si cambió.
	bool backspace() noexcept {
		if (buf == nullptr || caret == 0u) {
			return false;
		}
		for (eng::u16 i = caret - 1u; i + 1u < len; ++i) {
			buf[i] = buf[i + 1u];
		}
		--caret;
		--len;
		buf[len] = '\0';
		ensure_caret_visible();
		return true;
	}

	/// Borra el caracter bajo el caret. `true` si cambió.
	bool del() noexcept {
		if (buf == nullptr || caret >= len) {
			return false;
		}
		for (eng::u16 i = caret; i + 1u < len; ++i) {
			buf[i] = buf[i + 1u];
		}
		--len;
		buf[len] = '\0';
		ensure_caret_visible();
		return true;
	}

	/// Ajusta `view` para que el caret quede dentro de las columnas visibles.
	void ensure_caret_visible() noexcept {
		const eng::u16 c = cols();
		if (caret < view) {
			view = caret;
		} else if (caret >= static_cast<eng::u16>(view + c)) {
			view = static_cast<eng::u16>(caret - c + 1u);
		}
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
	const eng::u16 c = e.cols();
	for (eng::u16 i = 0u; i < c && static_cast<eng::u32>(e.view + i) < e.len; ++i) {
		const eng::u32 cp = static_cast<eng::u8>(e.buf[e.view + i]);
		p.codepoint(static_cast<eng::s16>(x0 + i * 8u), ty, cp, p.theme().text);
	}
	if (e.has(WfFocused) && e.caret >= e.view &&
	    static_cast<eng::u32>(e.caret - e.view) <= c) {
		const eng::s16 cx = static_cast<eng::s16>(x0 + (e.caret - e.view) * 8u);
		p.vline(cx, static_cast<eng::s16>(e.bounds.y + 2),
			static_cast<eng::s16>(e.bounds.bottom() - 2), p.theme().focus_ring);
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
			e.caret = static_cast<eng::u16>(pos);
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
			if (e.caret > 0u) {
				--e.caret;
				e.ensure_caret_visible();
			}
			break;
		case kKeyRight:
			if (e.caret < e.len) {
				++e.caret;
				e.ensure_caret_visible();
			}
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
				changed = e.insert(static_cast<char>(ev.key));
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
