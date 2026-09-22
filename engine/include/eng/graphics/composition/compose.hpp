#pragma once

/// \file compose.hpp
/// **Composición de escenas** (prototipo del modelo de tres planos): una escena se construye
/// uniendo **etapas** que piden recursos y emiten Copper, en vez de una clase por driver.
/// Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.
///
/// Uso:
///
/// ```cpp
/// composition::Scene s;
/// composition::compose(s, memory, composition::planar4(320, 256),
///     composition::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200),
///     composition::palette(pal, 0, 16));
/// s.install(backend);        // o takeover
/// ```
///
/// Una etapa es un callable `void(Scene&)`: pide lo que necesita a la escena (geometría,
/// planos) y emite al `copper::Scheduler`. Los **presets** son funciones que devuelven un
/// `SceneResources`; no hay clase por efecto. El **programa** es data (Copper) que ejecuta
/// el chip; la **variabilidad** se hace con `copper::PatchHandle` (`scheduler().patchable`).
///
/// ```text
///   escena = unión de ETAPAS (no una clase por driver)
///   ┌──────────────────────────────────────────────────────────────────┐
///   │ composition::Scene                                                       │
///   │  recursos: geometría · planos · buffers · paleta                   │
///   │  ├─ display()        ─┐                                            │
///   │  ├─ palette()        ─┼─► piden recursos y emiten al copper::Scheduler
///   │  ├─ palette_zones()  ─┤                                            │
///   │  └─ reverse_ptrs()   ─┘                                            │
///   │  ciclo de vida: Task = FunctionRef<void()> (por frame)             │
///   │  variabilidad : copper::PatchHandle (scheduler().patchable)        │
///   └──────────────────────────────────────────────────────────────────┘
///   El PROGRAMA es data (Copper) que ejecuta el chip; los presets devuelven un SceneResources.
/// ```

