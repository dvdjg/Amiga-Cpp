#pragma once

/// \file engine.hpp
/// Bucle principal generico del engine.
///
/// Esta cabecera demuestra la arquitectura que buscamos: la logica del juego no
/// conoce el Amiga directamente. El juego habla con un `Backend`, y el backend puede
/// ser Amiga, Mega Drive, Neo Geo o una version PC de herramientas.
///
/// Por ahora el bucle es intencionadamente pequeno. A medida que crezca incorporara
/// fases explicitas: input, update fijo, preparacion de render, blitter jobs,
/// copper commit, sprites, audio y profiler.

#include <eng/core/util/scope_guard.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/task/background.hpp>

namespace eng {

/// Contexto mutable de juego por frame.
///
/// No debe contener ownership pesado. Es el paquete de estado que el engine pasa a
/// `init`, `update` y `render`.
struct GameContext {
	FrameStats frame {};
	/// Cola de tareas de fondo. El engine la drena en los huecos (VBlank) con
	/// prioridad al bucle principal; el juego registra rutinas y consulta su
	/// progreso/rendimiento aqui. Dentro del bucle del engine nunca es null.
	task::BackgroundQueue* background = nullptr;
};

/// Contrato minimo que debe cumplir una demo/juego.
///
/// Usamos un concept de C++23 para obtener abstraccion sin coste runtime: si un tipo
/// no tiene `init`, `update` y `render`, el error aparece en compilacion.
template <typename Game, typename Backend>
concept GameModule = requires(Game game, Backend& backend, GameContext& context) {
	game.init(backend, context);
	game.update(backend, context);
	game.render(backend, context);
};

/// Contrato OPCIONAL: un juego puede exponer `idle(backend, context)` para avanzar
/// trabajo de hardware mientras el engine espera el VBlank (la CPU esta ociosa y el
/// Blitter puede usar el bus completo). Ver `Engine::run_frames`.
template <typename Game, typename Backend>
concept GameIdle = requires(Game game, Backend& backend, GameContext& context) {
	game.idle(backend, context);
};

/// **Hook de VBlank**: el **latido** del frame. Es lo único que corre la IRQ de VBlank (modo
/// IRQ mínima): el mini-SO (`eng::os`) lo usa para publicar `MsgType::VBlank` y avanzar su tick.
/// `update`/`render` **no** van aquí (los corre el bucle principal). Se llama una vez por tick.
/// No captura: recibe el `user` del Engine.
using VBlankHook = void (*)(void* user);

/// Bombea el trabajo de fondo durante el hueco de VBlank.
///
/// El engine lo pasa al backend como tarea ociosa: solo corre mientras el CPU
/// estaria esperando el VBlank (o cualquier otro hueco), asi que `update`/`render`
/// tienen siempre prioridad. Drena como maximo `max_slices_per_frame` rebanadas de
/// la cola por frame (para acotar el coste) y, si el juego expone `idle()`, lo llama.
template <typename Backend, typename Game>
struct BackgroundPump {
	task::BackgroundQueue* queue = nullptr;
	GameContext* context = nullptr;
	Backend* backend = nullptr;
	Game* game = nullptr;

	static void run(BackgroundPump& pump, u16 vpos) {
		pump.queue->run_slice(pump.context->frame.frame_index, vpos);
		if constexpr (GameIdle<Game, Backend>) {
			pump.game->idle(*pump.backend, *pump.context);
		}
	}
};

/// Servicio de Blitter: drena el fondo mientras el backend gira en `BBUSY` (una
/// espera sincrona de blit, p. ej. `wait_blitter`). No llama a `game.idle` (no es un
/// hueco de frame) y comparte el mismo cupo por frame que el bombeo de VBlank.
struct BackgroundBlitterService {
	task::BackgroundQueue* queue = nullptr;
	GameContext* context = nullptr;

