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

#include <eng/api/device.hpp>
#include <eng/core/types/box.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/debug/telemetry.hpp>
#include <eng/engine.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/input/input.hpp>
#include <eng/os/port.hpp>
#include <eng/os/os.hpp>
#include <eng/res/asset_cache.hpp>
#include <eng/res/budget.hpp>
#include <eng/scene/bobs.hpp>
#include <eng/scene/world.hpp>
#include <eng/task/background.hpp>

namespace eng {

/// Descripción declarativa del display que `App::start()` compone y posee.
struct GameDisplay {
	static constexpr u8 kMaxColorDepth = 8u;
	static constexpr u8 kWorldLayerCapacity = scene::kDefaultWorldLayerCapacity;
	static constexpr u16 kPaletteEntries = eng::kPaletteEntries;
	static constexpr u8 kDefaultColorDepth = 4u;
	static constexpr u8 kDefaultBufferCount = 1u;
	static constexpr u16 kDefaultWidth = 320u;
	static constexpr u16 kDefaultHeight = 256u;
	u16 width = kDefaultWidth;
	u16 height = kDefaultHeight;
	u8 color_depth = kDefaultColorDepth;
	u8 buffers = kDefaultBufferCount;
	graphics::PlaneLayout layout = graphics::PlaneLayout::Contiguous;
	Palette32 palette = kBlackPalette;
	/// **Efectos de Copper por línea** (opcional): gradientes, zonas de paleta, etc. El juego los
	/// declara como intenciones de dominio; `App::start()` los compone sin que el juego vea
	/// registros ni la copperlist. Ver `composition::intents`.
	eng::Span<const graphics::CopperIntent> intents {};
};

/// Motivo por el que no pudo prepararse el display propio de `App`.
enum class StartError : u8 {
	AlreadyStarted,
	MemoryUnavailable,
	InvalidDisplay,
	OutOfMemory,
	CompositionFailed,
};

/// **Racha de blits en streaming**, inyectada por el `App` en el `Screen` de forma *type-erased*
/// (punteros a función + contexto): el `Screen` puede emitir blits inmediatos **sin conocer el
/// backend**. Ver `Screen::stamp`/`clear_now` y `ZERO_COST_FRAME_PATH.md` §Streaming.
struct BlitStream {
	void* ctx = nullptr;
	bool (*begin)(void*, graphics::BlobOp, u16, u16, s16, s16, s16, s16) = nullptr;
	void (*one)(void*, const void*, const void*, void*, u8) = nullptr;
	bool (*end)(void*) = nullptr;
	[[nodiscard]] bool valid() const noexcept { return begin != nullptr; }
};

/// **Contexto de dibujo de alto nivel** (análogo al `RastPort`): la app dibuja sin ver planos,
/// `FramePlan` ni `Rasterizer`. Envuelve un `field::DrawTarget` (Surface + rasterizador + plan +
/// clip) y ofrece primitivas del dominio.
class Screen {
public:
	Screen() = default;
	explicit constexpr Screen(field::DrawTarget target, BlitStream stream = {}) noexcept
		: m_target(target), m_stream(stream) {}


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

	/// Borra `b` (`D = 0`) **encolado en el plan del frame**, en orden con los sprites (a
	/// diferencia de `fill`, que se vuelca con el rasterizador y puede pisar lo dibujado después).
	/// Es la forma de limpiar una banda/región antes de pintar objetos. `false` si no hay plan.
	bool clear_box(Box b) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return eng::scene::clear_box(*m_target.plan(), m_target.bob_target(), b.x, b.y, b.w, b.h);
	}

