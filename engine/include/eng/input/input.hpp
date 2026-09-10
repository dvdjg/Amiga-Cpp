#pragma once

/// \file input.hpp
/// Entrada unificada y portable (paso 6 de ENGINE_DESIGN.md §5).
///
/// `InputAggregator` es el estado lógico de entrada POR FRAME, agnóstico del
/// backend: el backend (Amiga: CIA-A/B, potgo/joyport, teclado) lo alimenta cada
/// frame y la lógica de juego lo lee. Sustituye al uso suelto de `input_poll` con
/// una sola abstracción, como en Sevgi (joystick/CD32 en ambos puertos, ratón,
/// teclado) pero con el estilo freestanding del engine.
///
/// Es puro (solo `eng::core`): host-testable. El `PadState` modela el CD32
/// (direcciones + fire/fire2/play + botones de color), de modo que un joystick de
/// 1 botón y un pad CD32 se reducen a la misma estructura.

#include <eng/core/types.hpp>

namespace eng::input {

/// Estado de un pad/joystick (direcciones + botones CD32). `fire` es el botón
/// rojo (el fire clásico); `fire2` el azul. Los botones de color se exponen como
/// `yellow`/`green`, `play` para Play/Pause y `reverse`/`forward` para los
/// hombros del CD32.
struct PadState {
	bool up = false;
	bool down = false;
	bool left = false;
	bool right = false;
	bool fire = false;    // rojo (CD32_RED)
	bool fire2 = false;   // azul (CD32_BLUE)
	bool play = false;    // play/pause (CD32_PAUSE)
	bool yellow = false;  // CD32_YELLOW
	bool green = false;   // CD32_GREEN
	bool reverse = false; // CD32_REVERSE (hombro izquierdo)
	bool forward = false; // CD32_FORWARD (hombro derecho)

	/// ¿Alguna dirección pulsada?
	bool any_direction() const { return up || down || left || right; }
	/// ¿Algún botón pulsado?
	bool any_button() const { return fire || fire2 || play || yellow || green || reverse || forward; }
	/// ¿Alguna entrada del pad?
	bool any() const { return any_direction() || any_button(); }
};

/// Estado del ratón (delta por frame + botones).
struct MouseState {
	s16 dx = 0;
	s16 dy = 0;
	bool left_button = false;
	bool right_button = false;
};

/// Estado del teclado (por scancode Amiga). `pending` es un make pendiente
/// (0 = ninguno). El escaneo de matriz completo (64 teclas) puede añadirse luego
/// como `scanned[8]`; aquí se mantiene mínimo.
struct KeyState {
	u8 pending = 0;
};

/// Agregador de entrada del frame. El backend lo rellena; la lógica lo lee.
struct InputAggregator {
	PadState pad0 {};
	PadState pad1 {};
	MouseState mouse {};
	KeyState keys {};

	/// Cualquier entrada de cualquier dispositivo.
	bool any() const { return pad0.any() || pad1.any() || mouse.left_button || mouse.right_button || keys.pending != 0u; }
};

} // namespace eng::input
