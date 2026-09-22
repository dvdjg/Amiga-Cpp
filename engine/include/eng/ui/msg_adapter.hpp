#pragma once

/// \file msg_adapter.hpp
/// **Adaptador `os::Msg` → `UiContext`** (`eng::ui`): la GUI consume la entrada **por mensajes**
/// del mini-SO (no sondeo). Traduce cada `Msg` de entrada con `ui_bridge` (`to_ui_event`), mapea
/// el **rawkey** a tecla lógica (`keymap.hpp`) y lo despacha. Ver
/// `docs/engine/architecture/MINI_OS_INPUT.md` y `GUI_LIBRARY.md` §15.

#include <eng/os/message.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/keymap.hpp>
#include <eng/ui/ui_bridge.hpp>

namespace eng::ui {

/// Despacha un `os::Msg` de entrada al `UiContext` (traduce rawkey→tecla lógica según `layout`).
/// `true` si el contexto lo consumió; `false` si el mensaje no es de entrada o nadie lo atendió.
inline bool dispatch_msg(UiContext& ctx, const eng::os::Msg& m,
			 KeyboardLayout layout = KeyboardLayout::Us) {
	UiEvent e {};
	if (!to_ui_event(m, e)) {
		return false;
	}
	if (e.kind == UiEventKind::KeyDown || e.kind == UiEventKind::KeyUp) {
		e.key = rawkey_to_key(static_cast<eng::u8>(e.key & 0xffu), e.shift, layout);
	}
	return ctx.dispatch(e);
}

} // namespace eng::ui
