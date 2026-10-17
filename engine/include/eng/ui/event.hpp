#pragma once

/// \file event.hpp
/// **Evento de UI** (`eng::ui`): la UI **no lee hardware**: consume `UiEvent` que fabrica el puente
/// `os::Msg` → `UiEvent` (`ui_bridge.hpp`). Ver `docs/engine/architecture/GUI_LIBRARY.md` y
/// `MINI_OS_MESSAGE_LOOP.md` §8.

#include <eng/core/types/types.hpp>

namespace eng::ui {

/// Tipo de evento de UI.
enum class UiEventKind : eng::u8 {
	None = 0,
	MouseMove,
	MouseDown,
	MouseUp,
	KeyDown,
	KeyUp,
	JoyButton,     ///< joystick/gamepad (botón o dirección)
	Tick,          ///< VBlank: caret, toasts, animación
	PointerCancel, ///< cancelación de puntero (pérdida de foco global, cierre de ventana, cambio de escena)
	Action,        ///< acción semántica (`UiAction`), ya traducida del rawkey por la capa de entrada
};

/// **Acción semántica** de UI (G9): el widget no depende del *scancode* ni del layout nacional.
/// La capa de entrada traduce rawkey → `UiAction`; los widgets consumen la acción y solo miran
/// `KeyDown`/`KeyUp` cuando necesitan repetición o combinaciones.
enum class UiAction : eng::u8 {
	None = 0,
	Accept,        ///< confirmar (Return/Enter)
	Cancel,        ///< cancelar/cerrar (Esc)
	NextFocus,     ///< siguiente foco (Tab)
	PreviousFocus, ///< foco anterior (Shift+Tab)
	MoveLeft,      ///< navegación izquierda
	MoveRight,     ///< navegación derecha
	MoveUp,        ///< navegación arriba
	MoveDown,      ///< navegación abajo
	PageUp,        ///< página anterior
	PageDown,      ///< página siguiente
	Home,          ///< inicio
	End,           ///< fin
	Delete,        ///< borrar hacia delante
	Copy,          ///< copiar
	Cut,           ///< cortar
	Paste,         ///< pegar
	SelectAll,     ///< seleccionar todo
	Activate,      ///< activar el elemento con foco (click del foco)
	ContextMenu,   ///< menú contextual
};

/// Evento de UI. Genérico para ratón, teclado, joystick y tick.
struct UiEvent {
	UiEventKind kind = UiEventKind::None;
	eng::s16 x = 0; ///< posición del ratón (coordenadas de pantalla)
	eng::s16 y = 0;
	eng::u8 buttons = 0;  ///< bitmask de botones del ratón
	eng::u16 key = 0;     ///< scancode Amiga
	UiAction action = UiAction::None; ///< acción semántica (solo con `UiEventKind::Action`)
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	bool amiga = false;
	eng::u8 joy_port = 0;    ///< puerto del joystick/gamepad
	eng::u16 joy_buttons = 0;///< bitmask de botones/direcciones del pad
};

} // namespace eng::ui