#include <eng/core/arith.hpp>
#include <eng/core/domains.hpp>
#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/function_ref.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
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
		const eng::u8* base = m_buffers[index].view.data();
		const eng::uintptr want =
			reinterpret_cast<eng::uintptr>(base) + static_cast<eng::u32>(src) * m_plane_bytes;
		return display_plane_address(p) == static_cast<u32>(want);
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
		return field::DrawTarget {surface(), pf.rasterizer(), plan};
	}

	/// **Chunky→planar** a través del rasterizador de la escena: con `BlitterRaster` y
	/// un `plan` encola un `BlitJobKind::C2P` (el backend ejecuta las 13 fases); con el
	/// rasterizador CPU convierte ya sin usar `plan`.
	[[nodiscard]] bool c2p(const field::C2pRequest& req, graphics::FramePlan* plan = nullptr) {
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

/// **Recursos de una escena planar**, parametrizados (no hay preset por caso de uso: la
/// geometría y el layout los decide el llamador). `SceneResources` tiene valores por
/// defecto razonables para 320x256 y se sobrescriben los campos que hagan falta.
///
/// Escenarios de uso (ilustrativos; no son funciones, solo configuraciones):
///
/// ```cpp
/// // EHB 320x256 (6 planos) + `kBplcon0_Ehb`:
/// SceneResources e = planar(320, 256, 6);
///
/// // HAM6 con cuadruplicado de filas (64 filas lógicas, 4 planos):
/// SceneResources h = planar(320, 256, 4);
/// h.rows = 64;                       // bitmap de 64 filas; `row_repeat(4, ...)` lo cuadruplica
///
/// // Lienzo interleaved con `surface()` para dibujo por primitivas:
/// SceneResources c = planar(320, 256, 4);
/// c.layout = SceneLayout::Interleaved;
///
/// // Doble/triple buffer de display:
/// SceneResources db = planar(320, 256, 4);
/// db.buffers = 2;                    // o 3
/// ```
[[nodiscard]] constexpr SceneResources planar(u16 width = 320, u16 height = 256,
					      u8 planes = 4) {
	SceneResources r {};
	r.width = width;
	r.height = height;
	r.planes = planes;
	return r;
}

/// Etapa de **punteros BPLxPT en orden inverso** (`bpl[N-1..0]`, como fire-rgb). Re-emite
/// los punteros tras `display`, de modo que el orden inverso (última escritura) manda.
[[nodiscard]] inline auto reverse_ptrs() {
	return [](Scene& sc) {
		copper::Scheduler& s = sc.scheduler();
		const eng::u8* base = sc.bitplanes().data();
		const u32 pb = sc.plane_bytes();
		for (eng::u8 p = 0u; p < sc.planes(); ++p) {
			const eng::u32 src = eng::math::mulu16(static_cast<u16>(sc.planes() - 1u - p),
							      static_cast<u16>(pb));
			const eng::uintptr ip = reinterpret_cast<eng::uintptr>(base + src);
			const u16 idx = s.move_at(copper::bitplane_pointer_high_register(p),
						  static_cast<u16>(ip >> 16));
			(void)s.move_at(copper::bitplane_pointer_low_register(p),
					static_cast<u16>(ip & 0xffffu));
			// Registra este MOVE como el parche del registro `p`, pero mostrando el plano
			// `planes-1-p` (la permutación inversa). Así `commit` repunta correctamente en
			// doble buffer sin deshacer la inversión.
			sc.set_plane_patch_source(p, static_cast<u8>(sc.planes() - 1u - p),
						  patch32_at(s, idx));
		}
	};
}

/// **`BPLCON0` habituales** (BPU + COLOR / HAM / EHB).
inline constexpr u16 kBplcon0_4Planes = 0x4200;      ///< 4 planos, COLOR
inline constexpr u16 kBplcon0_4PlanesNoColor = 0x4000; ///< 4 planos, sin COLOR
inline constexpr u16 kBplcon0_Ehb = 0x6200;          ///< EHB (6 planos, COLOR)
inline constexpr u16 kBplcon0_Ham6 = 0x7a00;         ///< HAM6 (6 planos, COLOR, HAM)

/// `BPLCON0` coherente con `mode` y `planes` (BPU = nº de planos, COLOR, y HAM/EHB si
/// aplica). Evita cablear el registro en cada demo; para el caso estándar de 4 planos da
/// `0x4200`. No cubre AGA (BPLCON3/FMODE aparte).
[[nodiscard]] constexpr u16 bplcon0_for(SceneMode mode, u8 planes) {
	const u16 bpu = static_cast<u16>((static_cast<u16>(planes) & 0x7u) << 12u);
	constexpr u16 kColor = 0x0200u; ///< bit COLOR (color indexado)
	constexpr u16 kHam = 0x0800u;   ///< bit HAM (HOMOD)
	// EHB **no** tiene bit propio en `BPLCON0` (OCS/ECS): se activa con **6 planos** y
	// `HOMOD = 0` (AHRM 3.ª, tabla de `BPLCON0`: «HOMOD=0 → EHB, solo si 6 bitplanes»). Por
	// eso `Ehb` y `Standard` comparten `bpu | COLOR` (equivale a `kBplcon0_Ehb = 0x6200`).
	switch (mode) {
		case SceneMode::Ham:
			return static_cast<u16>(bpu | kColor | kHam);
		case SceneMode::Ehb:
		default:
			return static_cast<u16>(bpu | kColor);
	}
}

/// Etapa de **display**: BPLCON0, DIW/DDF y punteros BPLxPT. Con layout `Interleaved` usa
/// los módulos del `CanvasPlayfield` (un plano por fila) y expone `surface()`; con
/// `Contiguous` usa `emit_planes_display` (un plano tras otro).
[[nodiscard]] inline auto display(u16 diwstrt, u16 diwstop, u16 ddfstrt, u16 ddfstop,
				  u16 bplcon0) {
	return [=](Scene& sc) {
		copper::Scheduler& s = sc.scheduler();
		if (sc.layout() == SceneLayout::Interleaved) {
			const field::PlayfieldHardwareView hv = sc.playfield().hardware_view();
			s.move(copper::Register::DMACON,
			       static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
						copper::DmaCopper | copper::DmaBitplane));
			s.move(copper::Register::BPLCON0, bplcon0);
			s.move(copper::Register::BPLCON1, 0x0000);
			s.move(copper::Register::BPLCON2, 0x0000);
			s.move(copper::Register::BPL1MOD, hv.bpl1mod);
			s.move(copper::Register::BPL2MOD, hv.bpl2mod);
			s.move(copper::Register::DIWSTRT, diwstrt);
			s.move(copper::Register::DIWSTOP, diwstop);
			s.move(copper::Register::DDFSTRT, ddfstrt);
			s.move(copper::Register::DDFSTOP, ddfstop);
			const u32 row = hv.bitmap_bytes_per_row;
			for (u8 p = 0u; p < sc.planes(); ++p) {
				const eng::uintptr addr = reinterpret_cast<eng::uintptr>(
					hv.bitplanes + static_cast<u32>(p) * row);
				s.move_bitplane_pointer(p, eng::ChipAddress {addr});
			}
		} else {
			s.move(copper::Register::DMACON,
			       static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
						copper::DmaCopper | copper::DmaBitplane));
			s.move(copper::Register::BPLCON0, bplcon0);
			s.move(copper::Register::BPLCON1, 0x0000);
			s.move(copper::Register::BPLCON2, 0x0000);
			s.move(copper::Register::BPL1MOD, 0x0000);
			s.move(copper::Register::BPL2MOD, 0x0000);
			s.move(copper::Register::DIWSTRT, diwstrt);
			s.move(copper::Register::DIWSTOP, diwstop);
			s.move(copper::Register::DDFSTRT, ddfstrt);
			s.move(copper::Register::DDFSTOP, ddfstop);
			// Punteros BPLxPT parcheables (uno por plano): habilitan el doble buffer por
			// parcheo (`Scene::commit`). `move_at` devuelve el índice del MOVE (PTH); el
			// PTL va 2 words después.
			const eng::u8* base = sc.bitplanes().data();
			for (u8 p = 0u; p < sc.planes(); ++p) {
				const eng::uintptr ip = reinterpret_cast<eng::uintptr>(
					base + static_cast<eng::u32>(p) * sc.plane_bytes());
				const u16 idx = s.move_at(copper::bitplane_pointer_high_register(p),
							  static_cast<u16>(ip >> 16));
				(void)s.move_at(copper::bitplane_pointer_low_register(p),
						static_cast<u16>(ip & 0xffffu));
				sc.set_plane_patch(p, patch32_at(s, idx));
			}
		}
	};
}

