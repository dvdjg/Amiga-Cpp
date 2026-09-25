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
/// eng::amiga::AmigaBackend backend {};
/// MyGame game {};
/// eng::App app {backend, game};
/// app.run();
/// ```
///
/// El objetivo y el mapeo desde el API interno están en
/// `docs/engine/architecture/PUBLIC_GAME_API.md`; los principios, en `PUBLIC_API.md` §1.1.
/// Es **evolutivo**: cubre lo que ya existe y se amplía cuando lleguen los demás módulos.

#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/engine.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/input/input.hpp>
#include <eng/os/port.hpp>
#include <eng/res/asset_cache.hpp>
#include <eng/res/budget.hpp>
#include <eng/scene/world.hpp>
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

	/// **Dibuja un sprite** (BOB cocinado) en `(x, y)`. La geometría del destino la trae el
	/// contexto de dibujo (`DrawTarget::bob_target`, preparado por la escena), así que el
	/// juego no ve planos, strides ni minterns. `false` si no hay plan de frame o el
	/// sprite/frame no es válido (ver `graphics::Sprite::draw`).
	bool sprite(const graphics::Sprite& spr, s16 x, s16 y, u8 frame = 0u) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return spr.draw(*m_target.plan(), m_target.bob_target(), frame, x, y);
	}
	/// **Borra la caja de un sprite** en `(x, y)` (si su política es `ClearRect`).
	bool erase_sprite(const graphics::Sprite& spr, s16 x, s16 y) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return spr.erase(*m_target.plan(), m_target.bob_target(), x, y);
	}

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

	/// Ejecuta el bucle del engine (`run_frames`: por defecto interrupt-driven). Registra el
	/// **hook de VBlank** que alimenta el puerto de mensajes: cada tick publica `MsgType::VBlank`.
	void run(u32 frames = 0xffffffffu) {
		m_engine.set_vblank_hook(&App::on_vblank, this);
		m_engine.run_frames(frames);
	}

	[[nodiscard]] u32 frame() const noexcept { return m_frame; }
	[[nodiscard]] task::BackgroundQueue& tasks() noexcept { return *m_context.get()->background; }

	/// **Estado de entrada del frame** (joystick/pads, ratón, teclado). El juego lo lee
	/// (`app.input().pad0.fire`) o lo rellena con el sondeo de plataforma
	/// (`eng::platform::poll_input(app.input())`) hasta que el mini-SO de mensajes lo sustituya.
	[[nodiscard]] input::InputAggregator& input() noexcept { return m_input; }

	/// **Puerto de mensajes del sistema** (mini-SO `eng::os`): el hook de VBlank y la IRQ BLIT
	/// publican aquí; el juego lo consume en `update` con `while (app.port().try_get(m)) { ... }`.
	[[nodiscard]] eng::os::MsgPort<16>& port() noexcept { return m_port; }

	/// **Drena** el puerto de mensajes (sin bloquear) y descarta lo que no se vaya a procesar;
	/// el juego lo llama si no consume `port()` directamente. Los contadores del sistema
	/// (`vblank_count`/`blitdone_count`) los actualizan los **productores** (hook de VBlank y
	/// callback de blit), de modo que son correctos aunque el juego consuma o drene el puerto.
	void pump() noexcept {
		eng::os::Msg m;
		while (m_port.pop(m)) {
			(void)route_resource_io(m);
		}
	}
	[[nodiscard]] u32 vblank_count() const noexcept { return m_vblank_count; }
	[[nodiscard]] u32 blitdone_count() const noexcept { return m_blitdone_count; }

	/// **Blit asíncrono con notificación al puerto**: arranca la copia por Blitter y, cuando
	/// termina la IRQ BLIT, publica un `MsgType::BlitDone` (y sube `blitdone_count`). `false`
	/// si el backend no lo soporta o no cabe. La lógica puede encadenar trabajo en `update`.
	bool blitter_memcpy_async(eng::Span<u8> dst, eng::Span<const u8> src) {
		if constexpr (requires(Backend& b, eng::Span<u8> d, eng::Span<const u8> s, App& a) {
				      b.blitter_memcpy_async(d, s, &App::on_blit_done, a);
			      }) {
			return m_backend.blitter_memcpy_async(dst, src, &App::on_blit_done, *this);
		} else {
			(void)dst;
			(void)src;
			return false;
		}
	}

	/// **Audio del backend** (SFX + música) si lo expone: `app.audio().play_sfx(...)`. Es un
	/// template para no exigir `audio()` a backends que no lo tengan (se instancia al usarlo).
	template <class B = Backend>
	[[nodiscard]] decltype(auto) audio() {
		return m_backend.audio();
	}

	// --- Servicios de Blitter (fachada de hardware del juego) ---------------------------
	// Operaciones de **dominio** (sin nombrar registros ni el backend). Cada método reenvía si
	// el backend lo soporta; si no, es no-op (`false`/nada). Son las que usan los juegos con
	// colisión por Blitter o rasterizador propio (204/086). Ver `PUBLIC_GAME_API.md` §5/12.

	/// Espera a que el Blitter termine el blit en curso.
	template <class B = Backend>
	bool wait_blitter() {
		if constexpr (requires(B& b) { b.wait_blitter(); }) {
			return m_backend.wait_blitter();
		} else {
			return false;
		}
	}

	/// Instala el **rasterizador por Blitter** de la escena (los rellenos de `screen()` usan
	/// el Blitter en vez de la CPU). `false` si el backend no lo soporta.
	template <class B = Backend>
	bool install_raster(graphics::composition::Scene& scene) {
		if constexpr (requires(B& b, graphics::composition::Scene& s) {
				      b.install_raster(s);
			      }) {
			m_backend.install_raster(scene);
			return true;
		} else {
			(void)scene;
			return false;
		}
	}

	/// Borra `planes` planos (`w`×`h`, `D = 0`). `wait` sincroniza con el fin del blit.
	template <class B = Backend>
	bool blitter_clear(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w,
			   u16 h, bool wait = true) {
		if constexpr (requires(B& b, eng::PlaneBytes d) {
				      b.blitter_clear(d, u8 {}, u16 {}, u32 {}, u16 {}, u16 {}, bool {});
			      }) {
			return m_backend.blitter_clear(dst, planes, row_bytes, plane_bytes, w, h, wait);
		} else {
			(void)dst;
			(void)planes;
			(void)row_bytes;
			(void)plane_bytes;
			(void)w;
			(void)h;
			(void)wait;
			return false;
		}
	}

	/// **BOBs OR en lote** (`D = A | D`): las `count` entradas se procesan en orden.
	/// `source_modulo`/`dest_modulo` son `BLTAMOD`/`BLTBMOD`=`BLTDMOD`.
	template <class B = Backend>
	bool blitter_or_bobs(const graphics::OrBob* bobs, u32 count, u16 words, u16 height,
			     s16 source_modulo, s16 dest_modulo) {
		if constexpr (requires(B& b, const graphics::OrBob* o) {
				      b.blitter_or_bobs(o, u32 {}, u16 {}, u16 {}, s16 {}, s16 {});
			      }) {
			return m_backend.blitter_or_bobs(bobs, count, words, height, source_modulo,
							 dest_modulo);
		} else {
			(void)bobs;
			(void)count;
			(void)words;
			(void)height;
			(void)source_modulo;
			(void)dest_modulo;
			return false;
		}
	}

	/// **Colisión pixel-perfect por Blitter**: `scratch = a & b` por plano y `true` si hay
	/// algún bit. `words`×`rows` es el rect en palabras de 16 px × filas.
	template <class B = Backend>
	bool blitter_collide(eng::PlaneBytes a, eng::PlaneBytes b, eng::PlaneBytes scratch, u8 planes,
			     u16 row_bytes, u32 plane_bytes, u16 words, u16 rows) {
		if constexpr (requires(B& bk, eng::PlaneBytes p) {
				      bk.blitter_collide(p, p, p, u8 {}, u16 {}, u32 {}, u16 {}, u16 {});
			      }) {
			return m_backend.blitter_collide(a, b, scratch, planes, row_bytes, plane_bytes,
							 words, rows);
		} else {
			(void)a;
			(void)b;
			(void)scratch;
			(void)planes;
			(void)row_bytes;
			(void)plane_bytes;
			(void)words;
			(void)rows;
			return false;
		}
	}

	/// **Overlay de depuración del backend** (texto/rectángulos sobre el frame), si lo expone.
	/// Es la vía de las demos para rotular estado sin romper la abstracción. Template para no
	/// exigir `debug()` a backends que no lo tengan.
	template <class B = Backend>
	[[nodiscard]] decltype(auto) debug() {
		return m_backend.debug();
	}

	/// **Memoria del backend** si la expone (`app.memory().chip.allocate_block<T>(...)`). El
	/// juego la usa para reservar buffers de hardware (Chip RAM) sin conocer el backend.
	template <class B = Backend>
	[[nodiscard]] decltype(auto) memory() {
		return m_backend.memory();
	}

	/// **Presupuesto de memoria** (`app.resources().used_chip()`/`can_fit_chip(...)`): vista
	/// de solo lectura de las arenas para decidir si un recurso cabe antes de pedirlo. La
	/// caché de assets + `load<T>` se construye encima (ver `PUBLIC_GAME_API.md` §2.1.4).
	template <class B = Backend>
	[[nodiscard]] res::Budget resources() noexcept {
		return res::Budget {m_backend.memory()};
	}

	/// **Runtime de assets** del backend si lo expone: `app.assets().load(path, size, bank)`.
	template <class B = Backend>
	[[nodiscard]] decltype(auto) assets() {
		return m_backend.assets();
	}

	/// **Carga un asset** por id (declara + lanza la E/S asíncrona): atajo de
	/// `app.assets().load(path, size, bank)`. `0` si no cabe; la carga se completa al drenar
	/// el puerto (`pump()`/`route_resource_io`) con los `FileDone`. En backends sin runtime de
	/// assets devuelve `0`. La decodificación tipada (`load<T>`) llegará con los decoders.
	template <class B = Backend>
	eng::u16 load_asset(const char* path, eng::u32 size,
			    res::MemBank bank = res::MemBank::Chip, eng::u8 prio = 128u) {
		if constexpr (requires(B& b, const char* p, eng::u32 s, res::MemBank mb, eng::u8 pr) {
				      b.assets().load(p, s, mb, pr);
			      }) {
			return m_backend.assets().load(path, size, bank, prio);
		} else {
			(void)path;
			(void)size;
			(void)bank;
			(void)prio;
			return 0u;
		}
	}

	/// **Enruta** un mensaje de E/S a los recursos del backend (caché de assets). `true` si lo
	/// consumió. `pump()` ya lo hace; llámalo tú si consumes `port()` a mano.
	template <class B = Backend>
	bool route_resource_io(const eng::os::Msg& m) {
		if constexpr (requires(B& b, const eng::os::Msg& mm) { b.assets().on_msg(mm); }) {
			return m_backend.assets().on_msg(m);
		} else {
			(void)m;
			return false;
		}
	}

	/// El juego registra su escena (en `init`); `screen()`/`present()` la usan.
	void bind_scene(graphics::composition::Scene& scene) noexcept { m_scene = scene; }

	/// **Mundo retenido** del juego (`app.world().add_layer("fondo", 0)`): capas con su
	/// cámara. Contenedor aditivo; el planner que lo materializa llega después
	/// (`PUBLIC_GAME_API.md` §2.1.3).
	[[nodiscard]] scene::World<8u>& world() noexcept { return m_world; }
	[[nodiscard]] const scene::World<8u>& world() const noexcept { return m_world; }

	/// **Planner (actores)**: emite los actores del `world()` al plan del frame con el clip y
	/// el destino del contexto de dibujo de la escena ligada. Devuelve cuántos se dibujaron.
	/// Llámalo antes de `present()`. (La materialización de **capas** —playfield/tilemap— llega
	/// cuando exista el modelo de contenido de capa; ver `SCENE_AND_RESOURCES.md`.)
	eng::u16 draw_world() {
		if (!m_scene.valid()) {
			return 0u;
		}
		field::DrawTarget target = m_scene.get()->draw_target(&m_plan);
		const eng::Box b = target.box();
		return m_world.emit(m_plan, eng::Span<const graphics::BobTarget> {&target.bob_target(), 1u},
				    graphics::DirtyRect {b.x, b.y, b.w, b.h});
	}

	/// **Toma el control del display** mostrando el buffer 0 de la escena ligada (una vez, en
	/// `init`). Es lo que instala la copperlist del camino planar (`Scene` + `DrawTarget`);
	/// sin él, `present()`/`commit()` no tienen lista sobre la que parchear `BPLxPT`. No hace
	/// nada si no hay escena (el modo copper-chunky se instala con su propio efecto).
	void takeover() {
		if (m_scene.valid()) {
			m_scene.get()->takeover(m_backend);
		}
	}

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
			self->m_game.update(*self); // el juego consume `port()` aquí
		}
		void render(Backend&, GameContext& ctx) {
			self->m_context = ctx;
			self->m_game.render(*self);
		}
	};

	/// Productor del hook de VBlank: sube el contador y publica en el puerto (IRQ-safe).
	static void on_vblank(void* user) noexcept {
		auto& self = *static_cast<App*>(user);
		const u32 seq = self.m_vblank_count + 1u;
		self.m_vblank_count = seq;
		eng::os::Msg msg {};
		msg.type = eng::os::MsgType::VBlank;
		msg.time_stamp = seq;
		self.m_port.post(msg);
	}
	/// Productor de fin de blit: sube el contador y publica `BlitDone` (IRQ-safe).
	static void on_blit_done(App& self, u16) noexcept {
		const u32 seq = self.m_blitdone_count + 1u;
		self.m_blitdone_count = seq;
		eng::os::Msg msg {};
		msg.type = eng::os::MsgType::BlitDone;
		msg.time_stamp = seq;
		self.m_port.post(msg);
	}

	Backend& m_backend;
	Game& m_game;
	Adapter m_adapter {};
	Engine<Backend, Adapter> m_engine;
	eng::Ref<GameContext> m_context {};                 ///< contexto del engine (no propietario)
	eng::os::MsgPort<16> m_port {};                     ///< puerto de mensajes del sistema
	volatile u32 m_vblank_count = 0;                    ///< VBlanks publicados (IRQ)
	volatile u32 m_blitdone_count = 0;                  ///< fines de blit publicados (IRQ)
	eng::Ref<graphics::composition::Scene> m_scene {};  ///< escena del juego (no propietaria)
	scene::World<8u> m_world {};                         ///< mundo retenido (capas + cámaras)
	input::InputAggregator m_input {};                  ///< entrada del frame (la lee/rellena el juego)
	graphics::FramePlan m_plan {};
	u32 m_frame = 0;
};

} // namespace eng