	static void run(BackgroundBlitterService& service, u16 vpos) {
		service.queue->run_slice(service.context->frame.frame_index, vpos);
	}
};

/// **Latido de VBlank mínimo** (el bucle por defecto `Engine::run_frames`): la IRQ **solo
/// anuncia** el frame — `vblank_hook` (p. ej. el mini-SO: input, timers, cola) + contador — y
/// `update`/`render` los corre el **bucle principal**, sobre su propia pila, al ver el contador
/// avanzar. Es el modelo de un SO real: la interrupción avisa, el trabajo se hace fuera de ella,
/// así que un `render` pesado **no** invade VBlanks (1 latido = 1 frame).
template <typename Backend, typename Game>
struct VBlankHeartbeat {
	GameContext* context = nullptr;
	volatile u32 frames = 0u;
	VBlankHook vblank_hook = nullptr; ///< latido (p. ej. `os::tick`) — debe ser corto
	void* vblank_user = nullptr;

	static void run(VBlankHeartbeat& hb, u16) {
		hb.context->frame.frame_index = hb.frames;
		if (hb.vblank_hook != nullptr) {
			hb.vblank_hook(hb.vblank_user);
		}
		++hb.frames;
	}
};

/// Engine generico parametrizado por backend y juego.
///
/// Esta clase es el primer paso para evitar que el juego sea "codigo Amiga". El
/// backend decide como esperar VBlank, reservar memoria o dibujar; el juego decide
/// que quiere que ocurra.
template <typename Backend, typename Game>
requires GameModule<Game, Backend>
class Engine {
public:
	constexpr Engine(Backend& backend, Game& game)
		: m_backend(backend), m_game(game) {}

	/// Modo **polling** (fallback, **sin** IRQ de VBlank): `update -> wait_vblank -> render`
	/// en el bucle principal, con el fondo drenado en el hueco de VBlank y en las esperas de
	/// Blitter. Se usa cuando el backend no ofrece `set_vblank_service`. Para trabajo normal,
	/// el bucle por defecto es `run_frames` (IRQ mínima).
	///
	/// Las demos acaban tras `frame_count` para que el runner pueda capturar y cerrar
	/// WinUAE de forma determinista; `0xffffffff` equivale a duracion indefinida.
	void run_frames_polling(u32 frame_count, bool already_booted = false) {
		GameContext context {};
		context.background = &m_background;

		if (!already_booted) {
			m_backend.boot();
			m_game.init(m_backend, context);
		}

		// Si el backend sabe ejecutar tareas durante las esperas de Blitter, drena
		// ahi el fondo (comparte el cupo por frame con el bombeo de VBlank).
		BackgroundBlitterService blitter_service {&m_background, &context};
		if constexpr (requires { m_backend.set_blitter_service(&BackgroundBlitterService::run, blitter_service); }) {
			m_backend.set_blitter_service(&BackgroundBlitterService::run, blitter_service);
		}

		for (u32 i = 0; i < frame_count; ++i) {
			context.frame.frame_index = i;
			reset_frame_scratch();
			if (m_vblank_hook != nullptr) {
				m_vblank_hook(m_vblank_user);
			}
			m_game.update(m_backend, context);
			// `render` es el punto de commit, no de simulacion. En Amiga esto importa:
			// instalar una copperlist con COPJMP1 fuera de VBlank reinicia el Copper
			// a media pantalla y parte el frame visible. El fondo se drena en el hueco de
			// VBlank, pero **solo** se instala el callback si hay algo que drenar (o el
			// juego expone `idle`): con la cola vacia, `wait_vblank` queda como bucle
			// apretado, sin coste por iteracion.
			const bool needs_pump = (m_background.live_count() != 0u) || GameIdle<Game, Backend>;
			if (needs_pump) {
				BackgroundPump<Backend, Game> pump {&m_background, &context, &m_backend, &m_game};
				m_backend.wait_vblank(&BackgroundPump<Backend, Game>::run, pump);
			} else {
				m_backend.wait_vblank();
			}
			m_game.render(m_backend, context);
		}
	}

