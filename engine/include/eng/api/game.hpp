#pragma once

/// \file game.hpp
/// **Fachada de juego** (borrador evolutivo): `App` junta el bucle, la pantalla, la entrada y
/// las tareas de fondo; `Screen` es el contexto de dibujo de alto nivel. El juego describe
/// **qué quiere ver** sin conocer el backend, `GameContext`, `FramePlan`, `Rasterizer` ni planos.
///
/// ```cpp
/// struct MyGame {
///     void init(auto& app);    // configura la escena (app.bind_scene(...))
///     void update(auto& app);  // logica del frame
///     void render(auto& app);  // dibujo: app.screen()...; app.present()
/// };
/// eng::amiga::MinimalBackend backend {};
/// MyGame game {};
/// eng::App app {backend, game};
/// app.run();
/// ```
///
/// El objetivo y el mapeo desde el API interno están en
/// `docs/engine/architecture/PUBLIC_GAME_API.md`; los principios, en `PUBLIC_API.md` §1.1.
/// Es **evolutivo**: cubre lo que ya existe y se amplía cuando lleguen los demás módulos.

#include <eng/core/box.hpp>
#include <eng/core/ptr.hpp>
#include <eng/engine.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/input/input.hpp>
#include <eng/task/background.hpp>

namespace eng {

/// **Contexto de dibujo de alto nivel** (análogo al `RastPort`): la app dibuja sin ver planos,
/// `FramePlan` ni `Rasterizer`. Envuelve un `field::DrawTarget` (Surface + rasterizador + plan +
/// clip) y ofrece primitivas del dominio.
class Screen {
public:
	explicit constexpr Screen(field::DrawTarget target) noexcept : m_target(target) {}

	[[nodiscard]] bool valid() const noexcept { return m_target.valid(); }
	[[nodiscard]] Box bounds() const noexcept { return m_target.box(); }

	/// Borra todo el área de dibujo con `color`.
	void clear(u8 color) {
		const Box b = bounds();
		(void)m_target.fill(b, color);
	}
	bool fill(Box box, u8 color) { return m_target.fill(box, color); }
	bool frame(Box box, u8 color) { return m_target.frame(box, color); }
	bool line(s16 x0, s16 y0, s16 x1, s16 y1, u8 color,
		  field::RasterOp op = field::RasterOp::Copy) {
		return m_target.line(x0, y0, x1, y1, color, op);
	}
	bool text(s16 x, s16 y, const char* s, u8 color) { return m_target.text(x, y, s, color); }

	/// El objetivo de dibujo subyacente (para efectos avanzados; el juego normal no lo necesita).
	[[nodiscard]] field::DrawTarget& target() noexcept { return m_target; }

private:
	field::DrawTarget m_target;
};

/// **Aplicación de juego**: bucle + pantalla + tareas, sin exponer el backend ni `GameContext`.
/// El juego implementa `init(App&)`, `update(App&)` y `render(App&)` (con `auto&` para no nombrar
/// el tipo concreto).
template <class Backend, class Game>
class App {
public:
	constexpr App(Backend& backend, Game& game) noexcept
		: m_backend(backend), m_game(game), m_engine(backend, m_adapter) {
		m_adapter.self = this;
	}
	App(const App&) = delete;
	App& operator=(const App&) = delete;

	/// Ejecuta el bucle del engine (`run_frames`: por defecto interrupt-driven).
	void run(u32 frames = 0xffffffffu) { m_engine.run_frames(frames); }

	[[nodiscard]] u32 frame() const noexcept { return m_frame; }
	[[nodiscard]] task::BackgroundQueue& tasks() noexcept { return *m_context.get()->background; }

	/// **Estado de entrada del frame** (joystick/pads, ratón, teclado). El juego lo lee
	/// (`app.input().pad0.fire`) o lo rellena con el sondeo de plataforma
	/// (`eng::platform::poll_input(app.input())`) hasta que el mini-SO de mensajes lo sustituya.
	[[nodiscard]] input::InputAggregator& input() noexcept { return m_input; }

	/// **Audio del backend** (SFX + música) si lo expone: `app.audio().play_sfx(...)`. Es un
	/// template para no exigir `audio()` a backends que no lo tengan (se instancia al usarlo).
	template <class B = Backend>
	[[nodiscard]] decltype(auto) audio() {
		return m_backend.audio();
	}

	/// El juego registra su escena (en `init`); `screen()`/`present()` la usan.
	void bind_scene(graphics::composition::Scene& scene) noexcept { m_scene = scene; }

	/// **Contexto de dibujo del frame** (buffer activo + plan del frame). Válido hasta `present`.
	[[nodiscard]] Screen screen() noexcept { return Screen {m_scene.get()->draw_target(&m_plan)}; }

	/// **Publica el frame**: ejecuta el plan de Blitter (si el backend lo soporta) y commitea la
	/// escena (swap de `BPLxPT`). Para el modo copper-chunky el juego usa su `CopperChunkyLayer`.
	void present() {
		if constexpr (requires(Backend& b, const graphics::FramePlan& p) {
				      b.execute_frame_plan(p);
			      }) {
			if (m_plan.ok()) {
				(void)m_backend.execute_frame_plan(m_plan);
			}
		}
		if (m_scene.valid()) {
			m_scene.get()->commit();
		}
		m_plan.clear();
	}

private:
	/// Adapta el contrato del engine (`init/update/render(backend, context)`) al del juego
	/// (`init/update/render(App&)`), reenviando el frame y el contexto de fondo.
	struct Adapter {
		App* self = nullptr;
		void init(Backend&, GameContext& ctx) {
			self->m_context = ctx;
			self->m_game.init(*self);
		}
		void update(Backend&, GameContext& ctx) {
			self->m_context = ctx;
			self->m_frame = ctx.frame.frame_index;
			self->m_game.update(*self);
		}
		void render(Backend&, GameContext& ctx) {
			self->m_context = ctx;
			self->m_game.render(*self);
		}
	};

	Backend& m_backend;
	Game& m_game;
	Adapter m_adapter {};
	Engine<Backend, Adapter> m_engine;
	eng::Ref<GameContext> m_context {};                 ///< contexto del engine (no propietario)
	eng::Ref<graphics::composition::Scene> m_scene {};  ///< escena del juego (no propietaria)
	input::InputAggregator m_input {};                  ///< entrada del frame (la lee/rellena el juego)
	graphics::FramePlan m_plan {};
	u32 m_frame = 0;
};

} // namespace eng
