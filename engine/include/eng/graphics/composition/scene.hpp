#pragma once

/// \file scene.hpp
/// **Escena de composición** (`composition::Scene`): posee los bitplanes y la copperlist y
/// expone el emisor de Copper, el objetivo de dibujo y el parcheo de punteros. Es la pieza
/// del **plano de recursos** del modelo de tres planos. Las **etapas** (display/palette/…) y
/// el preset `compose` viven en `stages.hpp`; `compose.hpp` es la cabecera de familia.
/// Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.
#include <eng/core/math/arith.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/function_ref.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/hw/info.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::composition {

/// **Tarea del ciclo de vida** (plano de comportamiento): referencia **no propietaria** a
/// un callable sin argumentos (`eng::util::FunctionRef<void()>`, el análogo de
/// `std::function_ref` del engine). Admite lambdas/functors directamente
/// (`scene.on_frame([&]{ ... })` con el lambda **con nombre**: la referencia no lo copia).
/// El callable debe vivir más que la escena (buffer del llamador): sin heap ni copia del
/// cierre. `std::function` no está disponible (freestanding, sin heap, sin excepciones).
using Task = eng::util::FunctionRef<void()>;

/// **Layout de los bitplanes** en memoria.
// (`SceneLayout` y `SceneResources` viven en `limits.hpp`, el contrato de display que
// `validate()` necesita conocer; se reexportan aquí por conveniencia.)

/// **Recursos** que una escena planar necesita (plano de recursos del modelo).
// (definidos en `limits.hpp`)

/// Máximos del modelo de escena (capacidad fija, sin heap).
inline constexpr u8 kMaxSceneBuffers = 3; ///< buffers de display
inline constexpr u8 kMaxScenePlanes = 6;  ///< planos de bitplane

/// **Valor de 32 bits parcheable** = pareja de MOVEs (`hi`/`lo`), p. ej. un puntero
/// `BPLxPTH` + `BPLxPTL` o un parámetro de 32 bits. Es el caso **multi-registro** de la base
/// común: un valor lógico que ocupa varios MOVEs.
///
/// No guarda el emisor: los índices se aplican sobre la **lista activa** del `Plan`
/// (`Scene::patch_plane_pointers`), que es el bloque que el Copper ejecuta. Guardar un
/// `PatchHandle` (ligado al `Scheduler`) no serviría aquí: `Plan::begin_frame`/`end_frame`
/// reorientan el emisor al bloque inactivo, así que el parche caería en el bitmap equivocado.
struct Patch32 {
	u16 hi_index = 0; ///< índice del MOVE de la mitad alta (p. ej. `BPLxPTH`)
	u16 lo_index = 0; ///< índice del MOVE de la mitad baja (p. ej. `BPLxPTL`)

	/// Escribe `value` repartido en `hi`/`lo` sobre `words` (la lista activa).
	void apply(u16* words, eng::u32 value) const {
		words[hi_index + 1u] = static_cast<u16>(value >> 16);
		words[lo_index + 1u] = static_cast<u16>(value & 0xffffu);
	}
};

/// Construye un `Patch32` a partir del índice del MOVE `hi` (el `lo` va 2 words después).
[[nodiscard]] inline Patch32 patch32_at(copper::Scheduler&, u16 hi_index) {
	return Patch32 {hi_index, static_cast<u16>(hi_index + 2u)};
}

/// Escena viva: posee los bitplanes (contiguos) y la copperlist, y el emisor de Copper.
/// No reserva al sistema más que a través de la `MemorySystem` del backend.
class Scene {
public:
	/// **Efecto de escena**: callable que recibe la escena y aporta intenciones/trabajos al
	/// plan del frame. Ver `docs/engine/architecture/EFFECT_MODEL.md`.
	using EffectFn = eng::util::FunctionRef<void(Scene&)>;
	/// Máximo de efectos registrados por escena (capacidad fija, sin heap).
	static constexpr u8 kMaxEffects = 8;

