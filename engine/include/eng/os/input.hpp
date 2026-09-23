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

/// Botones del **pad CD32** (bitmask estable para la app, independiente del orden del stream).
enum Cd32Btn : eng::u16 {
	Cd32Blue = 1u << 0,    ///< botón azul (acción primaria)
	Cd32Red = 1u << 1,     ///< botón rojo
	Cd32Yellow = 1u << 2,  ///< botón amarillo
	Cd32Green = 1u << 3,   ///< botón verde
	Cd32Forward = 1u << 4, ///< hombro derecho
	Cd32Reverse = 1u << 5, ///< hombro izquierdo
	Cd32Play = 1u << 6,    ///< Play/Pause
};

/// Decodifica los **8 bits serie** del pad CD32 al bitmask estable. El pad envía por la línea de
/// pot un registro de desplazamiento (74LS165) **activo a 0**: `bit i` de `bits` es el nivel leído
/// en el i-ésimo pulso de reloj (1 = no pulsado, 0 = pulsado). Orden del stream (calibrado contra
/// WinUAE, `inputdevice.cpp:4050-4053`): primero **Blue**, luego Red, Yellow, Green, Forward,
/// Reverse, Play; el 8.º bit es la firma (1 = pad CD32). Ver `MINI_OS_INPUT.md` §6.
[[nodiscard]] constexpr eng::u16 cd32_mask_from_shift(eng::u8 bits) noexcept {
	eng::u16 m = 0u;
	if ((bits & 0x01u) == 0u) m = static_cast<eng::u16>(m | Cd32Blue);
	if ((bits & 0x02u) == 0u) m = static_cast<eng::u16>(m | Cd32Red);
	if ((bits & 0x04u) == 0u) m = static_cast<eng::u16>(m | Cd32Yellow);
	if ((bits & 0x08u) == 0u) m = static_cast<eng::u16>(m | Cd32Green);
	if ((bits & 0x10u) == 0u) m = static_cast<eng::u16>(m | Cd32Forward);
	if ((bits & 0x20u) == 0u) m = static_cast<eng::u16>(m | Cd32Reverse);
	if ((bits & 0x40u) == 0u) m = static_cast<eng::u16>(m | Cd32Play);
	return m;
}

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

/// **Productor de teclado**: recibe el byte crudo de la CIA-A (`SDR`), corrige el **bit-reverse**
/// del scancode, mantiene los modificadores y emite `KeyDown`/`KeyUp`. Cada scancode es un evento
/// (siempre emite).
struct KeyProducer {
	eng::u16 qual = 0u; ///< bits `kQual*`

	/// Procesa un byte crudo de `SDR` (`bit7` = 1 → suelta). Devuelve `true` y rellena `out`.
	bool update(eng::u8 raw, eng::u32 stamp, Msg& out) noexcept {
		const bool up = (raw & 0x80u) != 0u;
		const eng::u8 code = reverse_bits7(static_cast<eng::u8>(raw & 0x7fu));
		update_qual(code, up);
		out = Msg {};
		out.type = up ? MsgType::KeyUp : MsgType::KeyDown;
		out.time_stamp = stamp;
		out.payload.key = {code, qual};
		return true;
	}

	/// Invierte los 7 bits de un scancode de la CIA (el teclado los manda al revés).
	[[nodiscard]] static constexpr eng::u8 reverse_bits7(eng::u8 v) noexcept {
		eng::u8 r = 0u;
		for (eng::u8 i = 0u; i < 7u; ++i) {
			r = static_cast<eng::u8>((r << 1u) | (v & 1u));
			v = static_cast<eng::u8>(v >> 1u);
		}
		return r;
	}

private:
	/// Pone o quita un bit de modificador.
	void set_qual(eng::u16 mask, bool on) noexcept {
		qual = on ? static_cast<eng::u16>(qual | mask) : static_cast<eng::u16>(qual & ~mask);
	}
	/// Actualiza los modificadores segun el scancode (Shift/Ctrl/Alt/Amiga) y down/up.
	void update_qual(eng::u8 code, bool up) noexcept {
		switch (code) {
		case 0x60u: // LShift
		case 0x61u: // RShift
			set_qual(kQualShift, !up);
			break;
		case 0x63u: // Ctrl
			set_qual(kQualCtrl, !up);
			break;
		case 0x64u: // LAlt
		case 0x65u: // RAlt
			set_qual(kQualAlt, !up);
			break;
		case 0x66u: // LAmiga
		case 0x67u: // RAmiga
			set_qual(kQualAmiga, !up);
			break;
		default:
			break;
		}
	}
};

} // namespace eng::os
