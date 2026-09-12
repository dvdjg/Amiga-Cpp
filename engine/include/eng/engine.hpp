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

#include <eng/graphics/driver.hpp>
#include <eng/task/background.hpp>

namespace eng {

/// Telemetria del tick del juego cuando corre en la IRQ de VBlank (modo por defecto).
///
/// El engine la rellena en cada tick; el juego la consulta para vigilar su presupuesto.
/// Se mide en **lineas de raster** (no ciclos): es lo que hay en hardware real y encaja
/// con la nocion clasica de "el efecto tarda N lineas".
struct IrqTelemetry {
	u32 ticks = 0;          // ticks ejecutados
	u16 last_lines = 0;     // lineas de raster que duro el ultimo tick (update+render)
	u16 max_lines = 0;
	u16 overruns = 0;       // ticks que superaron el presupuesto
	u16 budget_lines = 313; // objetivo por tick (1 frame PAL)
};

/// Contexto mutable de juego por frame.
///
/// No debe contener ownership pesado. Es el paquete de estado que el engine pasa a
/// `init`, `update` y `render`.
struct GameContext {
	FrameStats frame {};
	IrqTelemetry irq {};
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

	static void run(void* self, u16 vpos) {
		auto* pump = static_cast<BackgroundPump*>(self);
		pump->queue->run_slice(pump->context->frame.frame_index, vpos);
		if constexpr (GameIdle<Game, Backend>) {
			pump->game->idle(*pump->backend, *pump->context);
		}
	}
};

/// Servicio de Blitter: drena el fondo mientras el backend gira en `BBUSY` (una
/// espera sincrona de blit, p. ej. `wait_blitter`). No llama a `game.idle` (no es un
/// hueco de frame) y comparte el mismo cupo por frame que el bombeo de VBlank.
struct BackgroundBlitterService {
	task::BackgroundQueue* queue = nullptr;
	GameContext* context = nullptr;

	static void run(void* self, u16 vpos) {
		auto* service = static_cast<BackgroundBlitterService*>(self);
		service->queue->run_slice(service->context->frame.frame_index, vpos);
	}
};

/// Tick del juego por IRQ de VBlank (modo interrupt-driven).
///
/// La IRQ lleva el **latido del juego**: `update` + `render` con deadline de un frame.
/// El bucle principal queda libre para el trabajo de fondo cooperativo, que la IRQ
/// preempta. `frames` lo lee/escribe el bucle principal y lo incrementa la IRQ.
template <typename Backend, typename Game>
struct InterruptTick {
	Game* game = nullptr;
	Backend* backend = nullptr;
	GameContext* context = nullptr;
	volatile u32 frames = 0u;
	u32 frame_count = 0u;

	static u16 raster_line(InterruptTick* tick, u16 fallback) {
		if constexpr (requires { tick->backend->current_raster_line(); }) {
			return tick->backend->current_raster_line();
		} else {
			return fallback;
		}
	}

	static void run(void* self, u16 vpos) {
		auto* tick = static_cast<InterruptTick*>(self);
		if (tick->frames >= tick->frame_count) {
			return;
		}
		tick->context->frame.frame_index = tick->frames;

		// Presupuesto: cuanto raster consume el tick (update+render). Si se pasa del
		// objetivo, `overruns` avisa de que el juego invade el frame/el fondo.
		const u16 start = raster_line(tick, vpos);
		tick->game->update(*tick->backend, *tick->context);
		tick->game->render(*tick->backend, *tick->context);
		const u16 end = raster_line(tick, vpos);

		IrqTelemetry& tel = tick->context->irq;
		tel.last_lines = static_cast<u16>((end - start) & 0x1ffu);
		if (tel.last_lines > tel.max_lines) tel.max_lines = tel.last_lines;
		if (tel.last_lines > tel.budget_lines) ++tel.overruns;
		++tel.ticks;

		++tick->frames;
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

	/// Modo **polling** (alternativo): `update -> wait_vblank -> render` en el bucle
	/// principal, con el fondo drenado en el hueco de VBlank y en las esperas de Blitter.
	/// Es el modelo de las demos que hacen trabajo pesado en `update`. Para trabajo nuevo
	/// se prefiere `run_frames` (interrupt-driven, sin polling de VBlank).
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
		if constexpr (requires { m_backend.set_blitter_service(nullptr, nullptr); }) {
			m_backend.set_blitter_service(&BackgroundBlitterService::run, &blitter_service);
		}

		for (u32 i = 0; i < frame_count; ++i) {
			context.frame.frame_index = i;
			m_game.update(m_backend, context);
			// `render` es el punto de commit, no de simulacion. En Amiga esto importa:
			// instalar una copperlist con COPJMP1 fuera de VBlank reinicia el Copper
			// a media pantalla y parte el frame visible. El trabajo de fondo se drena
			// mientras se espera el VBlank (prioridad al bucle principal).
			BackgroundPump<Backend, Game> pump {&m_background, &context, &m_backend, &m_game};
			m_backend.wait_vblank(&BackgroundPump<Backend, Game>::run, &pump);
			m_game.render(m_backend, context);
		}
	}

	/// Cola de tareas de fondo (el juego la usa via `GameContext::background`).
	task::BackgroundQueue& background() { return m_background; }

	/// Ejecuta `frame_count` frames en el modo **por defecto: interrupt-driven**.
	///
	/// La IRQ de VBlank corre `update`/`render` (el latido del juego, deadline de 1
	/// frame) y el bucle principal ejecuta el trabajo de fondo cooperativo, que la IRQ
	/// preempta. Es el modelo mas natural en Amiga y el que **no quema ciclos en
	/// polling** de VBlank: la CPU no espera, solo trabaja (juego en la IRQ, fondo en el
	/// bucle). Requiere un backend con `set_vblank_service`; si no lo tiene, cae a
	/// `run_frames_polling`.
	void run_frames(u32 frame_count) {
		GameContext context {};
		context.background = &m_background;

		m_backend.boot();
		m_game.init(m_backend, context);

		if constexpr (requires { m_backend.set_vblank_service(nullptr, nullptr); }) {
			InterruptTick<Backend, Game> tick {&m_game, &m_backend, &context, 0u, frame_count};
			if (m_backend.set_vblank_service(&InterruptTick<Backend, Game>::run, &tick)) {
				// El servicio de blit (nivel 3, mismo vector que el VBlank) drena el
				// fondo mientras el juego espera a un blit.
				BackgroundBlitterService blitter_service {&m_background, &context};
				if constexpr (requires { m_backend.set_blit_service(nullptr, nullptr); }) {
					m_backend.set_blit_service(&BackgroundBlitterService::run, &blitter_service);
				}
				// Bucle principal = fondo cooperativo continuo (la IRQ lo preempta).
				while (tick.frames < frame_count) {
					m_background.run_slice(tick.frames, 0u);
				}
				if constexpr (requires { m_backend.clear_blit_service(); }) {
					m_backend.clear_blit_service();
				}
				m_backend.clear_vblank_service();
				return;
			}
		}
		// Backend sin IRQ de VBlank: modo polling.
		run_frames_polling(frame_count, /*already_booted=*/true);
	}

private:
	Backend& m_backend;
	Game& m_game;
	task::BackgroundQueue m_background {};
};

} // namespace eng
