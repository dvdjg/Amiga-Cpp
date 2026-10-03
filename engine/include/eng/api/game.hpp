#pragma once

/// \file game.hpp
/// **Fachada de juego**: `App` junta el bucle, la pantalla, la entrada y las tareas de fondo.
/// Reexporta, ya troceados por tema, el contexto de dibujo (`api/screen.hpp`: `Screen` +
/// `BlitStream`), la descripción del display (`api/display.hpp`: `GameDisplay`/`StartError`) y el
/// materializado del mundo retenido (`api/world_render.hpp`). El juego describe **qué quiere ver**
/// sin conocer el backend, `GameContext`, `FramePlan`, `Rasterizer` ni planos.
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

#include <eng/api/device.hpp>
#include <eng/api/display.hpp>
#include <eng/api/screen.hpp>
#include <eng/api/world_render.hpp>
#include <eng/core/types/box.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/debug/telemetry.hpp>
#include <eng/engine.hpp>
#include <eng/field/strip_layer.hpp>
#include <eng/field/scroll_ladder.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/hw/bus_budget.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/input/input.hpp>
#include <eng/os/port.hpp>
#include <eng/os/os.hpp>
#include <eng/res/asset_cache.hpp>
#include <eng/res/budget.hpp>
#include <eng/scene/band_plan.hpp>
#include <eng/scene/bobs.hpp>
#include <eng/scene/display.hpp>
#include <eng/scene/plan.hpp>
#include <eng/scene/raster_plan.hpp>
#include <eng/scene/world.hpp>
#include <eng/task/background.hpp>

namespace eng {

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
	constexpr App(Backend& backend, Game& game, MemoryManager& memory) noexcept
		: m_backend(backend), m_game(game), m_engine(backend, m_adapter), m_memory(memory) {
		m_adapter.self = this;
	}
	~App() {
		shutdown();
		for (u8 i = 0u; i < m_bitmap_owner_count; ++i) {
			if (m_memory.valid() && m_bitmap_owners[i].valid()) m_memory->chip().release(m_bitmap_owners[i]);
		}
	}
	App(const App&) = delete;
	App& operator=(const App&) = delete;

	/// Ejecuta el bucle del engine en el modo **IRQ mínima**: la IRQ de VBlank solo lleva el
	/// **latido** (el hook que alimenta el puerto de mensajes / el mini-SO) y `update`/`render`
	/// corren en el **bucle principal**. Registra el **hook de VBlank** que publica
	/// `MsgType::VBlank` en el puerto.
	void run(u32 frames = kRunIndefinitely) {
		m_engine.set_vblank_hook(&App::on_vblank, this);
		m_engine.run_frames(frames);
		if (frames != kRunIndefinitely) shutdown();
	}

	/// Detiene el backend antes de liberar la escena propia. Idempotente; `run()` también lo llama
	/// al acabar un número finito de frames, para hacer comprobable el ciclo de vida en integración.
	void shutdown() noexcept {
		if (!m_started || m_shutdown) return;
		if constexpr (requires(Backend& backend) { backend.stop_display(); }) {
			m_backend.stop_display();
		}
		m_shutdown = true;
	}

	/// Compone una escena propia desde el gestor de memoria preconfigurado por el composition root.
	/// El perfil de display es OCS/A500. Una composición que no cabe no deja recursos parciales y
	/// puede corregirse antes de reintentar. NO VERIFICADA en hardware; HOST-234 prueba ownership y errores.
	[[nodiscard]] util::Expected<void, StartError> start() {
		if (m_started) return util::unexpected(StartError::AlreadyStarted);
		if (!m_memory.valid() || !m_memory->configured())
			return util::unexpected(StartError::MemoryUnavailable);
		const auto resources = scene_resources(m_display);
		if (!graphics::composition::validate(resources, graphics::composition::ocs_a500).ok())
			return util::unexpected(StartError::InvalidDisplay);
		if (graphics::composition::chip_bytes_for(resources) > m_memory->chip().free_bytes())
			return util::unexpected(StartError::OutOfMemory);
		// Preflight del **bus DMA**: si la escena declarada (display + Blitter/Copper/CPU) agota el
		// bus del A500, falla rápido antes de componer (ver `eng/hw/bus_budget.hpp`/`BUS_BUDGET.md`).
		if (hw::amiga500_bus_budget(bus_budget_input(m_display)).remaining_slots < 0)
			return util::unexpected(StartError::BusOverBudget);
		const auto display_stage = graphics::composition::display(resources);
		const auto palette_stage = graphics::composition::palette(m_display.palette.words(),
									  kPaletteFirstColor, kPaletteEntries);
		// El gradiente/efectos de copper por línea son opcionales: se añaden como etapa solo si
		// el juego los declaró (`GameDisplay::intents`).
		const bool composed = m_display.intents.empty()
			? graphics::composition::compose(m_owned_scene, *m_memory.get(), resources,
							 graphics::composition::ocs_a500, display_stage,
							 palette_stage)
			: graphics::composition::compose(m_owned_scene, *m_memory.get(), resources,
							 graphics::composition::ocs_a500, display_stage,
							 palette_stage,
							 graphics::composition::intents(m_display.intents));
		if (!composed) {
			m_owned_scene.release();
			return util::unexpected(StartError::CompositionFailed);
		}
		m_scene = m_owned_scene;
		m_started = true;
		if constexpr (requires(Backend& backend, graphics::composition::Scene& scene) { scene.takeover(backend); }) {
			m_owned_scene.takeover(m_backend);
		}
		if constexpr (requires(Backend& backend, graphics::composition::Scene& scene) { backend.install_raster(scene); }) {
			m_backend.install_raster(m_owned_scene);
		}
		return {};
	}

