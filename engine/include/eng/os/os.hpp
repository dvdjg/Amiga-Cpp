#pragma once

/// \file os.hpp
/// **Fachada de servicios del mini-SO** (`eng::os`): puerto del sistema, contador de frames y un
/// **tick** que latcha el VBlank y pollea los productores de entrada (una vez por frame). La
/// implementación (`tick`, `system_port`…) la aporta el backend. Ver
/// `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` y `MINI_OS_INPUT.md`.

#include <eng/core/types.hpp>
#include <eng/os/port.hpp>

namespace eng::os {

/// Puerto del sistema (el de la aplicación). El backend lo implementa.
[[nodiscard]] MsgPort<32>& system_port();

/// Contador de frames (VBlank).
[[nodiscard]] eng::u32 frame_count();

/// **Tick del mini-SO**: incrementa el frame, marca el `VBlankLatch` (`SigVBlank`) y pollea los
/// productores de entrada (ratón/joystick/pad) posteando los mensajes que cambien. Lo llama el
/// bucle principal una vez por frame. Lo implementa el backend Amiga.
void tick();

/// Postea un mensaje de usuario (`MsgType::User`); seguro desde cualquier sitio.
void post_user(eng::u32 code, eng::u32 a, eng::u32 b);

/// Pide terminar el bucle (postea `MsgType::Quit`).
void request_quit();

} // namespace eng::os
