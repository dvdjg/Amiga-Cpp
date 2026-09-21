#pragma once

/// \file input.hpp
/// **Productores de entrada del mini-SO** (`eng::os`): convierten el estado ya leído del hardware en
/// `Msg` de entrada, **solo cuando cambia** (flancos / estado), sin sondear en el bucle. La lectura
/// de registros (CIA/`JOYxDAT`/`POTGO`) la hace el backend; aquí está la parte **pura** y
/// host-testable. Ver `docs/engine/architecture/MINI_OS_INPUT.md`.
///
/// Cada productor guarda su estado previo y, en `update`, devuelve `true` y rellena el `Msg` si hay
/// novedad (movimiento, cambio de botones o de dirección). Así un registro que no cambia **no**
/// genera mensaje.

#include <eng/core/types.hpp>
#include <eng/os/message.hpp>

namespace eng::os {

/// **Productor de joystick** (digital, 1 botón). Emite `Joystick` solo si cambia dirs/fuego.
struct JoyProducer {
	eng::u8 port = 2u; ///< puerto (2 = izquierdo, el habitual del joystick)
	eng::u8 dirs = 0u;
	eng::u8 fire = 0u;
	bool inited = false;

	/// Actualiza con el estado del joystick; `true` si cambió (y rellena `out`).
	bool update(eng::u8 new_dirs, eng::u8 new_fire, eng::u32 stamp, Msg& out) noexcept {
		if (inited && new_dirs == dirs && new_fire == fire) {
			return false;
		}
		inited = true;
		dirs = new_dirs;
		fire = new_fire;
		out = Msg {};
		out.type = MsgType::Joystick;
		out.time_stamp = stamp;
		out.payload.joy = {port, new_dirs, new_fire};
		return true;
	}
};

/// **Productor de gamepad** (CD32 / multi-botón). Emite `Gamepad` solo si cambia el bitmask.
struct PadProducer {
	eng::u8 port = 2u;
	eng::u16 buttons = 0u;
	bool inited = false;

	/// Actualiza con el bitmask del pad; `true` si cambió (y rellena `out`).
	bool update(eng::u16 new_buttons, eng::u32 stamp, Msg& out) noexcept {
		if (inited && new_buttons == buttons) {
			return false;
		}
		inited = true;
		buttons = new_buttons;
		out = Msg {};
		out.type = MsgType::Gamepad;
		out.time_stamp = stamp;
		out.payload.pad = {port, new_buttons};
		return true;
	}
};

/// **Productor de ratón** (relativo): compara los contadores de 8 bits, mantiene la posición
/// absoluta (clampada) y emite `MouseMove` (si se movió) o `MouseButton` (si cambiaron los botones).
struct MouseProducer {
	eng::s16 x = 160;
	eng::s16 y = 128;
	eng::s16 max_x = 319;
	eng::s16 max_y = 255;
	eng::u8 buttons = 0u;
	eng::u8 prev_x = 0u;
	eng::u8 prev_y = 0u;
	bool inited = false;

	/// Actualiza con los contadores de 8 bits y los botones; `true` si emite (movimiento o botón).
	bool update(eng::u8 raw_x, eng::u8 raw_y, eng::u8 new_buttons, eng::u32 stamp,
		    Msg& out) noexcept {
		const eng::s8 dx = inited ? static_cast<eng::s8>(raw_x - prev_x) : 0;
		const eng::s8 dy = inited ? static_cast<eng::s8>(raw_y - prev_y) : 0;
		prev_x = raw_x;
		prev_y = raw_y;
		inited = true;

		if (dx != 0 || dy != 0) {
			x = clamp(x + dx, max_x);
			y = clamp(y + dy, max_y);
			out = Msg {};
			out.type = MsgType::MouseMove;
			out.time_stamp = stamp;
			out.payload.mouse = {x, y, dx, dy, buttons};
			return true;
		}
		if (new_buttons != buttons) {
			buttons = new_buttons;
			out = Msg {};
			out.type = MsgType::MouseButton;
			out.time_stamp = stamp;
			out.payload.mouse = {x, y, 0, 0, new_buttons};
			return true;
		}
		return false;
	}

private:
	/// Satura `v` al rango `[0, hi]`.
	[[nodiscard]] static eng::s16 clamp(eng::s32 v, eng::s16 hi) noexcept {
		if (v < 0) {
			return 0;
		}
		return (v > hi) ? hi : static_cast<eng::s16>(v);
	}
};

} // namespace eng::os