	/// Crea la escena reservando los bitplanes (según `res`) y la copperlist en Chip RAM,
	/// **validando antes** `res` contra las capacidades de `limits` (perfil de la máquina).
	/// El motivo del rechazo queda en `config_error()`. Devuelve `false` si no es válida o no
	/// hay memoria. El perfil es **obligatorio**: no existe una vía que acepte configuraciones
	/// que el hardware no permite.
	bool init(MemorySystem& memory, const SceneResources& res, const DisplayLimits& limits) {
		const ConfigError e = validate(res, limits);
		if (!e.ok()) {
			m_config_error = e;
			return false;
		}
		return init_raw(memory, res);
	}

	/// Abre la construcción del programa (retarget del emisor al bloque trasero).
	void begin_build() { m_plan.begin_frame(); }
	/// Cierra el programa (orden + presupuesto) y voltea el buffer. `false` si no cupo.
	[[nodiscard]] bool end_build() { return m_plan.end_frame(); }

	/// Emisor de Copper de esta escena (las etapas emiten por aquí).
	[[nodiscard]] copper::Scheduler& scheduler() { return m_plan.scheduler(); }
	/// Emisor de Copper (versión const, solo lectura).
	[[nodiscard]] const copper::Scheduler& scheduler() const { return m_plan.scheduler(); }
	/// Programa de Copper de la escena (orden + presupuesto de las intenciones).
	[[nodiscard]] copper::Plan& plan() { return m_plan; }
	/// Programa de Copper (versión const, solo lectura).
	[[nodiscard]] const copper::Plan& plan() const { return m_plan; }
	/// Vista de los bitplanes del buffer trasero (el que se está dibujando).
	[[nodiscard]] constexpr eng::PlaneBytes bitplanes() const { return m_buffers[m_back].view; }
	/// Vista **certificada en Chip** de los bitplanes del buffer trasero: fuente para el **DMA**
	/// (Copper/`BPLxPT`). Sale del `Block<Tag>` del buffer (medio en la arena) con la comprobación
	/// de que es Chip; no se fabrica una dirección a mano.
	[[nodiscard]] eng::ChipPlaneView chip_planes() const { return m_buffers[m_back].mem_view_chip(); }
	/// Buffer de display que se está dibujando (el trasero).
	[[nodiscard]] constexpr eng::PlaneBytes back() const { return m_buffers[m_back].view; }
	/// Buffer de display `i` (para leer/escribir otro); vacío si `i` fuera de rango.
	[[nodiscard]] constexpr eng::PlaneBytes buffer(u8 i) const {
		return (i < m_buffer_count) ? m_buffers[i].view : eng::PlaneBytes {};
	}
	/// Nº de buffers de display en uso (1 = simple, 2/3 = doble/triple).
	[[nodiscard]] constexpr u8 buffer_count() const { return m_buffer_count; }

	/// Indice del buffer trasero (el que se dibuja y que `commit()` publicara).
	[[nodiscard]] constexpr u8 back_index() const { return m_back; }

	/// Dirección **efectiva** del plano `p` en la copperlist activa (la que el Copper
	/// ejecuta ahora): lee el `BPLxPT` (par hi/lo) en el punto donde `display` lo emitió.
	/// Es la forma de observar el doble buffer sin recorrer la lista a mano. `0` si el
	/// plano no tiene puntero parcheable o está fuera de rango.
	[[nodiscard]] u32 display_plane_address(u8 p) const {
		if (p >= m_res.planes || p >= kMaxScenePlanes) {
			return 0u;
		}
		const Patch32& patch = m_plane_patch[p];
		if (patch.hi_index == 0u && patch.lo_index == 0u) {
			return 0u;
		}
		const u16* words = m_plan.active_words();
		return (static_cast<u32>(words[patch.hi_index + 1u]) << 16) |
		       static_cast<u32>(words[patch.lo_index + 1u]);
	}

