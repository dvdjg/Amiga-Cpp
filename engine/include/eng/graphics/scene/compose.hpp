#pragma once

/// \file compose.hpp
/// **Composición de escenas** (prototipo del modelo de tres planos): una escena se construye
/// uniendo **etapas** que piden recursos y emiten Copper, en vez de una clase por driver.
/// Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.
///
/// Uso:
///
/// ```cpp
/// scene::Scene s;
/// scene::compose(s, memory, scene::planar4(320, 256),
///     scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200),
///     scene::palette(pal, 0, 16));
/// s.install(backend);        // o takeover
/// ```
///
/// Una etapa es un callable `void(Scene&)`: pide lo que necesita a la escena (geometría,
/// planos) y emite al `copper::Scheduler`. Los **presets** son funciones que devuelven un
/// `SceneResources`; no hay clase por efecto. El **programa** es data (Copper) que ejecuta
/// el chip; la **variabilidad** se hace con `copper::PatchHandle` (`scheduler().patchable`).

#include <eng/core/arith.hpp>
#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/function_ref.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/scene/limits.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::scene {

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
	/// Crea la escena: reserva los bitplanes (según `res`) y la copperlist en Chip RAM.
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
		return init_unchecked(memory, res);
	}

	/// Devuelve `false` si la geometría o la memoria no son válidas.
	/// (Interno: solo lo llama `init(...)` tras validar; no valida el perfil.)
	bool init_unchecked(MemorySystem& memory, const SceneResources& res) {
		m_res = res;
		const u16 row = row_bytes();
		const u16 logical_rows = res.rows != 0u ? res.rows : res.height;
		if (row == 0u || res.height == 0u || res.planes == 0u) {
			return false;
		}
		const u16 alloc_rows = (res.layout == SceneLayout::Interleaved) ? res.height : logical_rows;
		m_plane_bytes = static_cast<u32>(row) * alloc_rows;
		// Doble/triple buffer solo en layout contiguo (la doble buffer se hace parcheando los
		// BPLxPT; el interleaved usa un único `CanvasPlayfield`).
		u8 buffers = res.buffers;
		if (res.layout == SceneLayout::Interleaved || buffers < 1u) {
			buffers = 1u;
		}
		if (buffers > kMaxSceneBuffers) {
			buffers = kMaxSceneBuffers;
		}
		m_buffer_count = buffers;
		for (u8 b = 0u; b < buffers; ++b) {
			m_buffers[b] = memory.chip.allocate_block<eng::PlaneTag>(m_plane_bytes * res.planes + 16u, 16);
			if (!m_buffers[b].valid()) {
				return false;
			}
		}
		if (res.layout == SceneLayout::Interleaved &&
		    !m_playfield.bind(m_buffers[0], field::CanvasPlayfield::Config {res.width, res.height, res.planes})) {
			return false;
		}
		m_back = (buffers > 1u) ? 1u : 0u;
		copper::PlanConfig pcfg {};
		pcfg.copper_bytes = res.copper_bytes;
		pcfg.first_line = res.first_line;
		if (!m_plan.begin(memory, pcfg)) {
			return false;
		}
		return true;
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
			? bitplanes().subspan(static_cast<eng::u32>(i) * m_plane_bytes, m_plane_bytes)
			: eng::PlaneBytes {};
	}
	/// Bytes de un plano completo (`row_bytes * alloc_rows`).
	[[nodiscard]] constexpr u32 plane_bytes() const { return m_plane_bytes; }
	/// Bytes por fila de un plano (`width/8`, redondeado a par).
	[[nodiscard]] constexpr u16 row_bytes() const {
		return static_cast<u16>((m_res.width / 8u) & ~1u);
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
	/// Superficie de dibujo con clip (solo layout `Interleaved`).
	[[nodiscard]] field::Surface surface() {
		return field::Surface {m_playfield,
				       field::SurfaceRect {0, 0, m_res.width, m_res.height}};
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
	}

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
	/// Ejecuta la tarea de frame (si la hay). **Una vez por frame (hot path).**
	void tick() { run(m_frame); }
	/// Ejecuta la tarea de teardown (si la hay).
	void teardown() { run(m_teardown); }

private:
	/// Invoca una `Task` solo si es válida (no nula).
	static void run(Task t) {
		if (t.valid()) {
			t();
		}
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
			const eng::u32 off = static_cast<eng::u32>(src) * m_plane_bytes;
			m_plane_patch[p].apply(words, static_cast<eng::u32>(
							 reinterpret_cast<eng::uintptr>(addr + off)));
		}
	}

	SceneResources m_res {}; ///< geometría/recursos de la escena (copiados en `init`)
	eng::util::Array<eng::Block<eng::PlaneTag>, kMaxSceneBuffers> m_buffers {}; ///< buffers de bitplanes (Chip)
	field::CanvasPlayfield m_playfield {}; ///< playfield del layout interleaved (base de `surface()`)
	copper::Plan m_plan {}; ///< programa de Copper (lista + presupuesto + emisor)
	u32 m_plane_bytes = 0; ///< bytes de un plano completo (`row_bytes * alloc_rows`)
	u8 m_buffer_count = 1; ///< buffers de display en uso (1..`kMaxSceneBuffers`)
	u8 m_back = 0; ///< índice del buffer trasero (el que se dibuja/publica)
	Patch32 m_plane_patch[kMaxScenePlanes] {}; ///< parcheo `BPLxPT` por registro (doble/triple buffer)
	u8 m_plane_source[kMaxScenePlanes] {}; ///< qué plano de bitmap muestra cada registro `BPLxPT`
	Task m_setup {}; ///< tarea de setup (una vez)
	Task m_frame {}; ///< tarea de frame (por `tick`)
	Task m_teardown {}; ///< tarea de teardown
	ConfigError m_config_error {}; ///< motivo del último rechazo de configuración (vacío = ok)
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