	/// Declara recursos del display antes de `start()`; no altera una escena ya compuesta.
	[[nodiscard]] bool set_display(const GameDisplay& display) noexcept {
		if (m_started || m_display_bound_from_scene) return false;
		m_display = display;
		return true;
	}
	[[nodiscard]] const GameDisplay& display() const noexcept { return m_display; }
	[[nodiscard]] u32 remaining_chip() const noexcept {
		return m_memory.valid() ? m_memory->chip().free_bytes() : 0u;
	}

	/// **Pila de escenas** (title→game→gameover) para juegos con estados. Una escena es un objeto
	/// con `enter/exit/update/render(App&)` (los que falten se detectan con `requires`). Mientras
	/// haya una escena en la pila, su `update`/`render` **sustituyen** a los del `Game`; el `Game`
	/// sigue siendo el *composition root* que empuja la primera escena. El llamador conserva la
	/// vida de la escena (igual que con el `Game&`). Sin heap ni vtable: capacidad fija
	/// `kMaxScenes` y despacho por thunks de puntero a función.
	template <class Scene>
	[[nodiscard]] bool push_scene(Scene& scene) {
		if (m_scene_depth >= kMaxScenes) return false;
		SceneSlot& slot = m_scenes[m_scene_depth];
		slot = SceneSlot {
			&scene,
			[](void* o, App& a) {
				if constexpr (requires(Scene& s, App& app) { s.enter(app); })
					static_cast<Scene*>(o)->enter(a);
			},
			[](void* o, App& a) {
				if constexpr (requires(Scene& s, App& app) { s.exit(app); })
					static_cast<Scene*>(o)->exit(a);
			},
			[](void* o, App& a) {
				if constexpr (requires(Scene& s, App& app) { s.update(app); })
					static_cast<Scene*>(o)->update(a);
			},
			[](void* o, App& a) {
				if constexpr (requires(Scene& s, App& app) { s.render(app); })
					static_cast<Scene*>(o)->render(a);
			},
		};
		++m_scene_depth;
		slot.enter(slot.obj, *this);
		return true;
	}

	/// Saca la escena superior (llama a su `exit`). `false` si la pila ya está vacía.
	bool pop_scene() {
		if (m_scene_depth == 0u) return false;
		--m_scene_depth;
		SceneSlot& slot = m_scenes[m_scene_depth];
		slot.exit(slot.obj, *this);
		return true;
	}

	/// Vacía la pila (con `exit` de cada escena) y empuja `scene`. `false` si no cabe.
	template <class Scene>
	[[nodiscard]] bool set_scene(Scene& scene) {
		while (pop_scene()) {}
		return push_scene(scene);
	}

	[[nodiscard]] u16 scene_depth() const noexcept { return m_scene_depth; }

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

	/// **Música por nombre de asset**: une `assets().music(name)` + `audio().play_music(...)`, de
	/// modo que el juego no nombra `MusicModule` ni elige formato; el engine detecta formato y
	/// buffer de descompresión (§2). Es la llamada de la música **de una escena**: en el `enter` de
	/// la escena. La siguiente `play_music` detiene la anterior, y `stop_music()` la para (una
	/// escena sin música la silencia en su `enter`). Templates para no exigir `assets()`/`audio()`.
	template <class B = Backend>
	bool play_music(eng::util::StringView name) {
		if constexpr (requires(B& b, eng::util::StringView n) {
				      b.audio().play_music(b.assets().music(n));
			      }) {
			return m_backend.audio().play_music(m_backend.assets().music(name));
		} else {
			return false;
		}
	}

	/// Detiene la música actual (ver `play_music`). No falla si el backend no tiene audio.
	template <class B = Backend>
	void stop_music() {
		if constexpr (requires(B& b) { b.audio().stop_music(); }) {
			m_backend.audio().stop_music();
		}
	}

	/// **Overlay de depuración del backend** (texto/rectángulos sobre el frame), si lo expone.
	/// Es la vía de las demos para rotular estado sin romper la abstracción. Template para no
	/// exigir `debug()` a backends que no lo tengan.
	template <class B = Backend>
	[[nodiscard]] decltype(auto) debug() {
		return m_backend.debug();
	}

	/// **Presupuesto de memoria** (`app.resources().used_chip()`/`can_fit_chip(...)`): vista
	/// de solo lectura de las arenas para decidir si un recurso cabe antes de pedirlo. La
	/// caché de assets + `load<T>` se construye encima (ver `PUBLIC_GAME_API.md` §2.1.4).
	template <class B = Backend>
	[[nodiscard]] res::Budget resources() noexcept {
		return res::Budget {m_backend.memory_manager()};
	}

	/// **Telemetría de memoria** para el panel de depuración: rellena `Telemetry` desde los
	/// bancos del backend (reservas reales) + la scratch de frame. El juego aporta `fps_x100`/
	/// `frames`; lo demás sale de aquí. Ver `debug::draw_telemetry`.
	[[nodiscard]] eng::debug::Telemetry telemetry() noexcept {
		eng::debug::Telemetry t {};
		eng::debug::telemetry_from(m_backend.memory_manager(), m_backend.memory(), t);
		return t;
	}

	/// **Configura la memoria del backend** (budget por banco). Normalmente en `init`; el motor
	/// podría fijarlo solo a partir del perfil de hardware (pendiente).
	template <class B = Backend>
	bool configure_memory(const MemoryConfig& cfg) {
		return m_backend.configure_memory(cfg);
	}

	/// **Bancos / arenas** (solo si el juego reserva a mano; lo normal es que no lo necesite:
	/// assets/actores/música reservan por dentro). Devuelve lo que exponga el backend.
	template <class B = Backend>
	[[nodiscard]] decltype(auto) memory_manager() {
		return m_backend.memory_manager();
	}

