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

/// **Espera** a que haya alguna señal de `mask` y devuelve los bits ya listos (consumidos). El
/// **host** decide cómo se bloquea: en el engine se coopera con `tick()` (ritmo de VBlank); en
/// Workbench será `Wait(señales Exec)` (ver `ROADMAP_WORKBENCH.md`, W5). No bloquea con el teclado
/// ni el ratón apagados: si no llega nada, gira en `tick()`.
[[nodiscard]] eng::u32 wait(eng::u32 mask);

/// Postea un mensaje de usuario (`MsgType::User`); seguro desde cualquier sitio.
void post_user(eng::u32 code, eng::u32 a, eng::u32 b);

/// Pide terminar el bucle (postea `MsgType::Quit`).
void request_quit();

/// **Habilita el teclado** (CIA-A serie, IRQ de nivel 2): a partir de aquí los scancodes llegan
/// como `KeyDown`/`KeyUp` por el puerto del sistema. Lo implementa el backend Amiga.
void enable_keyboard();

} // namespace eng::os
