#pragma once

/// \file ui_bridge.hpp
/// **Puente `os::Msg` → `UiEvent`** (`eng::ui`): traduce los mensajes de entrada del mini-SO a
/// eventos de UI. La UI no lee hardware ni la cola del SO: solo recibe `UiEvent`. Ver
/// `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §8.

#include <eng/os/message.hpp>
#include <eng/ui/event.hpp>

namespace eng::ui {

/// Traduce un `os::Msg` de **entrada** a `UiEvent`. Devuelve `false` si el mensaje no es de entrada
/// (VBlank, Timer, FileDone, User, Quit…): el llamador lo atiende por otra vía.
[[nodiscard]] inline bool to_ui_event(const eng::os::Msg& m, UiEvent& out) noexcept {
	out = UiEvent {};
	switch (m.type) {
	case eng::os::MsgType::MouseMove:
		out.kind = UiEventKind::MouseMove;
		out.x = m.payload.mouse.x;
		out.y = m.payload.mouse.y;
		out.buttons = m.payload.mouse.buttons;
		return true;
	case eng::os::MsgType::MouseButton:
		out.kind = ((m.payload.mouse.buttons & 1u) != 0u) ? UiEventKind::MouseDown
								  : UiEventKind::MouseUp;
		out.x = m.payload.mouse.x;
		out.y = m.payload.mouse.y;
		out.buttons = m.payload.mouse.buttons;
		return true;
	case eng::os::MsgType::KeyDown:
		out.kind = UiEventKind::KeyDown;
		out.key = m.payload.key.code;
		out.shift = (m.payload.key.qual & eng::os::kQualShift) != 0u;
		out.ctrl = (m.payload.key.qual & eng::os::kQualCtrl) != 0u;
		out.alt = (m.payload.key.qual & eng::os::kQualAlt) != 0u;
		out.amiga = (m.payload.key.qual & eng::os::kQualAmiga) != 0u;
		return true;
	case eng::os::MsgType::KeyUp:
		out.kind = UiEventKind::KeyUp;
		out.key = m.payload.key.code;
		return true;
	case eng::os::MsgType::Joystick:
		out.kind = UiEventKind::JoyButton;
		out.joy_port = m.payload.joy.port;
		out.joy_buttons = static_cast<eng::u16>(
			static_cast<eng::u16>(m.payload.joy.dirs) |
			static_cast<eng::u16>(static_cast<eng::u16>(m.payload.joy.fire) << 8u));
		return true;
	case eng::os::MsgType::Gamepad:
		out.kind = UiEventKind::JoyButton;
		out.joy_port = m.payload.pad.port;
		out.joy_buttons = m.payload.pad.buttons;
		return true;
	default:
		return false;
	}
}

} // namespace eng::ui