/// Etapa de **display** con geometría predefinida (`kPal320x256`).
[[nodiscard]] inline auto display(DisplayGeometry g, u16 bplcon0) {
	return display(g.diwstrt, g.diwstop, g.ddfstrt, g.ddfstop, bplcon0);
}

/// Etapa de **display** desde los recursos de la escena: deriva la geometría
/// (`geometry_for`) y usa `bplcon0_for(mode, planes)` si `bplcon0 == 0`. Es la forma
/// recomendada: la demo describe la escena y el `mode`, no los registros.
[[nodiscard]] inline auto display(const SceneResources& res, u16 bplcon0 = 0u) {
	const DisplayGeometry g = geometry_for(res);
	const u16 con = (bplcon0 != 0u) ? bplcon0 : bplcon0_for(res.mode, res.planes);
	return display(g.diwstrt, g.diwstop, g.ddfstrt, g.ddfstop, con);
}

/// Etapa de **paleta**: carga `count` colores desde `first`.
[[nodiscard]] inline auto palette(eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
	return [=](Scene& sc) { sc.scheduler().emit_palette(colors, first, count); };
}

/// Etapa de **intenciones de Copper** (capas dinámicas: paleta por línea, splits…). En vez
/// de emitir en orden de construcción, se las da al `Plan`, que las **ordena por scanline**
/// (y por prioridad) y las materializa en el punto actual de la lista.
[[nodiscard]] inline auto intents(eng::Span<const graphics::CopperIntent> list) {
	return [=](Scene& sc) {
		sc.plan().add(list.data(), static_cast<eng::u16>(list.size()));
		sc.plan().materialize();
	};
}

/// Zona de paleta por raster (franja horizontal).
struct PaletteZone {
	u8 line = 0;                 ///< línea de raster donde entra la zona
	eng::PaletteWords colors {}; ///< colores RGB444 de la zona
	u8 first = 0;                ///< primer color de la zona (`COLORfirst`)
	u8 count = 32;               ///< nº de colores de la zona
};