	/// **Dibuja un sprite** (BOB cocinado) en `(x, y)`. La geometría del destino la trae el
	/// contexto de dibujo (`DrawTarget::bob_target`, preparado por la escena), así que el
	/// juego no ve planos, strides ni minterns. `false` si no hay plan de frame o el
	/// sprite/frame no es válido (ver `graphics::Sprite::draw`).
	bool sprite(const graphics::Sprite& spr, s16 x, s16 y, u8 frame = 0u) {
		if (!m_target.plan().valid()) {
			return false;
		}
		const Box clip = m_target.box();
		if (x < clip.x || y < clip.y || static_cast<s32>(x) + spr.width() >
			static_cast<s32>(clip.x) + clip.w || static_cast<s32>(y) + spr.height() >
			static_cast<s32>(clip.y) + clip.h) return false;
		return spr.draw(*m_target.plan(), m_target.bob_target(), frame, x, y);
	}
	/// **Borra la caja de un sprite** en `(x, y)` (si su política es `ClearRect`).
	bool erase_sprite(const graphics::Sprite& spr, s16 x, s16 y) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return spr.erase(*m_target.plan(), m_target.bob_target(), x, y);
	}

	/// **Encola un aviso** (`ticket`) en el plan del frame: en modo asíncrono
	/// (`App::set_async_present(true)`) la IRQ de fin de blit lo publica como
	/// `MsgType::IntentDone` (con el `ticket` en `payload.user.a`) cuando hayan terminado los
	/// trabajos encolados **hasta ahora**. Para avisar al final de la ristra, llamar tras el
	/// último `sprite`/`clear_box`/… No consume Blitter: es una marca en la cadena.
	bool notify(u16 ticket) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return m_target.plan()->add_notify(ticket);
	}

	/// **Blit planar** al plan del frame (copia desde una hoja planar). Para primitivas que
	/// `Screen` no cubre (chunky→planar usa `c2p`). El plan lo pone el contexto.
	bool blit(eng::Span<const u16> src, s32 x, s32 y, u16 w, u16 h, u16 src_row_bytes,
		  u32 src_plane_stride, u8 planes, u8 source_shift = 0u, bool descending = false,
		  field::RasterOp op = field::RasterOp::Copy) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return m_target.blit(*m_target.plan(), src, x, y, w, h, src_row_bytes, src_plane_stride,
				    planes, source_shift, descending, op);
	}
	/// Copia un asset planar completo (procedencia Chip + geometría + layout) a la pantalla en el
	/// plan del frame. El juego no pasa punteros,
	/// strides, plano ni cantidad de planos: los trae el asset tipado y el contexto Screen.
	[[nodiscard]] bool bitmap(graphics::ChipBitmapView<PlaneTag> source, Box destination) {
		const Box screen_bounds = bounds();
		const auto& target = m_target.bob_target();
		if (!m_target.plan().valid() || !source.valid() || destination.empty() ||
		    source.plane_count > graphics::kBlitterMaxPlanes ||
		    source.layout != (target.interleaved() ? graphics::PlaneLayout::Interleaved
						    : graphics::PlaneLayout::Contiguous) ||
		    source.row_bytes < source.width / graphics::kPixelsPerByte ||
		    (source.row_bytes & (graphics::kBytesPerBlitterWord - 1u)) != 0u ||
		    source.planes.size() < source.byte_count() ||
		    destination.x < screen_bounds.x || destination.y < screen_bounds.y ||
		    static_cast<u32>(destination.w) > source.width || static_cast<u32>(destination.h) > source.height ||
		    static_cast<s32>(destination.x) + destination.w > static_cast<s32>(screen_bounds.x) + screen_bounds.w ||
		    static_cast<s32>(destination.y) + destination.h > static_cast<s32>(screen_bounds.y) + screen_bounds.h ||
		    (destination.x & (graphics::kPixelsPerBlitterWord - 1u)) != 0 ||
		    (destination.w & (graphics::kPixelsPerBlitterWord - 1u)) != 0u ||
		    source.width / graphics::kPixelsPerBlitterWord > graphics::kBlitterMaxWordsPerRow ||
		    source.height > graphics::kBlitterMaxRows ||
		    destination.w != source.width || destination.h != source.height) return false;
		const u16 words = static_cast<u16>(source.width / graphics::kPixelsPerBlitterWord);
		const u32 copied_row_bytes = static_cast<u32>(words) * graphics::kBytesPerBlitterWord;
		const u32 source_row_stride = source.interleaved()
				       ? static_cast<u32>(source.row_bytes) * source.plane_count
				       : source.row_bytes;
		const bool source_interleaved = source.interleaved();
		const bool destination_interleaved = target.interleaved();
		if (source.plane_count != target.plane_count || source_interleaved != destination_interleaved)
			return false;
		const u32 destination_row_stride = target.interleaved()
					   ? static_cast<u32>(target.row_bytes) * target.plane_count
					   : target.row_bytes;
		if (source.interleaved()) {
			// Each physical plane row is `row_bytes` apart; the modulo skips the remaining
			// planes to reach the same plane on the next logical scanline.
			graphics::BlitJob job {};
			job.kind = graphics::BlitJobKind::CopyRect;
			job.source = graphics::BlitPtr {source.planes};
			job.destination = graphics::BlitPtr {target.planes,
				static_cast<s32>(static_cast<u32>(destination.y) * destination_row_stride +
						 destination.x / graphics::kPixelsPerByte)};
			job.words_per_row = words;
			job.height = source.height;
			job.source_modulo_bytes = static_cast<s16>(source_row_stride - copied_row_bytes);
			job.destination_modulo_bytes = static_cast<s16>(destination_row_stride - copied_row_bytes);
			job.bitplane_count = source.plane_count;
			job.interleaved = true;
			job.source_plane_stride_bytes = source.plane_pointer_step();
			job.destination_plane_stride_bytes = target.plane_pointer_step();
			return m_target.plan()->add_copy_rect(job);
		}
		graphics::BlitJob job {};
		job.kind = graphics::BlitJobKind::CopyRect;
		job.source = graphics::BlitPtr {source.planes};
		job.destination = graphics::BlitPtr {target.planes,
			static_cast<s32>(static_cast<u32>(destination.y) * destination_row_stride +
					 destination.x / graphics::kPixelsPerByte)};
		job.words_per_row = words;
		job.height = source.height;
		job.source_modulo_bytes = 0;
		job.destination_modulo_bytes = 0;
		job.bitplane_count = source.plane_count;
		job.interleaved = false;
		job.source_plane_stride_bytes = source.plane_pointer_step();
		job.destination_plane_stride_bytes = target.plane_pointer_step();
		return m_target.plan()->add_copy_rect(job);
	}

	/// **Chunky→planar** por el seam (Blitter si hay plan, si no CPU del playfield).
	bool c2p(const field::C2pRequest& req) { return m_target.c2p(req); }

	/// El objetivo de dibujo subyacente (para efectos avanzados; el juego normal no lo necesita).
	[[nodiscard]] field::DrawTarget& target() noexcept { return m_target; }

	/// **Racha de estampado (streaming)**: emite copias de `sheet` al Blitter **en el momento** de
	/// cada `at()` (espera al blit anterior y escribe solo los registros que cambian), **sin
	/// `FramePlan` ni pasada de ejecución**. El juego describe objetos; el motor emite. Ver
	/// `ZERO_COST_FRAME_PATH.md` §Streaming. Requiere dibujar en orden con `clear_now` (no mezclar
	/// con el plan, que se ejecuta en `present`).
	struct StampRun {
		BlitStream stream {};
		const graphics::Sprite* sheet = nullptr;
		graphics::BobTarget target {};
		graphics::BlobOp op = graphics::BlobOp::CookieCut;
		u16 words = 0u;
		s32 start_row = 0;
		bool active = false;

		/// Emite una copia del `frame` del sprite en `(x, y)`.
		bool at(s16 x, s16 y, u8 frame) {
			if (!active || sheet == nullptr || frame >= sheet->bob().frame_count) {
				return false;
			}
			const graphics::Bob& bob = sheet->bob();
			const s16 wx = static_cast<s16>(x & ~15);
			const u16* base = reinterpret_cast<const u16*>(
				bob.sheet.address(static_cast<u32>(frame) * bob.frame_stride).cptr());
			u8* dst = target.data() + static_cast<u32>(start_row) * static_cast<u32>(y) +
				  (static_cast<u32>(wx < 0 ? 0 : wx) >> 3u);
			if (op == graphics::BlobOp::CookieCut) {
				stream.one(stream.ctx, base + words, base, dst, static_cast<u8>(x & 15));
			} else {
				stream.one(stream.ctx, base, base, dst, static_cast<u8>(x & 15));
			}
			return true;
		}
		/// Espera al último blit de la racha.
		bool done() {
			if (!active) {
				return false;
			}
			active = false;
			return stream.end(stream.ctx);
		}
	};

	/// Abre una racha de estampado; el `BlobOp` sale de la política del sprite (`Or`, `Opaque` o
	/// `CookieCut`). `false`/inactivo si el backend no la soporta o el sprite no vale.
	[[nodiscard]] StampRun stamp(const graphics::Sprite& sheet) {
		StampRun run {};
		if (!m_stream.valid() || !sheet.valid()) {
			return run;
		}
		const graphics::BobTarget bt = m_target.bob_target();
		const graphics::Bob& bob = sheet.bob();
		if (bob.sheet.empty() || bt.planes.empty() || bob.width < 16u) {
			return run;
		}
		const u16 words = static_cast<u16>(bob.width / 16u);
		const s16 amod = static_cast<s16>(words * 2u);
		const s16 dmod = static_cast<s16>(static_cast<s32>(bt.row_bytes) - static_cast<s32>(words) * 2);
		const u16 height = static_cast<u16>(bob.height * bob.planes);
		graphics::BlobOp op = graphics::BlobOp::CookieCut;
		if (bob.draw == graphics::BobDraw::Or) {
			op = graphics::BlobOp::Or;
		} else if (bob.draw == graphics::BobDraw::Opaque) {
			op = graphics::BlobOp::Opaque;
		}
		if (!m_stream.begin(m_stream.ctx, op, words, height, amod, amod, dmod, dmod)) {
			return run;
		}
		run.stream = m_stream;
		run.sheet = &sheet;
		run.target = bt;
		run.op = op;
		run.words = words;
		run.start_row = static_cast<s32>(bt.row_bytes) * bt.plane_count;
		run.active = true;
		return run;
	}

	/// **Borra ahora** (streaming, `D = 0`) la banda completa de `box` (filas completas de todos los
	/// planos, interleaved). Complementa a `stamp`; ambas emiten **en orden**, sin `FramePlan`.
	bool clear_now(Box box) {
		if (!m_stream.valid() || box.empty()) {
			return false;
		}
		const graphics::BobTarget bt = m_target.bob_target();
		if (bt.planes.empty()) {
			return false;
		}
		const u16 words = static_cast<u16>((box.w + 15u) / 16u);
		if (words == 0u) {
			return false;
		}
		const u16 height = static_cast<u16>(box.h * bt.plane_count);
		// Fila completa de un plano → filas interleaved contiguas: un solo blit D-only, `dmod = 0`.
		const bool full_row = (static_cast<u32>(words) * 2u == bt.row_bytes);
		const s16 dmod = full_row ? 0
					  : static_cast<s16>(static_cast<s32>(bt.row_bytes) *
									     bt.plane_count -
								     static_cast<s32>(words) * 2);
		u8* dst = bt.data() + static_cast<u32>(box.y) * static_cast<u32>(bt.row_bytes) *
					      static_cast<u32>(bt.plane_count);
		if (!m_stream.begin(m_stream.ctx, graphics::BlobOp::Clear, words, height, 0, 0, 0, dmod)) {
			return false;
		}
		m_stream.one(m_stream.ctx, nullptr, nullptr, dst, 0u);
		return m_stream.end(m_stream.ctx);
	}

	/// **Copia un rect ahora** (streaming, `D = C`): `words`×`height` palabras de `src` a `dst` con
	/// módulos `cmod`/`dmod` (bytes). Para desplazar/copiar una banda fuera del plan (scroll).
	bool copy_now(const void* src, void* dst, u16 words, u16 height, s16 cmod, s16 dmod) {
		if (!m_stream.valid() || src == nullptr || dst == nullptr || words == 0u || height == 0u) {
			return false;
		}
		if (!m_stream.begin(m_stream.ctx, graphics::BlobOp::Copy, words, height, 0, 0, cmod, dmod)) {
			return false;
		}
		m_stream.one(m_stream.ctx, src, nullptr, dst, 0u);
		return m_stream.end(m_stream.ctx);
	}

