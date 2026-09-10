#pragma once

/// \file input_poll.hpp
/// Entrada por LECTURA DIRECTA DEL HARDWARE, sin kernel (el engine es
/// freestanding: no abre Intuition ni input.device, no hay runtime del sistema).
///
/// Direcciones del joystick (AHRM cap. 8, "Reading Digital Joystick
/// Controllers"): los cuatro conmutadores de dirección NO llegan a los CIA sino
/// a los contadores de Denise, `JOY0DAT` ($DFF00A, puerto 0) y `JOY1DAT`
/// ($DFF00C, puerto 1). Su decodificación es (1 = pulsado):
///   - derecha : bit 1 (X1)
///   - izquierda: bit 9 (Y1)
///   - arriba  : bit 9 XOR bit 8  (Y1 xor Y0)
///   - abajo   : bit 1 XOR bit 0  (X1 xor X0)
///
/// Fuego (AHRM Ap. E, "Game Port Interface to Fire Buttons"): el botón de fuego
/// de cada puerto llega a la PRA del CIA-A ($BFE001), con 0 = pulsado:
///   - puerto 0 (ratón/joystick 0): bit 6
///   - puerto 1 (joystick 1):       bit 7
///
/// Los botones extra del CD32 (fire2/play/yellow/green) usan el protocolo serie
/// POTGO/POTINP y quedan FUERA de este archivo (pendiente; aquí se dejan a 0).
///
/// `poll_input()` se llama UNA vez por frame (en update/VBL). Con el edge del
/// resto de calls puede alimentarse `g_tech_new` (selector de técnicas 1..9) o
/// rellenarse un `eng::input::InputAggregator`.

#include <eng/core/types.hpp>
#include <eng/input/input.hpp>

namespace eng::amiga {

/// Bits de dirección/fuego decodificados (1 = pulsado).
enum JoyBits : eng::u8 {
	kJoyUp = 1u << 0,
	kJoyDown = 1u << 1,
	kJoyLeft = 1u << 2,
	kJoyRight = 1u << 3,
	kJoyFire = 1u << 4,
};

/// Decodifica un registro `JOYxDAT` (16 bits) en bits de dirección.
///
/// Es pura (host-testable): la combinación de bits del contador de Denise es lo
/// único Amiga-específico. Cada eje es un código de GRAY de 2 bits (AHRM cap. 8
/// y Ap. A; mismo mapeo que los motores ACE y Sevgi):
///   eje Y (bits 8-9): 00=reposo, 01=arriba, 11=izquierda, 10=arriba+izquierda
///   eje X (bits 0-1): 00=reposo, 01=abajo,  11=derecha,  10=abajo+derecha
/// por eso arriba/abajo necesitan XOR (conversión Gray→binario) y derecha/
/// izquierda se leen directas del bit "alto" de cada eje.
constexpr eng::u8 decode_joystick(eng::u16 joydat) {
	eng::u8 r = 0u;
	if ((joydat & 0x0002u) != 0u) r |= kJoyRight;              // X1 (bit 1)
	if ((joydat & 0x0200u) != 0u) r |= kJoyLeft;               // Y1 (bit 9)
	if (((joydat >> 9u) ^ (joydat >> 8u)) & 1u) r |= kJoyUp;   // Y1 xor Y0
	if (((joydat >> 1u) ^ joydat) & 1u) r |= kJoyDown;         // X1 xor X0
	return r;
}

/// Estado de los puertos de juego del frame (hardware directo, decodificado).
struct GameInput {
	eng::u8 port0 = 0; // bits 0..4 := up/down/left/right/fire (1 = pulsado)
	eng::u8 port1 = 0; // ídem para el puerto 1
	/// ¿Alguna dirección/fire está pulsado este frame?
	bool any() const { return (port0 | port1) != 0u; }
};

/// Lee los puertos 0/1 directamente del hardware (JOYxDAT + CIAAPRA).
inline void poll_input(GameInput& out) {
	const eng::u16 joy0 = *reinterpret_cast<volatile eng::u16*>(0xDFF00Au);
	const eng::u16 joy1 = *reinterpret_cast<volatile eng::u16*>(0xDFF00Cu);
	const eng::u8  pa   = *reinterpret_cast<volatile eng::u8*>(0xBFE001u);

	out.port0 = decode_joystick(joy0);
	out.port1 = decode_joystick(joy1);
	// Fuego: CIAAPRA bit 6 (puerto 0) y bit 7 (puerto 1), 0 = pulsado.
	if ((pa & 0x40u) == 0u) out.port0 |= kJoyFire;
	if ((pa & 0x80u) == 0u) out.port1 |= kJoyFire;
}

/// Rellena un `eng::input::InputAggregator` desde el hardware.
///
/// Solo mapea las direcciones + fuego (pad0/pad1). El ratón (deltas por
/// contadores) y los botones CD32 extra quedan pendientes; el teclado se
/// rellena aparte con `poll_keyboard`.
inline void poll_input(eng::input::InputAggregator& agg) {
	GameInput g;
	poll_input(g);

	agg.pad0.up    = (g.port0 & kJoyUp) != 0u;
	agg.pad0.down  = (g.port0 & kJoyDown) != 0u;
	agg.pad0.left  = (g.port0 & kJoyLeft) != 0u;
	agg.pad0.right = (g.port0 & kJoyRight) != 0u;
	agg.pad0.fire  = (g.port0 & kJoyFire) != 0u;

	agg.pad1.up    = (g.port1 & kJoyUp) != 0u;
	agg.pad1.down  = (g.port1 & kJoyDown) != 0u;
	agg.pad1.left  = (g.port1 & kJoyLeft) != 0u;
	agg.pad1.right = (g.port1 & kJoyRight) != 0u;
	agg.pad1.fire  = (g.port1 & kJoyFire) != 0u;
}

// ---------------------------------------------------------------------------
// TECLADO por automatización de memoria (recomendado frente al port serie de
// CIAA: leer SDR/ICR con la IRQ de teclado del KS desarmada provoca una
// excepción (bus/address error → bucle de boot del KS) en WinUAE-DBG al llegar
// el primer byte; queda documentado como incidencia abierta).
//
// El HOST inyecta el scancode Amiga por memoria (`poke` del monitor sobre la
// variable global `g_automation_keycode`), y la demo la lee cada frame. El
// edge (cambio de valor + no-idle) produce un make. Scancodes: '1'=0x02,
// '2'=0x03, ..., '9'=0x0a, '0'=0x0b; 0xff = sin tecla.
// Enlace C (símbolo sin mangle) para que el runner la resuelva en el .map.
extern "C" volatile eng::u8 g_automation_keycode;

/// Estado del teclado sintético (edge por memoria).
struct KeyboardState {
	eng::u8 prev = 0xffu;   // último valor observado (arranca "sin tecla")
	eng::u8 pending = 0u;   // make pendiente (0 = ninguno)
};

/// Lee la tecla sintética inyectada por el host (edge de valor).
inline void poll_keyboard(KeyboardState& st) {
	const eng::u8 cur = g_automation_keycode;
	st.pending = 0u;
	if (cur != st.prev && cur != 0xffu) {
		st.pending = cur; // make: valor nuevo y distinto de "sin tecla"
	}
	st.prev = cur;
}

} // namespace eng::amiga