	/// **Runtime de assets** del backend si lo expone: `app.assets().load(path, size, policy)`.
	template <class B = Backend>
	[[nodiscard]] decltype(auto) assets() {
		return m_backend.assets();
	}

	/// **Carga un asset** por id (declara + lanza la E/S asíncrona): atajo de
	/// `app.assets().load(path, size, policy)`. `0` si no cabe; la carga se completa al drenar
	/// el puerto (`pump()`/`route_resource_io`) con los `FileDone`. En backends sin runtime de
	/// assets devuelve `0`. La decodificación tipada (`load<T>`) llegará con los decoders.
	template <class B = Backend>
	eng::u16 load_asset(const char* path, eng::u32 size,
			    res::MemoryRequest request = res::MemoryRequest::Chip, eng::u8 prio = 128u) {
		if constexpr (requires(B& b, const char* p, eng::u32 s, res::MemoryRequest req, eng::u8 pr) {
				      b.assets().load(p, s, req, pr);
			      }) {
			return m_backend.assets().load(path, size, request, prio);
		} else {
			(void)path;
			(void)size;
			(void)request;
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

	/// `true` si el asset `id` ya está cargado (`Ready`).
	template <class B = Backend>
	[[nodiscard]] bool asset_ready(eng::u16 id) const {
		if constexpr (requires(const B& b, eng::u16 i) { b.assets().state(i); }) {
			return m_backend.assets().state(id) == res::AssetState::Ready;
		} else {
			(void)id;
			return false;
		}
	}

	/// Vista no propietaria para consultar el banco/generación del asset antes de usarlo.
	template <class B = Backend>
	[[nodiscard]] res::AssetView asset_view(eng::u16 id) const {
		if constexpr (requires(const B& b, eng::u16 i) { b.assets().view(i); }) {
			return m_backend.assets().view(id);
		} else {
			(void)id;
			return {};
		}
	}

	/// Retiene un asset hasta que el objeto move-only se destruya o se reinicie explícitamente.
	template <class B = Backend>
	[[nodiscard]] res::AssetLease asset_lease(eng::u16 id) {
		if constexpr (requires(B& b, eng::u16 i) { b.assets().lease(i); }) {
			return m_backend.assets().lease(id);
		} else {
			(void)id;
			return {};
		}
	}

	/// Retención para recursos Chip mientras un consumidor de DMA (música/Blitter) siga activo.
	template <class B = Backend>
	[[nodiscard]] res::AssetDmaLease asset_dma_lease(eng::u16 id) {
		if constexpr (requires(B& b, eng::u16 i) { b.assets().lease_dma(i); }) {
			return m_backend.assets().lease_dma(id);
		} else {
			(void)id;
			return {};
		}
	}

	/// **Vista tipada** del asset `id` (`Tag` de dominio): vacía si aún no está `Ready`. La
	/// "decodificación" es la reinterpretación al dominio (los bytes se cargan tal cual).
	template <class Tag, class B = Backend>
	[[nodiscard]] eng::ByteView<Tag> asset(eng::u16 id) {
		if constexpr (requires(B& b, eng::u16 i) { b.assets().template bytes<Tag>(i); }) {
			return m_backend.assets().template bytes<Tag>(id);
		} else {
			(void)id;
			return {};
		}
	}

	/// **Carga tipada** de un asset desde fichero: declara + lanza (asíncrona) y devuelve su
	/// id. Cuando `asset_ready(id)`, `asset<Tag>(id)` da la vista de dominio. `0` si no cabe o
	/// el backend no tiene runtime de assets. (`PUBLIC_GAME_API.md` §2.1.4.)
	template <class Tag, class B = Backend>
	eng::u16 load(const char* path, eng::u32 size, res::MemoryRequest request = res::MemoryRequest::Chip,
		      eng::u8 prio = 128u) {
		return load_asset<B>(path, size, request, prio);
	}

	/// El juego registra su escena (en `init`); `screen()`/`present()` la usan.
	void bind_scene(graphics::composition::Scene& scene) noexcept {
		if (!m_started) {
			m_scene = scene;
			m_display.width = scene.width();
			m_display.height = scene.height();
			m_display.color_depth = scene.planes();
			m_display.layout = scene.layout() == graphics::composition::SceneLayout::Interleaved
					   ? graphics::PlaneLayout::Interleaved : graphics::PlaneLayout::Contiguous;
			m_display_bound_from_scene = true;
		}
	}

	/// Añade una región opaca de fondo en coordenadas de mundo. `depth` ordena la composición;
	/// los fondos con profundidad mayor se aplican encima de los de menor profundidad.
	[[nodiscard]] Ref<scene::Layer> add_background(const char* id, u8 depth, Box bounds, u8 color) noexcept {
		if (id == nullptr || bounds.empty() || bounds.x < 0 || bounds.y < 0 ||
		    m_display.color_depth == 0u || m_display.color_depth > GameDisplay::kMaxColorDepth ||
		    color >= (static_cast<u16>(kPaletteBaseColorCount << m_display.color_depth)) ||
		    static_cast<u32>(bounds.x) + bounds.w > kWorldCoordinateLimit ||
		    static_cast<u32>(bounds.y) + bounds.h > kWorldCoordinateLimit) return {};
		return m_world.add_fill_layer(id, depth, bounds, color);
	}

	/// Copia un bitmap indexado a Chip y lo conserva mientras la capa pertenezca a World. La vista
	/// del juego no posee el bloque y `App::~App()` lo libera después de detener DMA/presentación.
	[[nodiscard]] Ref<scene::Layer> add_bitmap_background(const char* id, u8 depth,
						      Span<const u8> pixels, u16 width,
						      u16 height, u16 row_bytes = 0u) {
		if (!m_memory.valid() || m_shutdown || id == nullptr || pixels.empty() ||
		    m_bitmap_owner_count >= kMaxBitmapOwners || width == 0u || height == 0u ||
		    width > kWorldCoordinateLimit) return {};
		const u16 stride = row_bytes == 0u ? width : row_bytes;
		const usize bytes = static_cast<usize>(stride) * height;
		if (stride < width || pixels.size() < bytes || bytes > kSizeLimit) return {};
		auto owner = m_memory->chip().template reserve<TextureTag>(static_cast<u32>(bytes), kBitmapAlignmentBytes);
		if (!owner.valid()) return {};
		for (usize i = 0u; i < bytes; ++i) owner.view[i] = pixels[i];
		graphics::BitmapView<eng::TextureTag> bitmap {};
		bitmap.planes = owner.mem_view();
		bitmap.width = width;
		bitmap.height = height;
		bitmap.row_bytes = stride;
		bitmap.plane_count = 1u;
		bitmap.layout = graphics::PlaneLayout::Contiguous;
		const auto layer = m_world.add_bitmap_layer(id, depth, bitmap);
		if (!layer.valid()) {
			m_memory->chip().release(owner);
			return {};
		}
		m_bitmap_owners[m_bitmap_owner_count++] = static_cast<decltype(owner)&&>(owner);
		return layer;
	}
	[[nodiscard]] Ref<scene::Layer> add_bitmap_background(const char* id, u8 depth,
						      ByteView<TextureTag> pixels, u16 width,
						      u16 height, u16 row_bytes = 0u) {
		return add_bitmap_background(id, depth, pixels.raw(), width, height, row_bytes);
	}
	[[nodiscard]] bool world_materialization_ok() const noexcept { return m_world_materialization_ok; }
	/// Materializa las capas Fill desde sus coordenadas de mundo, aplicando cámara y viewport.
	/// `App` lo ejecuta antes de `Game::render`; se puede repetir tras cambiar el contenido del mundo.
	[[nodiscard]] bool draw_world_backgrounds() {
		if (!m_scene.valid()) return false;
		m_world_materialization_ok =
			materialize_world_layers(m_world, m_display.width, m_display.height, screen());
		return m_world_materialization_ok;
	}

	/// **Mundo retenido** del juego: sus capas Fill se materializan con la cámara antes de render.
	/// Capas de actores/tilemaps se conservan para sus caminos específicos.
	[[nodiscard]] scene::World<GameDisplay::kWorldLayerCapacity>& world() noexcept { return m_world; }
	[[nodiscard]] const scene::World<GameDisplay::kWorldLayerCapacity>& world() const noexcept { return m_world; }

	/// **Registra una capa de scroll** (`playfield::ScrollLayer<Backend>` — p. ej.
	/// `StripScrollLayer` o `XlimitedScrollLayer`): `App` la **arranca** con su memoria (bloques
	/// **tageados** por banco) y su backend, y la **conduce por frame**; el juego no ve el compositor,
	/// los buffers ni los registros. `false` si no cabe o el arranque falla. Llamar desde `init(App&)`.
	[[nodiscard]] bool add_scroll_layer(eng::playfield::ScrollLayer<Backend>& layer) noexcept {
		if (m_scroll_count >= kMaxScrollLayers) return false;
		if (!layer.begin(m_backend.memory_manager(), m_backend)) return false;
		m_scroll_layers[m_scroll_count++] = layer;
		return true;
	}
	[[nodiscard]] u8 scroll_layer_count() const noexcept { return m_scroll_count; }

	/// **Registra una capa de scroll con su rol y colocación** (planner §7): además de arrancarla y
	/// conducirla, la **añade al plan de escena** del `App`, del que sale la **estrategia** de
	/// composición. El **camino manual** (`add_scroll_layer(layer)`, sin rol) sigue disponible.
	[[nodiscard]] bool add_scroll_layer(eng::playfield::ScrollLayer<Backend>& layer,
					    eng::scene::LayerRole role,
					    eng::scene::LayerPlacement placement) noexcept {
		if (!add_scroll_layer(layer)) return false;
		(void)m_scene_plan.add(role, placement);
		return true;
	}
	/// **Elige el motor del `ladder` que encaja** con la geometría cargada `g` (caso runtime/editor,
	/// §7) y lo **registra** (lo arranca y lo conduce como `add_scroll_layer`). `false` si ninguna
	/// geometría del registro coincide o el arranque falla. El camino manual sigue igual.
	template <class Ladder>
	[[nodiscard]] bool pick_scroll_engine(Ladder& ladder,
					      const eng::playfield::RuntimeScrollGeometry& g) noexcept {
		auto picked = ladder.pick(g);
		return picked.valid() && add_scroll_layer(*picked);
	}

	/// **Estrategia de composición** deducida del plan de escena registrado (`Single`/`Dpf`/`Bands`).
	[[nodiscard]] eng::scene::SceneStrategy scene_strategy() const noexcept {
		return m_scene_plan.strategy();
	}
	/// Plan de escena formado por las capas de scroll registradas con rol.
	[[nodiscard]] const auto& scene_plan() const noexcept { return m_scene_plan; }

	/// **Deriva el `RasterLayout` de la escena** del plan + las **vistas de banda** de las capas
	/// registradas (cada capa declara `band_view_count()`/`band_view()`; ver `ScrollLayer`). El
	/// juego **no monta el layout**: declara capas con rol y el `App` lo compone — con
	/// `present_layout(out)` el `App` lo materializa y toma el display. `false` si faltan vistas
	/// para las capas del plan o la estrategia no tiene mapeo (`Empty`/`Unsupported`).
	[[nodiscard]] bool scene_layout(eng::scene::RasterLayout& out) noexcept {
		eng::playfield::PlayfieldHardwareView views[kMaxScrollLayers * 2u] {};
		eng::usize n = 0u;
		for (eng::u8 i = 0u; i < m_scroll_count; ++i) {
			const eng::u8 c = m_scroll_layers[i]->band_view_count();
			for (eng::u8 v = 0u; v < c && n < kMaxScrollLayers * 2u; ++v) {
				views[n] = m_scroll_layers[i]->band_view(v);
				++n;
			}
		}
		// **DPF de una sola capa**: la capa expone 2 vistas (PF1 + PF2) → una banda **dual** (no
		// hace falta declarar dos capas). Es el caso `XlimitedScene` con `dpf` en una capa.
		if (m_scene_plan.count() == 1u && n == 2u) {
			const eng::u16 top = m_scene_plan.layer(0u).placement.top;
			(void)out.add(eng::scene::band_from_dual_view(views[0], views[1], top));
			apply_scene_palette(out);
			return true;
		}
		if (!eng::scene::plan_raster_layout(
			    m_scene_plan,
			    eng::Span<const eng::playfield::PlayfieldHardwareView> {views, n}, out)
			     .has_value()) {
			return false;
		}
		apply_scene_palette(out);
		return true;
	}

	/// Copia la **paleta** del plan de escena (primera capa) a la primera banda del layout, para que
	/// la composición derivada quede completa sin que el juego la fije a mano.
	void apply_scene_palette(eng::scene::RasterLayout& out) noexcept {
		if (out.count() == 0u || m_scene_plan.count() == 0u) {
			return;
		}
		const eng::PaletteWords pal = m_scene_plan.layer(0u).scroll.tilemap.palette;
		if (!pal.empty()) {
			out[0u].palette = pal;
			out[0u].palette_colors = static_cast<eng::u8>(pal.size());
		}
	}

	/// **Compone la pantalla por bandas y toma el display** (`RasterLayout`): el juego aporta las
	/// `Band`s (p. ej. `plan_raster_layout` sobre las vistas de sus capas, o `band_from_view`) y el
	/// `App` **materializa** la copperlist en Chip (**de la que es dueño**) y hace el `takeover`.
	/// Es el camino del split-screen / varios tramos por la fachada: el `App` posee la
	/// composición, no solo la escena de un campo. `false` si no hay bandas, memoria, o el backend
	/// no expone `takeover_display`. Llámalo una vez (en `init`, como `takeover`).
	template <eng::usize N>
	[[nodiscard]] bool present_scene(const eng::scene::Band (&bands)[N]) noexcept {
		return present_scene(eng::Span<const eng::scene::Band> {bands, N});
	}
	[[nodiscard]] bool present_scene(eng::Span<const eng::scene::Band> bands) noexcept {
		if (bands.empty()) {
			return false;
		}
		eng::scene::RasterLayout layout {};
		for (eng::usize i = 0u; i < bands.size(); ++i) {
			(void)layout.add(bands[i]);
		}
		return present_layout(layout);
	}
	/// **Materializa y muestra una `RasterLayout` ya construida** (p. ej. por `plan_raster_layout`)
	/// y hace el `takeover`. La copperlist la posee el `App`. El juego que prefiere declarar el
	/// layout entero (varios tramos con geometría propia) usa esta forma.
	[[nodiscard]] bool present_layout(const eng::scene::RasterLayout& layout) noexcept {
		if (!m_scene_copper.valid()) {
			auto& mem = m_memory.valid() ? *m_memory.get() : m_backend.memory_manager();
			m_scene_copper = mem.chip().template reserve<eng::CopperTag>(kSceneCopperWords, 16u);
			if (!m_scene_copper.valid()) {
				return false;
			}
		}
		if constexpr (requires(Backend& backend, const eng::u16* list) {
			      backend.takeover_display(list);
		      }) {
			const eng::Bytes<eng::CopperTag> slice = m_scene_copper.view;
			eng::copper::SchedulerT<false> sched {eng::Block<eng::CopperTag> {slice, m_scene_copper.kind}};
			if (!layout.materialize(sched)) {
				return false;
			}
			sched.end();
			if (!sched.ok()) {
				return false;
			}
			m_backend.takeover_display(sched.data());
			return true;
		} else {
			return false;
		}
	}

	/// **Tramos de banda** del plan de escena (para rutar objetos/dibujos con
	/// `BobLayer::emit_banded`/`for_each_band_part`): deriva el layout de bandas (`plan_bands`) sobre
	/// el alto del display. Devuelve cuántas bandas escribió (`0` si el plan no es de bandas).
	[[nodiscard]] eng::u16 scene_bands(eng::Span<eng::scene::BandSpan> out) const noexcept {
		const auto r = eng::scene::plan_bands(m_scene_plan.layers(),
						      static_cast<eng::u16>(m_display.height), out);
		return r.has_value() ? *r : 0u;
	}

	/// **Emite los BOBs por banda del `App`** (`BobLayer::emit_banded`/`FastBobLayer::emit_banded`):
	/// usa los tramos del plan de escena (o el **display completo** si no hay plan de bandas) y los
	/// `targets`/`fine` por banda que aporta la composición. Añade los blits al plan del frame
	/// (se ejecutan en el `present()`); devuelve cuántos actores se dibujaron.
	template <class Bobs>
	[[nodiscard]] eng::u16 emit_bobs_banded(Bobs& layer,
						eng::Span<const eng::graphics::BobTarget> targets,
						eng::Span<const eng::u8> fine = {}) {
		eng::scene::BandSpan bands[8] {};
		eng::u16 n = scene_bands(eng::Span<eng::scene::BandSpan> {bands, 8u});
		if (n == 0u) { // sin plan de bandas: una banda a pantalla completa
			bands[0] = eng::scene::BandSpan {0u, static_cast<eng::u16>(m_display.height),
							 eng::scene::LayerRole::Foreground};
			n = 1u;
		}
		return emit_bobs_banded(layer, eng::Span<const eng::scene::BandSpan> {bands, n},
					targets, fine);
	}
	/// Igual, pero con los **tramos de banda explícitos** (para el juego que compone sus propias
	/// bandas, p. ej. con `present_layout`/`plan_bands` sin capas de scroll registradas).
	template <class Bobs>
	[[nodiscard]] eng::u16 emit_bobs_banded(Bobs& layer,
						eng::Span<const eng::scene::BandSpan> bands,
						eng::Span<const eng::graphics::BobTarget> targets,
						eng::Span<const eng::u8> fine = {}) {
		return layer.emit_banded(m_plan, bands, targets, fine);
	}

	/// **Planner (actores)**: emite los actores del `world()` al plan del frame con el clip y
	/// el destino del contexto de dibujo de la escena ligada. Devuelve cuántos se dibujaron.
	/// Llámalo antes de `present()`. Los actores se dibujan sobre el fondo Fill; las capas de
	/// playfield/tilemap todavía esperan su driver de materialización.
	eng::u16 draw_world() {
		if (!m_scene.valid()) {
			return 0u;
		}
		playfield::DrawTarget target = m_scene.get()->draw_target(&m_plan);
		const eng::Box b = target.box();
		return m_world.emit(m_plan,
				    eng::Span<const graphics::BobTarget> {&target.bob_target(), 1u},
				    graphics::DirtyRect {b.x, b.y, b.w, b.h});
	}

	/// **Servicios de hardware** (`app.device()`): memoria, Blitter, Copper, raster. Es la vía
	/// canónica para el hardware; el juego simple lo ignora. Ver `ROADMAP_API_COHERENCE.md` (F2).
	[[nodiscard]] Device<Backend> device() noexcept { return Device<Backend> {m_backend, m_scene}; }

	/// **Toma el control del display** mostrando el buffer 0 de la escena ligada (una vez, en
	/// `init`). Es lo que instala la copperlist del camino planar (`Scene` + `DrawTarget`);
	/// sin él, `present()`/`commit()` no tienen lista sobre la que parchear `BPLxPT`. No hace
	/// nada si no hay escena (el modo copper-chunky se instala con su propio efecto).
	void takeover() {
		if (m_scene.valid()) {
			m_scene.get()->takeover(m_backend);
		}
	}

	/// **Fine-scroll horizontal del playfield**: valor de **retardo de `BPLCON1`** (0..15
	/// color-clocks) escrito tal cual en ambos nibbles (PF1/PF2), como el `main.c` de
	/// referencia. Desplaza TODO el fondo de la escena (la imagen), no los objetos ya
	/// dibujados en el framebuffer. Un juego que quiera mover el fondo por seno lo llama por
	/// frame. El valor se **aplica en el VBlank siguiente** (en `on_vblank`), de modo que el
	/// cambio de `BPLCON1` entra con el haz arriba y no parte ninguna scanline a media
	/// pantalla. `false` si la escena no expone el slot (p. ej. composición sin etapa
	/// `display`).
	[[nodiscard]] bool set_fine_scroll(u8 pixels) {
		if (!m_scene.valid() || !m_scene.get()->fine_scroll_patch_valid()) {
			return false;
		}
		m_fine_scroll_request = static_cast<u8>(pixels & 0x0fu);
		m_fine_scroll_pending = true;
		return true;
	}
	[[nodiscard]] u8 fine_scroll() const noexcept { return m_fine_scroll_request; }

	/// **Contexto de dibujo del frame** (buffer activo + plan del frame + racha de blits en
	/// streaming, `Screen::stamp`/`clear_now`). Válido hasta `present`.
	[[nodiscard]] Screen screen() noexcept {
		if (!m_scene.valid()) {
			return Screen {};
		}
		BlitStream stream {};
		if constexpr (requires(Backend& b, graphics::BlobOp o) {
				      b.blitter_blob_run_begin(o, u16{}, u16{}, s16{}, s16{}, s16{}, s16{});
			      }) {
			stream.ctx = this;
			stream.begin = &App::stream_begin;
			stream.one = &App::stream_one;
			stream.end = &App::stream_end;
		}
		return Screen {m_scene.get()->draw_target(&m_plan), stream};
	}

	/// **Publica el frame**: ejecuta el plan de Blitter (si el backend lo soporta) y commitea la
	/// escena (swap de `BPLxPT`). Para el modo copper-chunky el juego usa su `CopperChunkyLayer`.
	///
	/// Si `set_async_present(true)` y el backend soporta la vía IRQ
	/// (`execute_frame_plan_async`, `BLITTER_INTENT_QUEUE.md` §6 vía I), los trabajos se lanzan y
	/// la **IRQ de fin de blit encadena el resto**, dejando la CPU libre. El `commit` (swap de
	/// `BPLxPT`) y el vaciado del `FramePlan` se hacen **al inicio del frame siguiente**, cuando
	/// la cadena ya terminó (`begin_async_frame`): así el frame mostrado está completo (sin
	/// tearing) y el plan no se reutiliza con la cadena viva.
	void present() {
		// Reconstruye el informe de presupuesto **una vez** por plan (el encolado no lo hace por
		// job: sería O(N²)). Ver `ZERO_COST_FRAME_PATH.md`.
		m_plan.finalize();
		if (m_async_present) {
			if constexpr (requires(Backend& b, const graphics::FramePlan& p) {
					      b.execute_frame_plan_async(p);
				      }) {
				if (m_plan.ok()) {
					m_async_launched = m_backend.execute_frame_plan_async(m_plan);
				}
			}
			if (!m_async_launched) {
				// Sin vía async (plan vacío o backend sin soporte): cae al camino síncrono.
				present_sync();
			}
			return;
		}
		present_sync();
	}

	/// Camino **síncrono**: ejecuta el plan y commitea en el mismo punto (esperas activas). Es el
	/// defecto de baja latencia.
	void present_sync() {
		if constexpr (requires(Backend& b, const graphics::FramePlan& p) { b.execute_frame_plan(p); }) {
			if (m_plan.ok()) {
				(void)m_backend.execute_frame_plan(m_plan);
			}
		}
		if (m_scene.valid()) {
			m_scene.get()->commit();
		}
		m_plan.clear();
	}

	/// **Inicio del frame en modo async**: espera a que la cadena del frame anterior termine,
	/// publica el frame ya dibujado (`commit`) y vacía el `FramePlan` para el frame nuevo. Lo
	/// llama el motor **antes de `game.render`** (`Adapter::render`); en modo síncrono no hace
	/// nada (el `present_sync` ya commitea y limpia).
	void begin_async_frame() {
		if (!m_async_present) {
			return;
		}
		if constexpr (requires(Backend& b) { b.frame_plan_async_wait(); }) {
			if (m_async_launched) {
				m_backend.frame_plan_async_wait();
				if (m_scene.valid()) {
					m_scene.get()->commit();
				}
				m_async_launched = false;
			}
		}
		m_plan.clear();
	}

	/// **Modo asíncrono** de publicación (vía IRQ de blit). `true` = el `present()` lanza los
	/// trabajos y la IRQ los encadena (CPU libre); `false` (defecto) = ejecución síncrona.
	/// El motor espera la cadena y commitea al inicio del frame siguiente (`begin_async_frame`),
	/// así que el juego puede escribir el plan normalmente cada frame.
	///
	/// Toma el **slot exclusivo** de la IRQ de blit para el feeder: desactiva el servicio de
	/// fondo del bucle por defecto (`BackgroundBlitterService`, `engine.hpp`), que si no
	/// ocuparía el slot y el modo async fallaría al instalarse (silencioso, vía sync).
	void set_async_present(bool on) noexcept {
		m_async_present = on;
		if (on) {
			m_engine.set_blit_service_enabled(false);
			// Los avisos de la cadena (`Screen::notify`) se publican como `MsgType::IntentDone`
			// en el puerto del juego; el callback corre en la ISR (solo hace `port.post`).
			if constexpr (requires(Backend& b) {
					      b.set_blit_chain_notify(nullptr, nullptr);
				      }) {
				m_backend.set_blit_chain_notify(&App::on_chain_notify, this);
			}
		}
	}
	[[nodiscard]] bool async_present() const noexcept { return m_async_present; }
	[[nodiscard]] bool shutdown_complete() const noexcept { return m_shutdown; }

private:
	/// Callback de aviso de cadena (**ISR**): publica `IntentDone` con el `ticket`. Solo hace un
	/// `post` al puerto (IRQ-safe); el juego lo drena en su `update`.
	static void on_chain_notify(void* ctx, eng::u32 ticket) noexcept {
		auto* self = static_cast<App*>(ctx);
		eng::os::Msg m {};
		m.type = eng::os::MsgType::IntentDone;
		m.payload.user.a = ticket;
		(void)self->m_port.post(m);
	}

	/// Thunks de la racha de blits en streaming (`BlitStream`): conectan el `Screen` (que no
	/// conoce el backend) con `AmigaBackend::blitter_blob_run_*`. `ctx` = `this`.
	static bool stream_begin(void* ctx, graphics::BlobOp op, u16 words, u16 height, s16 amod,
				 s16 bmod, s16 cmod, s16 dmod) {
		auto* self = static_cast<App*>(ctx);
		if constexpr (requires(Backend& b, graphics::BlobOp o) {
				      b.blitter_blob_run_begin(o, u16{}, u16{}, s16{}, s16{}, s16{}, s16{});
			      }) {
			self->m_backend.blitter_blob_run_begin(op, words, height, amod, bmod, cmod, dmod);
			return true;
		}
		return false;
	}
	static void stream_one(void* ctx, const void* a, const void* b, void* d, u8 shift) {
		auto* self = static_cast<App*>(ctx);
		if constexpr (requires(Backend& bb, const void* p, void* q) {
				      bb.blitter_blob_run_one(p, p, q, u8{});
			      }) {
			self->m_backend.blitter_blob_run_one(a, b, d, shift);
		}
	}
	static bool stream_end(void* ctx) {
		auto* self = static_cast<App*>(ctx);
		if constexpr (requires(Backend& bb) { bb.blitter_blob_run_end(); }) {
			return self->m_backend.blitter_blob_run_end();
		}
		return false;
	}

	static constexpr u16 kPaletteBaseColorCount = 1u;
	static constexpr u8 kPaletteFirstColor = 0u;
	static constexpr u32 kRunIndefinitely = 0xffffffffu;
	static constexpr u8 kMaxBitmapOwners = graphics::FramePlan::kMaxDmaAssets;
	static constexpr u16 kMaxScenes = 8u;
	static constexpr u8 kMaxScrollLayers = 4u;
	/// Palabras de Copper para la copperlist que **posee** el `App` en el display por bandas
	/// (`present_scene`): cabecera + punteros por tramo/paleta, con holgura.
	static constexpr u32 kSceneCopperWords = 2048u;
	static constexpr u32 kSizeLimit = 0xffffffffu;
	static constexpr u16 kBitmapAlignmentBytes = 16u;
	static constexpr u16 kWorldCoordinateLimit = 0x7fffu;

	/// Conduce las capas de scroll registradas por frame (pinta la tira y parchea el Copper). Se
	/// llama tras el `update` del juego, para que este ya haya movido su cámara/scroll.
	void pump_scroll_layers() noexcept {
		for (u8 i = 0u; i < m_scroll_count; ++i) {
			if (m_scroll_layers[i].valid()) m_scroll_layers[i]->frame(m_backend);
		}
	}

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
			// Con escenas en la pila, la superior conduce el frame; sin escenas, el `Game`.
			if (self->m_scene_depth > 0u) {
				SceneSlot& slot = self->m_scenes[self->m_scene_depth - 1u];
				slot.update(slot.obj, *self); // la escena consume `port()` aquí
			} else {
				self->m_game.update(*self); // el juego consume `port()` aquí
			}
			self->pump_scroll_layers(); // el juego ya movió su scroll: pintar tira + Copper
		}
		void render(Backend&, GameContext& ctx) {
			self->m_context = ctx;
			self->begin_async_frame();
			self->m_world_materialization_ok = true;
			if (self->m_scene.valid())
				self->m_world_materialization_ok = materialize_world_layers(
					self->m_world, self->m_display.width, self->m_display.height,
					self->screen());
			if (self->m_scene_depth > 0u) {
				SceneSlot& slot = self->m_scenes[self->m_scene_depth - 1u];
				slot.render(slot.obj, *self);
			} else {
				self->m_game.render(*self);
			}
		}
	};