	/// `true` si el `BPLxPT` del registro `p` apunta al **plano de bitmap** `source` del buffer
	/// `index` (verificación de doble buffer sin tocar punteros raw). El plano fuente por
	/// defecto es `p`; `reverse_ptrs` lo permuta. `false` si no es parcheable o fuera de rango.
	[[nodiscard]] bool display_plane_uses(u8 p, u8 index) const {
		if (p >= m_res.planes || p >= kMaxScenePlanes || index >= m_buffer_count) {
			return false;
		}
		const u8 src = (m_plane_source[p] < m_res.planes) ? m_plane_source[p] : p;
		const eng::ChipPlaneView planes = m_buffers[index].mem_view_chip();
		const eng::Address<eng::MemoryKind::Chip> want =
			planes.address(static_cast<eng::s32>(static_cast<eng::u32>(src) * m_plane_bytes));
		return display_plane_address(p) == static_cast<u32>(want.value);
	}
	/// Plano `i` del bitmap (layout contiguo; vacío si fuera de rango).
	[[nodiscard]] constexpr eng::PlaneBytes plane(u8 i) const {
		return (i < m_res.planes)
			? bitplanes().subspan(eng::math::mulu32x16(m_plane_bytes, static_cast<u16>(i)),
					      m_plane_bytes)
			: eng::PlaneBytes {};
	}
	/// Bytes de un plano completo (`row_bytes * alloc_rows`).
	[[nodiscard]] constexpr u32 plane_bytes() const { return m_plane_bytes; }
	/// Bytes por fila de un plano (`width/8`, **redondeado a 4**). El padding a 4 deja
	/// las filas alineadas para la copia CPU de 32 bits (`copy_rect_cpu`).
	[[nodiscard]] constexpr u16 row_bytes() const {
		return static_cast<u16>(((m_res.width / 8u) + 3u) & ~3u);
	}
	/// Nº de planos de bitplane de la escena.
	[[nodiscard]] constexpr u8 planes() const { return m_res.planes; }
	/// Ancho visible en píxeles.
	[[nodiscard]] constexpr u16 width() const { return m_res.width; }
	/// Alto del display en filas.
	[[nodiscard]] constexpr u16 height() const { return m_res.height; }
	/// Filas lógicas del bitmap (`rows` o, si es 0, `height`).
	[[nodiscard]] constexpr u16 rows() const { return m_res.rows != 0u ? m_res.rows : m_res.height; }
	/// Disposición de los bitplanes (contiguos o interleaved).
	[[nodiscard]] constexpr SceneLayout layout() const { return m_res.layout; }
	/// **Geometría de BOB** del bitmap de dibujo activo (base del plano 0, fila de un plano,
	/// separación entre planos y layout). La consume `Sprite::draw` a través del
	/// `DrawTarget`; el juego nunca la construye a mano. Ver `PUBLIC_GAME_API.md` §2.1.1.
	[[nodiscard]] graphics::BobTarget bob_target() const {
		graphics::BobTarget t {};
		t.base = bitplanes().data();
		t.row_bytes = row_bytes();
		t.plane_bytes = plane_bytes();
		t.planes = planes();
		t.layout = (m_res.layout == SceneLayout::Interleaved)
				   ? graphics::BobLayout::Interleaved
				   : graphics::BobLayout::Planar;
		return t;
	}
	/// Recursos de la escena (geometría, modo, layout, buffers) tal como se configuraron.
	[[nodiscard]] constexpr const SceneResources& resources() const { return m_res; }
	/// Playfield (solo layout interleaved): base de `surface()`.
	[[nodiscard]] field::CanvasPlayfield& playfield() { return m_playfield; }
	/// Playfield (versión const, solo lectura).
	[[nodiscard]] const field::CanvasPlayfield& playfield() const { return m_playfield; }
	/// Superficie de dibujo con clip (cualquier layout): `Surface` enruta por el mapeo
	/// del playfield, así que la app dibuja igual sobre contiguo o interleaved sin ver
	/// planos ni punteros. En contiguo con varios buffers apunta al **trasero** (el que
	/// se publica en `commit`).
	[[nodiscard]] field::Surface surface() {
		field::Playfield& pf = (m_res.layout == SceneLayout::Interleaved)
					       ? static_cast<field::Playfield&>(m_playfield)
					       : static_cast<field::Playfield&>(m_contiguous);
		return field::Surface {pf, field::SurfaceRect {0, 0, m_res.width, m_res.height}};
	}
	/// `true` si la construcción de la copperlist cupo en el presupuesto.
	[[nodiscard]] bool ok() const { return m_plan.ok(); }