/// **Un MOVE parcheable dentro de una zona**: registro destino + valor inicial. Es la
/// unidad de la base común de toda modificación dinámica del copper (un color, un
/// `BPL1MOD` de scanline, un puntero `BPLxPT`, un `BPLCON1`…).
struct PatchSlot {
	copper::Register reg = copper::Register::COLOR00; ///< registro destino del MOVE
	u16 value = 0; ///< valor inicial escrito (un dato = una palabra)
};

/// **Grupo de MOVEs parcheables emitidos en una línea** (base común). Guarda el índice del
/// primer MOVE; cada slot es una instrucción de 2 words (MOVE + dato), de ahí el `+2*i`.
struct PatchZone {
	u8 line = 0;        ///< línea de raster donde se emite la zona
	u16 first_move = 0; ///< índice del primer MOVE de la zona en la copperlist
	u8 count = 0;       ///< nº de MOVEs (slots) de la zona

	/// Handle al slot `i` para parchearlo por frame (cualquier registro).
	[[nodiscard]] copper::PatchHandle handle(copper::Scheduler& s, u8 i) const {
		return s.patch_handle(static_cast<u16>(first_move + 2u * static_cast<u16>(i)));
	}
};

/// Etapa **genérica**: emite `WAIT(line)` + un MOVE por slot (con `move_at`) y escribe el
/// `PatchZone` resultante en `*out`. Sirve para paletas, offsets de scanline o cualquier
/// registro; las modificaciones dinámicas comparten esta base.
[[nodiscard]] inline auto patchable_zone(u8 line, eng::Span<const PatchSlot> slots,
					 PatchZone* out) {
	return [=](Scene& sc) {
		copper::Scheduler& s = sc.scheduler();
		s.wait_line(line);
		PatchZone z {};
		z.line = line;
		z.count = static_cast<u8>(slots.size());
		for (eng::usize i = 0; i < slots.size(); ++i) {
			const u16 idx = s.move_at(slots[i].reg, slots[i].value);
			if (i == 0u) {
				z.first_move = idx;
			}
		}
		if (out != nullptr) {
			*out = z;
		}
	};
}

/// Binding de una zona de **paleta** (caso particular de `PatchZone`).
using ZoneBinding = PatchZone;

/// Etapa de **zonas de paleta** (cambios por línea/banda). Si `out` no está vacío, escribe
/// el `PatchZone` de cada zona para parchear sus colores por frame; si está vacío, solo las
/// emite (conserva el informe de zona pesada del `Scheduler`). Unifica la versión estática y
/// la parcheable sobre la base común.
[[nodiscard]] inline auto palette_zones(eng::Span<const PaletteZone> zones,
					eng::Span<PatchZone> out = {}) {
	return [=](Scene& sc) {
		const bool record = !out.empty();
		const eng::usize total = record ? (zones.size() < out.size() ? zones.size() : out.size())
						: zones.size();
		for (eng::usize i = 0; i < total; ++i) {
			const PaletteZone& z = zones[i];
			u8 first = z.first;
			u8 count = z.count;
			if (first >= 32u) {
				continue;
			}
			if (static_cast<eng::u32>(first) + count > 32u) {
				count = static_cast<u8>(32u - first);
			}
			if (static_cast<eng::u32>(first) + count > z.colors.size()) {
				count = static_cast<u8>(z.colors.size() - first);
			}
			const u16 idx = sc.scheduler().emit_palette_zone_at(z.line, z.colors, first, count);
			if (record) {
				out[i] = PatchZone {z.line, idx, count};
			}
		}
	};
}

/// Etapa de **paleta base parcheable**: como `palette` pero escribe su `PatchZone` en `*out`
/// para reescribir los colores por frame (fundidos, ciclos).
[[nodiscard]] inline auto palette_patchable(eng::PaletteWords colors, u8 first, u8 count,
					    PatchZone* out) {
	return [=](Scene& sc) {
		const u16 idx = sc.scheduler().emit_palette_at(colors, first, count);
		if (out != nullptr) {
			*out = PatchZone {0u, idx, count};
		}
	};
}

/// Handle al color `i` de una zona de paleta (azúcar sobre `PatchZone::handle`).
[[nodiscard]] inline copper::PatchHandle zone_color(copper::Scheduler& s,
						    const PatchZone& z, u8 i) {
	return z.handle(s, i);
}

