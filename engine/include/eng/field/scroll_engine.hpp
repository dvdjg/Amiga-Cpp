#pragma once

/// \file scroll_engine.hpp
/// Driver del scroll separado del playfield (diseño §7 `ScrollEngine`).
///
/// `ScrollEngine` posee el ESTADO de cámara (mappos/videopos/dirección + la
/// guarda de 1 word del plane-shift) y decide el paso de 1 px a partir de
/// `(dx, dy)`. El playfield es el *layout sink*: conoce su geometría, su
/// mapeo y su framebuffer, y expone `scroll_right/left/up/down` que aplican
/// cada paso leyendo/actualizando el estado del engine.
///
/// Así el algoritmo (quién avanza, cuándo y en qué orden) queda FUERA del
/// framebuffer: un playfield puede no tener scroll (canvas estático) o tenerlo
/// (BG corkscrew 8-way) sin que la escena distinga. Toda la contracción X-only/
/// V-only/OneDirection se resuelve en el `ScrollMode` del playfield.
///
/// Reglas del engine: sin heap, sin RTTI, gnu++23. `ScrollState` es un tipo
/// valor que el playfield NO posee como miembro de scroll: vive aquí.

#include <eng/core/types.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::field {

/// Direcciones del corkscrew (constantes del original Scroller_XYLimited).
enum ScrollDirection : u8 {
    ScrollDirNone = 0,  // primer paso, sin dirección previa
    ScrollDirLeft = 1,
    ScrollDirRight = 2,
};

/// Estado de cámara del scroll. Lo mueve `ScrollEngine` (paso a paso) y lo lee
/// el display (`hardware_view`/getters). `saveword*` es la guarda de 1 word que
/// el blit plane-shifted pisa y restaura (solo corkscrew).
struct ScrollState {
    s32 mapposx = 0;
    s32 videoposx = 0;
    s32 mapposy = 0;
    s32 videoposy = 0;
    u8 previous_xdirection = ScrollDirNone; // estado del restore (saveword)
    u16* savewordpointer = nullptr;         // dirección de la guarda plane-shift
    u16 saveword = 0;                       // valor salvado de la guarda
};

/// Driver del scroll. No es un strategy polimórfico: `step` es un template
/// sobre el sink (el playfield concreto), que expone `scroll_right/left/up/down`.
class ScrollEngine {
public:
    ScrollState& state() { return m_state; }
    const ScrollState& state() const { return m_state; }

    /// Avanza 1 px por eje (0 si no toca). Réplica exacta del driver original:
    /// primero el eje X y luego el Y, ambos en una llamada (diagonal posible).
    /// Devuelve false si un borde del mapa bloqueó el avance en algún eje.
    template <class Sink>
    bool step(graphics::FramePlan& plan, Sink& sink, s32 dx, s32 dy) {
        if (dx != 0 && !((dx > 0) ? sink.scroll_right(plan) : sink.scroll_left(plan))) return false;
        if (dy != 0 && !((dy > 0) ? sink.scroll_down(plan) : sink.scroll_up(plan))) return false;
        return true;
    }

private:
    ScrollState m_state {};
};

} // namespace eng::field