	/// Motivo del último rechazo de configuración (`init` con `DisplayLimits`); `ok()` si
	/// la escena se creó. Útil para diagnóstico de una config inválida.
	[[nodiscard]] constexpr ConfigError config_error() const { return m_config_error; }
	/// Informe del plan (desbordes, zonas pesadas) para diagnóstico.
	[[nodiscard]] const copper::ScheduleReport& report() const { return m_plan.report(); }
	/// Palabras de Copper usadas por el programa.
	[[nodiscard]] constexpr u16 words() const { return m_plan.words(); }

	/// **Display vigente** derivado de los recursos de la escena (modo, planos, geometría). El
	/// perfil de composición es lores (sin hires/lace); HAM/EHB salen del `SceneMode`.
	[[nodiscard]] constexpr hw::DisplayInfo display_info() const {
		return hw::make_display(m_res.width, m_res.height, m_res.planes, false, false,
					m_res.mode == SceneMode::Ham,
					m_res.mode == SceneMode::Ehb);
	}

	/// Liga un inventario de hardware (`eng::hw`) y **declara el modo vigente** de la escena en
	/// él (`hw::set_display`). Desde aquí, `init` lo vuelve a publicar al configurar los recursos,
	/// de modo que el juego lee el display real por `HwInfo::display` sin conocer registros.
	void bind_hw_info(hw::HwInfo& info) {
		m_hw_info = info;
		publish_display();
	}

	/// Publica el display vigente en el `HwInfo` ligado (si lo hay). Lo llama `init`.
	void publish_display() {
		if (m_hw_info.valid()) {
			hw::set_display(*m_hw_info.get(), display_info());
		}
	}

	/// **Elige el rasterizador** (CPU/Blitter) de `surface()` y su política. El backend
	/// declara sus `RasterCaps`; la app decide el `RasterPolicy` (`Auto`/`Cpu`/`Blitter`).
	/// Vacío deja el CPU por defecto. No cambia la API de dibujo.
	void set_raster(eng::Ref<field::Rasterizer> r, const field::RasterPolicy& policy = {}) {
		m_playfield.set_rasterizer(r);
		m_playfield.set_raster_policy(policy);
		m_contiguous.set_rasterizer(r);
		m_contiguous.set_raster_policy(policy);
	}

	/// Instala el **motor de relleno por hardware** (Blitter) en los playfields de la
	/// escena. Lo usa el backend (`PolygonFillService`); `BlitterRaster` lo aprovecha.
	void set_polygon_fill_sink(field::PolygonFillSink sink) {
		m_playfield.set_polygon_fill_sink(sink);
		m_contiguous.set_polygon_fill_sink(sink);
	}

	/// Instala el **motor de relleno de rect por hardware** (Blitter D-only) en los playfields
	/// de la escena. `BlitterRaster` lo usa para las cajas de UI (más barato que el polígono).
	void set_rect_fill_sink(field::RectFillSink sink) {
		m_playfield.set_rect_fill_sink(sink);
		m_contiguous.set_rect_fill_sink(sink);
	}

