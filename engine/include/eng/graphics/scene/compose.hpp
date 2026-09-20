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
enum class SceneLayout : eng::u8 {
	Contiguous = 0, ///< un plano tras otro (cada fila de un plano, contiguas)
	Interleaved = 1, ///< fila a fila con los N planos (el que espera `CanvasPlayfield`)
};

/// **Recursos** que una escena planar necesita (plano de recursos del modelo).
struct SceneResources {
	u16 width = 320;   ///< ancho visible (múltiplo de 16)
	u16 height = 256;  ///< filas del display
	u16 rows = 0;      ///< filas lógicas del bitmap (0 = igual a `height`)
	u8 planes = 4;     ///< planos de bitplane
	SceneLayout layout = SceneLayout::Contiguous; ///< disposición de los bitplanes
	u8 buffers = 1;    ///< nº de buffers de display (1/2/3); >1 = doble/triple buffer
	u32 copper_bytes = 4096; ///< capacidad de la copperlist (bytes) reservada en Chip
	u16 first_line = 0x2c; ///< línea de raster donde arranca la ventana visible (Plan)
};

/// Máximos del modelo de escena (capacidad fija, sin heap).
inline constexpr u8 kMaxSceneBuffers = 3; ///< buffers de display
inline constexpr u8 kMaxScenePlanes = 6;  ///< planos de bitplane

/// **Valor de 32 bits parcheable** = pareja de MOVEs (`hi`/`lo`), p. ej. un puntero
/// `BPLxPTH` + `BPLxPTL` o un parámetro de 32 bits. Es el caso **multi-registro** de la base
/// común: un valor lógico que ocupa varios MOVEs.
struct Patch32 {
	copper::PatchHandle hi {}; ///< MOVE de la mitad alta (p. ej. `BPLxPTH`)
	copper::PatchHandle lo {}; ///< MOVE de la mitad baja (p. ej. `BPLxPTL`)

	/// Escribe `value` repartido en `hi`/`lo` (parcheo de 32 bits en dos MOVEs).
	void set(eng::u32 value) const {
		hi.set(static_cast<u16>(value >> 16));
		lo.set(static_cast<u16>(value & 0xffffu));
	}
};

/// Construye un `Patch32` a partir del índice del MOVE `hi` (el `lo` va 2 words después).
[[nodiscard]] inline Patch32 patch32_at(copper::Scheduler& s, u16 hi_index) {
	return Patch32 {s.patch_handle(hi_index), s.patch_handle(static_cast<u16>(hi_index + 2u))};
}

/// Escena viva: posee los bitplanes (contiguos) y la copperlist, y el emisor de Copper.
/// No reserva al sistema más que a través de la `MemorySystem` del backend.
class Scene {
public:
	/// Crea la escena: reserva los bitplanes (según `res`) y la copperlist en Chip RAM.
	/// Devuelve `false` si la geometría o la memoria no son válidas.
	bool init(MemorySystem& memory, const SceneResources& res) {
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
	/// (usa un único `CanvasPlayfield`). Recorre los planos **avanzando el puntero** (sin
	/// `p * plane_bytes`, que emitiría `__mulsi3` en 68000).
	void patch_plane_pointers(u8 index) {
		if (m_res.layout == SceneLayout::Interleaved || index >= m_buffer_count) {
			return;
		}
		const eng::u8* addr = m_buffers[index].view.data();
		for (u8 p = 0u; p < m_res.planes; ++p) {
			m_plane_patch[p].set(static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(addr)));
			addr += m_plane_bytes;
		}
	}

	SceneResources m_res {}; ///< geometría/recursos de la escena (copiados en `init`)
	eng::util::Array<eng::Block<eng::PlaneTag>, kMaxSceneBuffers> m_buffers {}; ///< buffers de bitplanes (Chip)
	field::CanvasPlayfield m_playfield {}; ///< playfield del layout interleaved (base de `surface()`)
	copper::Plan m_plan {}; ///< programa de Copper (lista + presupuesto + emisor)
	u32 m_plane_bytes = 0; ///< bytes de un plano completo (`row_bytes * alloc_rows`)
	u8 m_buffer_count = 1; ///< buffers de display en uso (1..`kMaxSceneBuffers`)
	u8 m_back = 0; ///< índice del buffer trasero (el que se dibuja/publica)
	Patch32 m_plane_patch[kMaxScenePlanes] {}; ///< parcheo `BPLxPT` por plano (doble/triple buffer)
	Task m_setup {}; ///< tarea de setup (una vez)
	Task m_frame {}; ///< tarea de frame (por `tick`)
	Task m_teardown {}; ///< tarea de teardown
};

/// Preset: escena planar de 4 planos por defecto.
[[nodiscard]] constexpr SceneResources planar4(u16 width = 320, u16 height = 256,
						u8 planes = 4) {
	SceneResources r {};
	r.width = width;
	r.height = height;
	r.planes = planes;
	return r;
}

/// Preset: escena planar **interleaved** con `surface()` (caso `CanvasScene`).
[[nodiscard]] constexpr SceneResources canvas(u16 width = 320, u16 height = 256,
					      u8 planes = 4) {
	SceneResources r {};
	r.width = width;
	r.height = height;
	r.planes = planes;
	r.layout = SceneLayout::Interleaved;
	return r;
}

/// Preset: escena para efecto HAM/cuadruplicado (`rows` lógicas, planos contiguos).
[[nodiscard]] constexpr SceneResources ham(u16 width = 320, u16 height = 256,
					   u16 rows = 64, u8 planes = 4) {
	SceneResources r {};
	r.width = width;
	r.height = height;
	r.rows = rows;
	r.planes = planes;
	return r;
}

/// Preset: escena EHB estática (6 planos contiguos; `bplcon0` EHB = 0x6200 en la etapa).
[[nodiscard]] constexpr SceneResources ehb(u16 width = 320, u16 height = 256) {
	SceneResources r {};
	r.width = width;
	r.height = height;
	r.planes = 6;
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
			// Registra este MOVE como el parche del plano `p`: en doble buffer, `commit()`
			// repunta el BPLxPT efectivo (el de orden inverso, que es el que manda).
			sc.set_plane_patch(p, patch32_at(s, idx));
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

/// Compone: inicializa la escena con `res` y ejecuta las etapas en orden, cierra la lista.
template <class... Stages>
bool compose(Scene& scene, MemorySystem& memory, const SceneResources& res, Stages... stages) {
	if (!scene.init(memory, res)) {
		return false;
	}
	scene.begin_build();
	(stages(scene), ...);
	return scene.end_build();
}

} // namespace eng::graphics::scene