/// **Geometría de display predefinida** (DIWSTRT/DIWSTOP/DDFSTRT/DDFSTOP). Evita cablear
/// los valores habituales en cada llamada a `display(...)`.
struct DisplayGeometry {
	u16 diwstrt = 0x2c81; ///< DIWSTRT (ventana visible, esquina superior)
	u16 diwstop = 0x2cc1; ///< DIWSTOP (ventana visible, esquina inferior)
	u16 ddfstrt = 0x0038; ///< DDFSTRT (inicio del fetch de bitplanes)
	u16 ddfstop = 0x00d0; ///< DDFSTOP (fin del fetch de bitplanes)
};

/// PAL lowres **320×256** con *fetch* estándar (40 B/fila): la geometría de las demos.
inline constexpr DisplayGeometry kPal320x256 {0x2c81, 0x2cc1, 0x0038, 0x00d0};

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
	constexpr u16 kHam = 0x0800u;   ///< bit HAM
	constexpr u16 kEhb = 0x0040u;   ///< BPU bit 0 extra para 6 planos (EHB usa BPU=6)
	switch (mode) {
		case SceneMode::Ham:
			return static_cast<u16>(bpu | kColor | kHam);
		case SceneMode::Ehb:
			return static_cast<u16>(bpu | kColor | kEhb);
		default:
			return static_cast<u16>(bpu | kColor);
	}
}

/// **Geometría de display** coherente con los recursos (DIW/DDF). Si `res` la especifica
/// (campos != 0) se respeta; si no, se deriva del ancho estándar lores: DIW `0x2c81/0x2cc1`
/// y DDF `0x0038` + palabras de fetch del ancho. Es lo que consume la etapa `display`.
[[nodiscard]] constexpr DisplayGeometry geometry_for(const SceneResources& res) {
	DisplayGeometry g {};
	g.diwstrt = (res.diwstrt != 0u) ? res.diwstrt : 0x2c81u;
	g.diwstop = (res.diwstop != 0u) ? res.diwstop : 0x2cc1u;
	if (res.ddfstrt != 0u || res.ddfstop != 0u) {
		g.ddfstrt = res.ddfstrt;
		g.ddfstop = res.ddfstop;
	} else {
		// Estándar lores: `DDFSTRT = 0x38`, y `DDFSTOP` tal que cubra `width`. Palabras de
		// fetch = (ddfstop - ddfstrt)/8 + 1 (paso de DDF = 8 B = 1 palabra de 16 px). Para
		// 320 px -> (0xD0-0x38)/8+1 = 20 palabras (el estándar). Se acota al máximo hw 0xD8.
		g.ddfstrt = 0x0038u;
		const u16 words = static_cast<u16>((res.width + 15u) / 16u);
		u16 stop = static_cast<u16>(g.ddfstrt + 8u * (words - 1u));
		if (stop > 0x00d8u) {
			stop = 0x00d8u;
		}
		g.ddfstop = stop;
	}
	return g;
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

/// Etapa de **repetición de filas** (cuadruplicado HAM): cada fila lógica ocupa `repeat`
/// líneas; en las `repeat-1` primeras `BPL1MOD/BPL2MOD = -row_bytes` (misma fila) y en la
/// última `0` (avanza). `bplcon1_shift` alterna `BPLCON1` en líneas impares (dither).
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

/// Variante **sin validar** (solo tests de bajo nivel / casos ya validados aparte): no
/// comprueba el perfil. No usar en código de aplicación.
template <class... Stages>
bool compose_unchecked(Scene& scene, MemorySystem& memory, const SceneResources& res,
		       Stages... stages) {
	if (!scene.init_unchecked(memory, res)) {
		return false;
	}
	scene.begin_build();
	(stages(scene), ...);
	return scene.end_build();
}

} // namespace eng::graphics::scene
