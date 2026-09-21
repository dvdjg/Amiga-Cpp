#pragma once

/// \file event.hpp
/// **Evento de UI** (`eng::ui`): la UI **no lee hardware**: consume `UiEvent` que fabrica el puente
/// `os::Msg` → `UiEvent` (`ui_bridge.hpp`). Ver `docs/engine/architecture/GUI_LIBRARY.md` y
/// `MINI_OS_MESSAGE_LOOP.md` §8.

#include <eng/core/types.hpp>

namespace eng::ui {

/// Tipo de evento de UI.
enum class UiEventKind : eng::u8 {
	None = 0,
	MouseMove,
	MouseDown,
	MouseUp,
	KeyDown,
	KeyUp,
	JoyButton, ///< joystick/gamepad (botón o dirección)
	Tick,      ///< VBlank: caret, toasts, animación
};

/// Evento de UI. Genérico para ratón, teclado, joystick y tick.
struct UiEvent {
	UiEventKind kind = UiEventKind::None;
	eng::s16 x = 0; ///< posición del ratón (coordenadas de pantalla)
	eng::s16 y = 0;
	eng::u8 buttons = 0;  ///< bitmask de botones del ratón
	eng::u16 key = 0;     ///< scancode Amiga
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	bool amiga = false;
	eng::u8 joy_port = 0;    ///< puerto del joystick/gamepad
	eng::u16 joy_buttons = 0;///< bitmask de botones/direcciones del pad
};

} // namespace eng::ui