private:
	field::DrawTarget m_target;
	BlitStream m_stream {};
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
		const auto resources = scene_resources();
		if (!graphics::composition::validate(resources, graphics::composition::ocs_a500).ok())
			return util::unexpected(StartError::InvalidDisplay);
		if (graphics::composition::chip_bytes_for(resources) > m_memory->chip().free_bytes())
			return util::unexpected(StartError::OutOfMemory);
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
		materialize_world_layers();
		return m_world_materialization_ok;
	}

	/// **Mundo retenido** del juego: sus capas Fill se materializan con la cámara antes de render.
	/// Capas de actores/tilemaps se conservan para sus caminos específicos.
	[[nodiscard]] scene::World<GameDisplay::kWorldLayerCapacity>& world() noexcept { return m_world; }
	[[nodiscard]] const scene::World<GameDisplay::kWorldLayerCapacity>& world() const noexcept { return m_world; }

	/// **Planner (actores)**: emite los actores del `world()` al plan del frame con el clip y
	/// el destino del contexto de dibujo de la escena ligada. Devuelve cuántos se dibujaron.
	/// Llámalo antes de `present()`. Los actores se dibujan sobre el fondo Fill; las capas de
	/// playfield/tilemap todavía esperan su driver de materialización.
	eng::u16 draw_world() {
		if (!m_scene.valid()) {
			return 0u;
		}
		field::DrawTarget target = m_scene.get()->draw_target(&m_plan);
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
	static constexpr u32 kSizeLimit = 0xffffffffu;
	static constexpr u16 kBitmapAlignmentBytes = 16u;
	static constexpr u16 kWorldCoordinateLimit = 0x7fffu;

	[[nodiscard]] graphics::composition::SceneResources scene_resources() const noexcept {
		auto resources = graphics::composition::planar(m_display.width, m_display.height,
								 m_display.color_depth);
		resources.buffers = m_display.buffers;
		resources.layout = m_display.layout;
		return resources;
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
			self->m_game.update(*self); // el juego consume `port()` aquí
		}
		void render(Backend&, GameContext& ctx) {
			self->m_context = ctx;
			self->begin_async_frame();
			self->m_world_materialization_ok = true;
			if (self->m_scene.valid()) self->materialize_world_layers();
			self->m_game.render(*self);
		}
	};

	void materialize_world_layers() {
		Screen background = screen();
		m_world_materialization_ok = m_world.materialize_fill_layers(
			[&](const Box& bounds, u8 color) {
				const bool cleared = background.fill(bounds, 0u);
				const bool colored = background.fill(bounds, color);
				return cleared && colored;
			},
			m_display.width, m_display.height);
		if (m_world_materialization_ok) materialize_bitmap_layers();
	}

	void materialize_bitmap_layers() {
		Screen background = screen();
		m_world_materialization_ok = m_world.materialize_bitmap_layers(
			[&](u16 x, u16 y, ByteView<TextureTag> row) {
				usize i = 0u;
				while (i < row.size()) {
					const u8 color = row[i];
					usize end = i + 1u;
					while (end < row.size() && row[end] == color) ++end;
					if (!background.fill(Box {static_cast<s16>(x + i), static_cast<s16>(y),
								  static_cast<u16>(end - i), 1u}, color)) return false;
					i = end;
				}
				return true;
			},
			m_display.width, m_display.height);
		if (!m_world_materialization_ok) return;
		m_world_materialization_ok = m_world.materialize_bitmap_layers(
			[&](u16 x, u16 y, ByteView<TextureTag> row) {
				usize i = 0u;
				while (i < row.size()) {
					const u8 color = row[i];
					usize end = i + 1u;
					while (end < row.size() && row[end] == color) ++end;
					if (!background.fill(Box {static_cast<s16>(x + i), static_cast<s16>(y),
								  static_cast<u16>(end - i), 1u}, color)) return false;
					i = end;
				}
				return true;
			},
			m_display.width, m_display.height);
	}

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
	input::InputAggregator m_input {};                  ///< entrada del frame (la lee/rellena el juego)
	graphics::FramePlan m_plan {};
	u32 m_frame = 0;
	bool m_display_bound_from_scene = false;
	u8 m_fine_scroll_request = 0u;  ///< fine-scroll pedido por el juego (aplica en VBlank)
	bool m_fine_scroll_pending = false;
};

} // namespace eng
