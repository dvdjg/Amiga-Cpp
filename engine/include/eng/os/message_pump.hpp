#pragma once

/// \file message_pump.hpp
/// **Bucle reactivo del mini-SO** (`eng::os`): drena el puerto de mensajes y entrega cada mensaje al
/// `App`. `MessagePumpGame<App>` es un `Game` (para `eng::Engine`) que drena en `update` y llama a
/// `on_frame`/`on_render` del `App`; así el VBlank y el input **siempre** se atienden antes de la
/// lógica de frame. Ver `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §7.

#include <eng/core/ptr.hpp>
#include <eng/os/dispatch.hpp>
#include <eng/os/port.hpp>
#include <eng/os/telemetry.hpp>

namespace eng::os {

/// Drena `port` y entrega cada mensaje al `App` con `app.on_msg(m)`.
template <class App, eng::u16 N>
void pump_messages(App& app, MsgPort<N>& port) noexcept {
	Msg m;
	while (port.pop(m)) {
		app.on_msg(m);
	}
}

/// `Game` que usa el mini-SO. El `App` implementa `on_start(ctx)`, `on_msg(m)`,
/// `on_frame(frame)` y `on_render()`. El puerto se liga con `bind_port` (normalmente en `on_start`).
///
/// ```text
///   Engine -> update()  ->  drena el puerto (input/VBlank/FileDone)  ->  app.on_msg(m)
///                        ->  app.on_frame(frame)   (lógica + pintado)
///          -> render()  ->  app.on_render()
/// ```
template <class App, eng::u16 N = 32u>
struct MessagePumpGame {
	App app {};
	eng::Ref<MsgPort<N>> port {}; ///< puerto del mini-SO (no propietario)
	void (*tick)() = nullptr; ///< tick opcional del mini-SO (`os::tick`), antes de drenar
	IrqTelemetry* telemetry = nullptr; ///< telemetría de saturación opcional (no propietaria)

	void bind_port(MsgPort<N>& p) noexcept { port = p; }
	/// Liga la telemetría: se muestrea el puerto cada frame (descartes + marcas de agua).
	void bind_telemetry(IrqTelemetry& t) noexcept { telemetry = &t; }

	/// Arranque: delega en `app.on_start(ctx)`.
	template <class Backend, class Ctx>
	void init(Backend&, Ctx& ctx) {
		app.on_start(ctx);
	}

	/// Tick del mini-SO (si lo hay), drena el puerto (entrega cada mensaje a `app.on_msg`) y luego
	/// llama a `app.on_frame`.
	template <class Backend, class Ctx>
	void update(Backend&, Ctx& ctx) {
		if (tick != nullptr) {
			tick();
		}
		if (port.valid()) {
			pump_messages(app, *port.get());
			if (telemetry != nullptr) {
				telemetry->sample_port(*port.get());
			}
		}
		app.on_frame(ctx.frame.frame_index);
	}

	/// Commit de frame: delega en `app.on_render(backend)`.
	template <class Backend, class Ctx>
	void render(Backend& backend, Ctx&) {
		app.on_render(backend);
	}
};

} // namespace eng::os
