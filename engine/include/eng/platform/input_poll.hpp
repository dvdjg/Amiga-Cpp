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

} // namespace eng::amiga