	/// **Objetivo de dibujo** de la escena: `Surface` + `Rasterizer` + `FramePlan` + clip.
	/// Es la puerta única a las primitivas (fill/línea/texto/blit/c2p) sobre el buffer de
	/// dibujo activo, sea la escena contigua o interleaved.
	[[nodiscard]] field::DrawTarget draw_target(eng::Ref<graphics::FramePlan> plan = {}) {
		const bool interleaved = (m_res.layout == SceneLayout::Interleaved);
		field::Playfield& pf = interleaved
					       ? static_cast<field::Playfield&>(m_playfield)
					       : static_cast<field::Playfield&>(m_contiguous);
		return field::DrawTarget {surface(), pf.rasterizer(), plan, bob_target()};
	}

	/// **Chunky→planar** a través del rasterizador de la escena: con `BlitterRaster` y
	/// un `plan` encola un `BlitJobKind::C2P` (el backend ejecuta las 13 fases); con el
	/// rasterizador CPU convierte ya sin usar `plan`.
	[[nodiscard]] bool c2p(const field::C2pRequest& req,
			       eng::Ref<graphics::FramePlan> plan = {}) {
		return draw_target(plan).c2p(req);
	}

	/// Toma el control mostrando el buffer 0 (una vez).
	template <typename Backend>
	void takeover(Backend& backend) {
		patch_plane_pointers(0u);
		m_plan.takeover(backend);
	}

	/// Publica el buffer dibujado (`m_back`) repuntando los `BPLxPT` y avanza. La lista ya
	/// está instalada; solo se parchean los punteros (doble buffer por `BPLxPT`). Sin
	/// libcalls: el avance es comparación (no `%`) y el recorrido de planos avanza el
	/// puntero (no `p * plane_bytes`).
	void commit() {
		patch_plane_pointers(m_back);
		++m_back;
		if (m_back >= m_buffer_count) {
			m_back = 0u;
		}
		// Contiguo con varios buffers: el lienzo de dibujo sigue al trasero.
		if (m_res.layout == SceneLayout::Contiguous && m_buffer_count > 1u) {
			(void)m_contiguous.bind(m_buffers[m_back], m_res.width, m_res.height,
						m_res.planes, m_plane_bytes);
		}
	}

	/// Words de la copperlist **activa** (la que el Copper ejecuta) y **inactiva** (la que se
	/// está construyendo). Las usa una etapa de Copper (p. ej. `copper_chunky`) para exponer
	/// los `data` de las instrucciones que emite.
	[[nodiscard]] constexpr const u16* active_words() const { return m_plan.active_words(); }
	[[nodiscard]] constexpr const u16* inactive_words() const { return m_plan.inactive_words(); }

	/// Publica la copperlist recién construida con `end_build` (la instala en el backend por
	/// `COP1LC`). La ruta **planar** no lo necesita (lista estática + parcheo de `BPLxPT`); la
	/// usa el modo **copper chunky**, cuya lista cambia cada frame.
	template <typename Backend>
	void present(Backend& backend) {
		m_plan.commit(backend);
	}

	/// Voltea el bloque de copperlist activo/inactivo **sin re-emitir**. Para un modo que
	/// preconstruye la estructura en ambos bloques y por frame solo parchea datos (copper
	/// chunky): escribir los colores en `inactive_words()` → `flip_copper()` → `present`.
	void flip_copper() { m_plan.flip(); }

	/// Registra el parcheo de 32 bits del puntero `BPLxPT` del plano `p` (lo llama `display`).
	void set_plane_patch(u8 p, Patch32 patch) {
		if (p < kMaxScenePlanes) {
			m_plane_patch[p] = patch;
			m_plane_source[p] = p; // por defecto, el registro `p` muestra el plano `p`
		}
	}

	/// Registra el parcheo del registro `BPLxPT` `p` pero haciendo que muestre el **plano
	/// de bitmap** `source` (permutación, p. ej. `reverse_ptrs`: `bpl[N-1..0]`). Sin esto,
	/// `commit` repuntaría el registro `p` al plano `p` y desharía la permutación.
	void set_plane_patch_source(u8 p, u8 source, Patch32 patch) {
		if (p < kMaxScenePlanes) {
			m_plane_patch[p] = patch;
			m_plane_source[p] = source;
		}
	}