/// **Presupuesto de palabras de Copper** de un `SceneResources` (`copper_bytes` en palabras
/// de 16 bits). Es el límite contra el que un `static_assert` de una etapa de forma conocida
/// compara su huella (`row_repeat_words`), sin ejecutar la escena.
[[nodiscard]] constexpr u16 copper_word_budget(const SceneResources& res) {
	return static_cast<u16>(res.copper_bytes / 2u);
}

/// **Huella en palabras** de la etapa `display(res, ...)`: 10 MOVEs fijos (`DMACON`,
/// `BPLCON0`, `BPLCON1`, `BPLCON2`, `BPL1MOD`, `BPL2MOD`, `DIWSTRT`, `DIWSTOP`, `DDFSTRT`,
/// `DDFSTOP`) más el par `BPLxPTH`+`BPLxPTL` de cada plano. Igual en layout contiguo e
/// interleaved.
[[nodiscard]] constexpr u16 display_words(const SceneResources& res) {
	return static_cast<u16>(20u + 4u * res.planes);
}

/// **Huella en palabras** de la etapa `palette(colors, first, count)`: 2 por color efectivo,
/// con el mismo recorte que `Scheduler::emit_palette` (a `32 - first` y al tamaño de la
/// paleta).
[[nodiscard]] constexpr u16 palette_words(u8 first, u8 count, eng::u32 palette_size) {
	if (first >= 32u || palette_size == 0u || static_cast<eng::u32>(first) >= palette_size) {
		return 0u;
	}
	eng::u32 c = count;
	if (static_cast<eng::u32>(first) + c > 32u) {
		c = 32u - first;
	}
	if (static_cast<eng::u32>(first) + c > palette_size) {
		c = palette_size - static_cast<eng::u32>(first);
	}
	return static_cast<u16>(c * 2u);
}

/// **Huella en palabras** de una zona de `palette_zones`: WAIT de línea (2) más 2 por color
/// efectivo (`Scheduler::emit_palette_zone_at`).
[[nodiscard]] constexpr u16 palette_zone_words(u8 first, u8 count, eng::u32 palette_size) {
	return static_cast<u16>(2u + palette_words(first, count, palette_size));
}

/// **Huella en palabras** de la etapa `reverse_ptrs()`: reemite el par
/// `BPLxPTH`+`BPLxPTL` de cada plano (4 palabras por plano).
[[nodiscard]] constexpr u16 reverse_ptrs_words(const SceneResources& res) {
	return static_cast<u16>(4u * res.planes);
}

/// **Huella en palabras** de la etapa `patchable_zone(line, slots, out)`: WAIT de línea (2)
/// más 2 por slot (un MOVE por slot).
[[nodiscard]] constexpr u16 patchable_zone_words(eng::usize slot_count) {
	return static_cast<u16>(2u + 2u * slot_count);
}

/// **Huella en palabras de UNA intención** de la etapa `intents`, incluido su WAIT, tal como
/// la materializa el `Plan` (que **no** conoce el layout del display): `BitplaneSplit` y
/// `ShiftLines` quedan **sin manejar** (0 palabras) y `SpriteRearm` requiere `sprite_ptr`.
[[nodiscard]] constexpr u16 intent_words(const graphics::CopperIntent& it) {
	switch (it.kind) {
		case graphics::CopperIntentKind::PaletteLine:
		case graphics::CopperIntentKind::PaletteSpan:
			return static_cast<u16>(2u + palette_words(it.first, it.count, it.colors.size()));
		case graphics::CopperIntentKind::SpriteRearm:
			return (it.sprite_ptr == nullptr) ? 0u : 6u; // WAIT + 2 MOVEs
		case graphics::CopperIntentKind::Priority:
			return 4u; // WAIT + 1 MOVE
		case graphics::CopperIntentKind::BitplaneSplit:
		case graphics::CopperIntentKind::ShiftLines:
		default:
			return 0u;
	}
}