	/// Productor del hook de VBlank: sube el contador y publica en el puerto (IRQ-safe).
	static void on_vblank(void* user) noexcept {
		// Corre el **latido del mini-SO** (`os::tick`: avanza el frame, entrada, timers y
		// **tareas de frame**) en la IRQ de VBlank; el juego registra las suyas con
		// `os::set_frame_task`. La **música** la conduce el propio `App` (abajo), no el juego.
		// Solo existe en Amiga.
#if defined(ENG_AMIGA)
		eng::os::tick();
#endif
		auto& self = *static_cast<App*>(user);
		// El **engine conduce la música** (no el juego): avanza el reproductor de audio en el mismo
		// latido de VBlank. Así el juego solo llama a `app.audio().play_music(...)` y no necesita
		// registrar una tarea de frame del mini-SO (ver `ROADMAP_GAME_API.md` §2).
		if constexpr (requires { self.m_backend.audio().update_music(); }) {
			self.m_backend.audio().update_music();
		}
		// **Fine-scroll del playfield**: se aplica aquí, con el haz en VBlank, para que el
		// cambio de `BPLCON1` (que desplaza todo el fondo) entre con la pantalla en blanco y
		// no parta una scanline a media imagen. El juego lo pide con `set_fine_scroll`.
		if (self.m_fine_scroll_pending && self.m_scene.valid()) {
			(void)self.m_scene.get()->set_fine_scroll(self.m_fine_scroll_request);
			self.m_fine_scroll_pending = false;
		}
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
	eng::Ref<MemoryManager> m_memory {};                 ///< composition root owns configured pools
	graphics::composition::Scene m_owned_scene {};       ///< scene composed and owned by this App
	GameDisplay m_display {};
	bool m_started = false;
	bool m_shutdown = false;
	bool m_async_present = false;   ///< modo async del `present()` (vía IRQ de blit)
	bool m_async_launched = false;  ///< hay una cadena async en vuelo pendiente de `begin_async_frame`
	bool m_world_materialization_ok = true;
	eng::Block<TextureTag, MemoryKind::Chip> m_bitmap_owners[kMaxBitmapOwners] {};
	u8 m_bitmap_owner_count = 0u;
	eng::Ref<GameContext> m_context {};                 ///< contexto del engine (no propietario)
	eng::os::MsgPort<16> m_port {};                     ///< puerto de mensajes del sistema
	volatile u32 m_vblank_count = 0;                    ///< VBlanks publicados (IRQ)
	volatile u32 m_blitdone_count = 0;                  ///< fines de blit publicados (IRQ)
	eng::Ref<graphics::composition::Scene> m_scene {};  ///< escena del juego (no propietaria)
	scene::World<GameDisplay::kWorldLayerCapacity> m_world {}; ///< mundo retenido (capas + cámaras)
	/// Capas de scroll que conduce el `App` (**observadores no propietarios**: las posee el juego).
	eng::Ref<eng::playfield::ScrollLayer<Backend>> m_scroll_layers[kMaxScrollLayers] {};
	u8 m_scroll_count = 0u;
	/// Plan de escena de las capas registradas **con rol** (planner §7); su estrategia la da
	/// `scene_strategy()`. El camino manual (sin rol) no lo alimenta.
	eng::scene::ScenePlan<kMaxScrollLayers> m_scene_plan {};
	/// Copperlist del display por bandas (`present_scene`): la **posee** el `App` (bloque Chip).
	eng::Block<eng::CopperTag> m_scene_copper {};
	input::InputAggregator m_input {};                  ///< entrada del frame (la lee/rellena el juego)
	graphics::FramePlan m_plan {};
	u32 m_frame = 0;
	bool m_display_bound_from_scene = false;
	u8 m_fine_scroll_request = 0u;  ///< fine-scroll pedido por el juego (aplica en VBlank)
	bool m_fine_scroll_pending = false;

	/// Ranura de la pila de escenas: puntero al objeto + thunks (sin vtable ni heap).
	struct SceneSlot {
		void* obj = nullptr;
		void (*enter)(void*, App&) = nullptr;
		void (*exit)(void*, App&) = nullptr;
		void (*update)(void*, App&) = nullptr;
		void (*render)(void*, App&) = nullptr;
	};
	SceneSlot m_scenes[kMaxScenes] {};
	u16 m_scene_depth = 0u;
};

} // namespace eng