	// --- Ciclo de vida (plano de comportamiento) ------------------------------------
	/// Liga la tarea de **setup** (una vez, tras `init`).
	Scene& on_setup(Task t) { m_setup = t; return *this; }
	/// Liga la tarea de **frame** (una vez por `tick`): el trabajo por frame.
	Scene& on_frame(Task t) { m_frame = t; return *this; }
	/// Liga la tarea de **teardown** (al desmontar la escena).
	Scene& on_teardown(Task t) { m_teardown = t; return *this; }
	/// Ejecuta la tarea de setup (si la hay).
	void setup() { run(m_setup); }
	/// Ejecuta los **efectos** y la tarea de frame (si las hay). **Una vez por frame.**
	void tick() {
		run_effects();
		run(m_frame);
	}
	/// Ejecuta la tarea de teardown (si la hay).
	void teardown() { run(m_teardown); }

	// --- Efectos (aportan intenciones/trabajos al plan del frame) -------------------
	/// Registra un **efecto**: callable `void(Scene&)` que aporta al `plan()` del frame.
	/// Se ejecutan en orden de registro (dentro de `tick()`, tras `begin_build()`).
	///
	/// El callable debe **sobrevivir** a la escena (`EffectFn` es un `FunctionRef` **no
	/// propietario**): pasa un **functor miembro** (como `FrameTask` en
	/// `081_background_tasks`), no una lambda temporal. El `static_assert` lo impide en
	/// compilación en vez de dejar una referencia colgante.
	template <class F>
	Scene& add_effect(F&& fn) {
		static_assert(!eng::util::is_rvalue_reference_v<F&&>,
			      "add_effect: el callable debe sobrevivir a la escena; usa un functor miembro, no una lambda temporal");
		const EffectFn ref { fn };
		if (m_effect_count < kMaxEffects && ref.valid()) {
			m_effects[m_effect_count++] = ref;
		}
		return *this;
	}
	/// Ejecuta los efectos registrados, en orden de registro.
	void run_effects() {
		for (u8 i = 0; i < m_effect_count; ++i) {
			m_effects[i](*this);
		}
	}
	/// Nº de efectos registrados.
	[[nodiscard]] constexpr u8 effect_count() const { return m_effect_count; }

private:
	/// Invoca una `Task` solo si es válida (no nula).
	static void run(Task t) {
		if (t.valid()) {
			t();
		}
	}

	/// Reserva bitplanes y copperlist según `res`. Interno: solo lo llama `init(...)` tras
	/// validar `res` contra el perfil. Devuelve `false` si la geometría o la memoria fallan.
	bool init_raw(MemorySystem& memory, const SceneResources& res) {
		m_res = res;
		const u16 row = row_bytes();
		const u16 logical_rows = res.rows != 0u ? res.rows : res.height;
		if (row == 0u || res.height == 0u) {
			return false;
		}
		// Copper chunky: sin bitplanes (el color lo escribe el Copper). No se reservan planos
		// ni se enlaza playfield; solo la copperlist (grande).
		const bool copper_chunky = (res.planes == 0u);
		const u16 alloc_rows = (res.layout == SceneLayout::Interleaved) ? res.height : logical_rows;
		m_plane_bytes = copper_chunky
					? 0u
					: eng::math::mulu32x16(static_cast<u32>(row), alloc_rows);
		// Doble/triple buffer solo en layout contiguo (la doble buffer se hace parcheando los
		// BPLxPT; el interleaved usa un único `CanvasPlayfield`).
		u8 buffers = copper_chunky ? 1u : res.buffers;
		if (res.layout == SceneLayout::Interleaved || buffers < 1u) {
			buffers = 1u;
		}
		if (buffers > kMaxSceneBuffers) {
			buffers = kMaxSceneBuffers;
		}
		m_buffer_count = buffers;
		if (copper_chunky) {
			m_back = 0u;
		} else {
			for (u8 b = 0u; b < buffers; ++b) {
				m_buffers[b] = memory.chip.allocate_block<eng::PlaneTag>(
					eng::math::mulu32x16(m_plane_bytes, static_cast<u16>(res.planes)) + 16u, 16);
				if (!m_buffers[b].valid()) {
					return false;
				}
			}
			m_back = (buffers > 1u) ? 1u : 0u;
			if (res.layout == SceneLayout::Interleaved) {
				if (!m_playfield.bind(m_buffers[0],
						     field::CanvasPlayfield::Config {res.width, res.height, res.planes})) {
					return false;
				}
			} else if (!m_contiguous.bind(m_buffers[m_back], res.width, res.height, res.planes,
						      m_plane_bytes)) {
				return false;
			}
		}
		copper::PlanConfig pcfg {};
		pcfg.copper_bytes = res.copper_bytes;
		pcfg.first_line = res.first_line;
		if (!m_plan.begin(memory, pcfg)) {
			return false;
		}
		publish_display(); // declara el modo vigente en el HwInfo ligado (si lo hay)
		return true;
	}