/// **Huella en palabras** de la etapa `intents(list)`: suma de `intent_words` más el par de
/// overflow de VPOS (2 palabras) si alguna intención manejada espera una línea > 255
/// (`wait_line_safe` lo emite **una vez**). No depende del orden en que el `Plan` las
/// materialice: el recuento total es el mismo. Asume que la lista es la primera que cruza la
/// 255 (sin un `row_repeat` previo).
[[nodiscard]] constexpr u16 intents_words(const graphics::CopperIntent* list, eng::usize count) {
	u16 words = 0;
	bool overflow = false;
	for (eng::usize i = 0; i < count; ++i) {
		const graphics::CopperIntent& it = list[i];
		const u16 w = intent_words(it);
		words = static_cast<u16>(words + w);
		// Solo `PaletteSpan` usa `wait_position` (sin overflow); el resto, `wait_line_safe`.
		if (w != 0u && it.kind != graphics::CopperIntentKind::PaletteSpan && it.top > 255u) {
			overflow = true;
		}
	}
	if (overflow) {
		words = static_cast<u16>(words + 2u);
	}
	return words;
}

/// **Huella en palabras** de la etapa `row_repeat(rows, repeat, first_line)`: un WAIT de
/// línea (2 palabras, +2 la primera vez que el contador cruza la 255 por el par de overflow)
/// más 3 MOVEs (6 palabras) por cada una de las `rows * repeat` líneas. Etapa de **forma
/// conocida**: permite `static_assert` sobre `copper_word_budget(res)` en compilación, con
/// la misma cuenta que hace la emisión dinámica. Para las dinámicas (intenciones), el
/// presupuesto se comprueba en `materialize`/`end_frame`, nunca por MOVE.
[[nodiscard]] constexpr u16 row_repeat_words(u16 rows, u8 repeat, u16 first_line) {
	const u16 r = (repeat == 0u) ? 1u : repeat;
	const u32 total = static_cast<u32>(rows) * r;
	u16 words = 0;
	bool overflow_sent = false;
	for (u32 i = 0; i < total; ++i) {
		const u16 line = static_cast<u16>(first_line + i);
		words = static_cast<u16>(words + 2u); // WAIT de línea
		if (line > 255u && !overflow_sent) {
			overflow_sent = true;
			words = static_cast<u16>(words + 2u); // par de overflow (0xffdf/0xfffe)
		}
		words = static_cast<u16>(words + 6u); // BPL1MOD + BPL2MOD + BPLCON1
	}
	return words;
}

/// Etapa de **repetición de filas** (cuadruplicado HAM): cada fila lógica ocupa `repeat`
/// líneas; en las `repeat-1` primeras `BPL1MOD/BPL2MOD = -row_bytes` (misma fila) y en la
/// última `0` (avanza). `bplcon1_shift` alterna `BPLCON1` en líneas impares (dither).
/// Huella en palabras: `row_repeat_words(rows, repeat, first_line)`.
[[nodiscard]] inline auto row_repeat(u8 repeat, u16 first_line, u16 bplcon1_shift = 0u) {
	return [=](Scene& sc) {
		const u16 r = repeat == 0u ? 1u : repeat;
		const u16 back = static_cast<u16>(0u - sc.row_bytes());
		const u32 total = static_cast<u32>(sc.rows()) * r;
		for (u32 i = 0; i < total; ++i) {
			sc.scheduler().wait_line_safe(static_cast<u16>(first_line + i));
			const bool last = ((i % r) == (r - 1u));
			const u16 mod = last ? 0u : back;
			sc.scheduler().move(copper::Register::BPL1MOD, mod);
			sc.scheduler().move(copper::Register::BPL2MOD, mod);
			sc.scheduler().move(copper::Register::BPLCON1,
					    ((i & 1u) != 0u) ? bplcon1_shift : 0u);
		}
	};
}

/// Compone: **valida** `res` contra las capacidades de `limits`, inicializa la escena y
/// ejecuta las etapas en orden, cierra la lista. El perfil es **obligatorio**. El motivo del
/// rechazo queda en `scene.config_error()`. Para configs conocidas en compilación, además,
/// usar `static_assert(valid_scene(res, limits))`.
template <class... Stages>
bool compose(Scene& scene, MemorySystem& memory, const SceneResources& res,
	     const DisplayLimits& limits, Stages... stages) {
	if (!scene.init(memory, res, limits)) {
		return false;
	}
	scene.begin_build();
	(stages(scene), ...);
	return scene.end_build();
}

} // namespace eng::graphics::composition
