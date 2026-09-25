#pragma once

/// \file os.hpp
/// **Fachada de servicios del mini-SO** (`eng::os`): puerto del sistema, contador de frames y un
/// **tick** que latcha el VBlank y pollea los productores de entrada (una vez por frame). La
/// implementación (`tick`, `system_port`…) la aporta el backend. Ver
/// `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` y `MINI_OS_INPUT.md`.

#include <eng/core/types/types.hpp>
#include <eng/os/file_stream.hpp>
#include <eng/os/port.hpp>
#include <eng/os/task.hpp>
#include <eng/os/telemetry.hpp>

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

/// **Habilita el pad CD32** en el puerto 2 (protocolo serie por `POTGO`/`POTINP`): a partir de aquí
/// el puerto 2 se prueba como `Gamepad` y sus botones llegan como `MsgType::Gamepad`; si no hay
/// pad, el `tick` **cae al joystick** (misma puerto). Lo implementa el backend Amiga. Ver
/// `MINI_OS_INPUT.md` §6.
void enable_cd32_pad();

/// Dispositivos de entrada que `os::input_enable` puede activar (bitmask). `InputJoystick` y
/// `InputCd32Pad` comparten el **puerto 2**; `InputAll` activa los que conviven (ratón + teclado +
/// joystick) y el pad CD32 se añade explícitamente con `enable_cd32_pad()` (auto-detección).
enum InputMask : eng::u8 {
	InputMouse = 1u << 0,
	InputKeyboard = 1u << 1,
	InputJoystick = 1u << 2,
	InputCd32Pad = 1u << 3,
	InputAll = 0x07u,
};

/// Habilita los dispositivos de `mask`: los demás **no** se pollean. El teclado instala su IRQ de
/// CIA-A; el pad CD32 cambia el puerto 2 a protocolo serie (en lugar del joystick). Lo implementa
/// el backend. Sin llamar, el backend pollea **todos** (compatibilidad).
void input_enable(eng::u8 mask);

/// Añade/actualiza un **timer de usuario**: postea `MsgType::Timer` con `id` cada `frames` VBlanks.
/// `frames == 0` lo elimina. Lo implementa el backend (sobre `TimerService`).
void add_timer(eng::u16 id, eng::u16 frames);

/// **Hook de VBlank** del mini-SO (lo implementa el backend): avanza el frame, señaliza
/// `SigVBlank` y pollea la entrada habilitada + los timers. Es el mismo latido que `os::tick`
/// (polling); `os::init` lo registra como `vblank_hook` del `Engine`, que es quien posee la IRQ de
/// VBlank, de modo que el latido corre **dentro de la IRQ** (sin sondeo en el bucle).
void vblank_hook(void* user);

/// **Arranca el mini-SO**: habilita los dispositivos de `inputs` y registra el latido del mini-SO
/// en el VBlank del `engine` (`engine.set_vblank_hook`). A partir de aquí el frame avanza y la
/// entrada/timers se publican por **IRQ**; **no** hay que llamar a `os::tick` en el bucle. Devuelve
/// `true` (el registro del hook no falla). Ver §9 de `MINI_OS_MESSAGE_LOOP.md`.
template <class EngineT>
[[nodiscard]] bool init(EngineT& engine, eng::u8 inputs = InputAll) noexcept {
	input_enable(inputs);
	engine.set_vblank_hook(&vblank_hook, nullptr);
	return true;
}

} // namespace eng::os