	/// Repunta los `BPLxPT` al buffer `index` parcheando el copper. No aplica al interleaved
	/// (usa un único `CanvasPlayfield`). Corre una vez por `commit` (no es hot path), así que
	/// el offset del plano va con multiplicación de 32 bits normal (un `mulu16` desbordaría si
	/// `src * plane_bytes` supera 65535).
	void patch_plane_pointers(u8 index) {
		if (m_res.layout == SceneLayout::Interleaved || index >= m_buffer_count) {
			return;
		}
		const eng::u8* addr = m_buffers[index].view.data();
		u16* words = m_plan.active_words();
		for (u8 p = 0u; p < m_res.planes; ++p) {
			const u8 src = (m_plane_source[p] < m_res.planes) ? m_plane_source[p] : p;
			const eng::u32 off = eng::math::mulu32x16(m_plane_bytes, static_cast<u16>(src));
			m_plane_patch[p].apply(words, static_cast<eng::u32>(
							 reinterpret_cast<eng::uintptr>(addr + off)));
		}
	}

	SceneResources m_res {}; ///< geometría/recursos de la escena (copiados en `init`)
	eng::util::Array<eng::Block<eng::PlaneTag>, kMaxSceneBuffers> m_buffers {}; ///< buffers de bitplanes (Chip)
	field::CanvasPlayfield m_playfield {}; ///< playfield del layout interleaved (base de `surface()`)
	field::ContiguousPlayfield m_contiguous {}; ///< playfield del layout contiguo (base de `surface()`)
	copper::Plan m_plan {}; ///< programa de Copper (lista + presupuesto + emisor)
	u32 m_plane_bytes = 0; ///< bytes de un plano completo (`row_bytes * alloc_rows`)
	u8 m_buffer_count = 1; ///< buffers de display en uso (1..`kMaxSceneBuffers`)
	u8 m_back = 0; ///< índice del buffer trasero (el que se dibuja/publica)
	Patch32 m_plane_patch[kMaxScenePlanes] {}; ///< parcheo `BPLxPT` por registro (doble/triple buffer)
	u8 m_plane_source[kMaxScenePlanes] {}; ///< qué plano de bitmap muestra cada registro `BPLxPT`
	Task m_setup {}; ///< tarea de setup (una vez)
	Task m_frame {}; ///< tarea de frame (por `tick`)
	Task m_teardown {}; ///< tarea de teardown
	eng::util::Array<EffectFn, kMaxEffects> m_effects {}; ///< efectos (orden de registro)
	u8 m_effect_count = 0; ///< nº de efectos registrados
	ConfigError m_config_error {}; ///< motivo del último rechazo de configuración (vacío = ok)
	eng::Ref<hw::HwInfo> m_hw_info {}; ///< inventario ligado (no propietario); publica el display
};

} // namespace eng::graphics::composition
