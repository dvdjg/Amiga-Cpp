#pragma once

/// \file input_poll.hpp
/// Entrada por LECTURA DIRECTA DEL HARDWARE (CIA), sin kernel (el engine es
/// freestanding: no abre Intuition ni input.device, no hay runtime del sistema).
///
/// Puertos de juego (AHRM): las direcciones/fire de cada joystick/pad llegan a
/// bits 3..9 de la PRA del CIA correspondiente (1 = no pulsado, 0 = pulsado).
///   - Joystick 0 / ratón botón derecho:  CIA-A PRA %BFE001, bits 3..9.
///   - Joystick 1 / ratón posición:       CIA-B PRA %BFD001, bits 3..9.
/// Se devuelven bits INVERTIDOS (1 = pulsado) en los 7 bits 0..6 (origen 3..9).
/// El TECLADO (matriz de CIA-A) NO se incluye aquí: requiere el escaneo de
/// filas/columnas (gestionado aparte, `input_dev`/futuro `input_poll_key`); se
/// verifica en emulador con inyección de teclas antes de integrarlo.
///
/// `poll_input()` se llama UNA vez por frame (en update/VBL). Con el edge del
/// resto de calls puede alimentarse `g_tech_new` (selector de técnicas 1..9).

#include <eng/core/types.hpp>

namespace eng::amiga {

/// Estado de los puertos de juego del frame (hardware directo).
struct GameInput {
    eng::u8 port0 = 0; // bits 0..6 := bits 3..9 de CIAAPRA, invertidos (1=presionado)
    eng::u8 port1 = 0; // ídem para CIA-B PRA (bit disimilar/port1)
    /// ¿Alguna dirección/fire está pulsado este frame?
    bool any() const { return (port0 | port1) != 0u; }
};

/// Lee los puertos 0/1 directamente del hardware (CIA-A/B PRA).
inline void poll_input(GameInput& out) {
    const eng::u8 pa0 = *reinterpret_cast<volatile eng::u8*>(0xBFE001u);
    const eng::u8 pa1 = *reinterpret_cast<volatile eng::u8*>(0xBFD001u);
    out.port0 = static_cast<eng::u8>((~pa0 & 0x7F8u) >> 3u);
    out.port1 = static_cast<eng::u8>((~pa1 & 0x7F8u) >> 3u);
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