	/// Cola de tareas de fondo (el juego la usa via `GameContext::background`).
	task::BackgroundQueue& background() { return m_background; }

	/// Registra un **hook de VBlank** (`nullptr` para quitarlo). Se llama una vez por tick,
	/// antes de `update`/`render`, tanto en modo interrupt-driven como polling. Es el punto
	/// que usa el `App` para reenviar el latido al puerto de mensajes del mini-SO sin abrir
	/// un segundo servicio de VBlank.
	void set_vblank_hook(VBlankHook hook, void* user) noexcept {
		m_vblank_hook = hook;
		m_vblank_user = user;
	}

	/// Ejecuta `frame_count` frames en el **bucle por defecto**: modo **IRQ mínima**
	/// (§`VBlankHeartbeat`). La IRQ de VBlank solo lleva el **latido** (el `vblank_hook`,
	/// p. ej. `os::tick` + cola del mini-SO) y el contador de frames; `update`/`render` corren
	/// en el **bucle principal** (su propia pila), notificados por el contador. Es el modelo de
	/// un SO real. Si el backend no tiene servicio de VBlank, cae a `run_frames_polling`.
	void run_frames(u32 frame_count) {
		GameContext context {};
		context.background = &m_background;

		m_backend.boot();
		m_game.init(m_backend, context);

		VBlankHeartbeat<Backend, Game> hb {&context, 0u, m_vblank_hook, m_vblank_user};
		if constexpr (requires { m_backend.set_vblank_service(&VBlankHeartbeat<Backend, Game>::run, hb); }) {
			if (m_backend.set_vblank_service(&VBlankHeartbeat<Backend, Game>::run, hb)) {
				[[maybe_unused]] auto services_off = eng::util::make_scope_guard([&] {
					if constexpr (requires { m_backend.clear_blit_service(); }) {
						m_backend.clear_blit_service();
					}
					m_backend.clear_vblank_service();
				});
				BackgroundBlitterService blitter_service {&m_background, &context};
				if constexpr (requires { m_backend.set_blit_service(&BackgroundBlitterService::run, blitter_service); }) {
					m_backend.set_blit_service(&BackgroundBlitterService::run, blitter_service);
				}
				// El bucle consume el latido: mientras no avance el contador, adelanta el
				// fondo (equivale al hueco de VBlank); cuando avanza, corre el frame fuera
				// de la IRQ. `frame_index` cuenta **frames completados** (como en
				// `run_frames_polling`), no latidos: asi el run-status/gate miden la tasa
				// real de update y las animaciones avanzan por frame dibujado.
				u32 seen = 0u;
				u32 done = 0u;
				while (done < frame_count) {
					if (hb.frames == seen) {
						m_background.run_slice(done, 0u);
						continue;
					}
					seen = hb.frames;
					context.frame.frame_index = done;
					reset_frame_scratch();
					m_game.update(m_backend, context);
					m_game.render(m_backend, context);
					++done;
				}
				return;
			}
		}
		run_frames_polling(frame_count, /*already_booted=*/true);
	}

private:
	/// **Reinicia la scratch de frame** al empezar cada frame (`MemorySystem::reset_frame`): los
	/// recursos temporales que el juego/efectos reservaron con `backend.memory().frame` dejan de
	/// ser válidos (LIFO total). No toca los bancos persistentes. Si el backend no expone un
	/// `MemorySystem` (host/otro), es no-op.
	void reset_frame_scratch() noexcept {
		if constexpr (requires { m_backend.memory().reset_frame(); }) {
			m_backend.memory().reset_frame();
		}
	}

	Backend& m_backend;
	Game& m_game;
	task::BackgroundQueue m_background {};
	VBlankHook m_vblank_hook = nullptr;
	void* m_vblank_user = nullptr;
};

} // namespace eng
