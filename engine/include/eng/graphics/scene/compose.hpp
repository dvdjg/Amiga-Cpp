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
	u32 copper_bytes = 4096;
	u16 first_line = 0x2c; ///< línea de raster donde arranca la ventana visible (Plan)
};

/// Máximos del modelo de escena (capacidad fija, sin heap).
inline constexpr u8 kMaxSceneBuffers = 3; ///< buffers de display
inline constexpr u8 kMaxScenePlanes = 6;  ///< planos de bitplane

/// Escena viva: posee los bitplanes (contiguos) y la copperlist, y el emisor de Copper.
/// No reserva al sistema más que a través de la `MemorySystem` del backend.
class Scene {
public:
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

	[[nodiscard]] copper::Scheduler& scheduler() { return m_plan.scheduler(); }
	[[nodiscard]] const copper::Scheduler& scheduler() const { return m_plan.scheduler(); }
	[[nodiscard]] copper::Plan& plan() { return m_plan; }
	[[nodiscard]] const copper::Plan& plan() const { return m_plan; }
	[[nodiscard]] constexpr eng::PlaneBytes bitplanes() const { return m_buffers[m_back].view; }
	/// Buffer de display que se está dibujando (el trasero).
	[[nodiscard]] constexpr eng::PlaneBytes back() const { return m_buffers[m_back].view; }
	/// Buffer de display `i` (para leer/escribir otro).
	[[nodiscard]] constexpr eng::PlaneBytes buffer(u8 i) const {
		return (i < m_buffer_count) ? m_buffers[i].view : eng::PlaneBytes {};
	}
	[[nodiscard]] constexpr u8 buffer_count() const { return m_buffer_count; }
	/// Plano `i` del bitmap (layout contiguo; vacío si fuera de rango).
	[[nodiscard]] constexpr eng::PlaneBytes plane(u8 i) const {
		return (i < m_res.planes)
			? bitplanes().subspan(static_cast<eng::u32>(i) * m_plane_bytes, m_plane_bytes)
			: eng::PlaneBytes {};
	}
	[[nodiscard]] constexpr u32 plane_bytes() const { return m_plane_bytes; }
	[[nodiscard]] constexpr u16 row_bytes() const {
		return static_cast<u16>((m_res.width / 8u) & ~1u);
	}
	[[nodiscard]] constexpr u8 planes() const { return m_res.planes; }
	[[nodiscard]] constexpr u16 width() const { return m_res.width; }
	[[nodiscard]] constexpr u16 height() const { return m_res.height; }
	[[nodiscard]] constexpr u16 rows() const { return m_res.rows != 0u ? m_res.rows : m_res.height; }
	[[nodiscard]] constexpr SceneLayout layout() const { return m_res.layout; }
	[[nodiscard]] field::CanvasPlayfield& playfield() { return m_playfield; }
	[[nodiscard]] const field::CanvasPlayfield& playfield() const { return m_playfield; }
	/// Superficie de dibujo con clip (solo layout `Interleaved`).
	[[nodiscard]] field::Surface surface() {
		return field::Surface {m_playfield,
				       field::SurfaceRect {0, 0, m_res.width, m_res.height}};
	}
	[[nodiscard]] bool ok() const { return m_plan.ok(); }
	[[nodiscard]] const copper::ScheduleReport& report() const { return m_plan.report(); }
	[[nodiscard]] constexpr u16 words() const { return m_plan.words(); }

	/// Toma el control mostrando el buffer 0 (una vez).
	template <typename Backend>
	void takeover(Backend& backend) {
		patch_plane_pointers(0u);
		m_plan.takeover(backend);
	}

	/// Publica el buffer dibujado (`m_back`) repuntando los `BPLxPT` y avanza. La lista ya
	/// está instalada; solo se parchean los punteros (doble buffer por `BPLxPT`).
	void commit() {
		patch_plane_pointers(m_back);
		m_back = static_cast<u8>((m_back + 1u) % m_buffer_count);
	}

	/// Registra el índice del MOVE de `BPLxPTH` del plano `p` (lo llama la etapa `display`).
	void set_plane_patch(u8 p, u16 pth_index) {
		if (p < kMaxScenePlanes) {
			m_plane_patch[p] = pth_index;
		}
	}

	// --- Ciclo de vida (plano de comportamiento) ------------------------------------
	/// Liga una tarea a cada punto del ciclo de vida de la escena.
	Scene& on_setup(Task t) { m_setup = t; return *this; }
	Scene& on_frame(Task t) { m_frame = t; return *this; }
	Scene& on_teardown(Task t) { m_teardown = t; return *this; }
	void setup() { run(m_setup); }
	void tick() { run(m_frame); } ///< una vez por frame (hot path)
	void teardown() { run(m_teardown); }

private:
	static void run(Task t) {
		if (t.valid()) {
			t();
		}
	}

	/// Repunta los `BPLxPT` al buffer `index` parcheando el copper. No aplica al interleaved
	/// (usa un único `CanvasPlayfield`).
	void patch_plane_pointers(u8 index) {
		if (m_res.layout == SceneLayout::Interleaved || index >= m_buffer_count) {
			return;
		}
		const eng::u8* base = m_buffers[index].view.data();
		for (u8 p = 0u; p < m_res.planes; ++p) {
			const eng::uintptr ip = reinterpret_cast<eng::uintptr>(
				base + static_cast<eng::u32>(p) * m_plane_bytes);
			m_plan.scheduler().patch_handle(m_plane_patch[p]).set(static_cast<u16>(ip >> 16));
			m_plan.scheduler().patch_handle(static_cast<u16>(m_plane_patch[p] + 2u))
				.set(static_cast<u16>(ip & 0xffffu));
		}
	}

	SceneResources m_res {};
	eng::util::Array<eng::Block<eng::PlaneTag>, kMaxSceneBuffers> m_buffers {};
	field::CanvasPlayfield m_playfield {};
	copper::Plan m_plan {};
	u32 m_plane_bytes = 0;
	u8 m_buffer_count = 1;
	u8 m_back = 0;
	u16 m_plane_patch[kMaxScenePlanes] {};
	Task m_setup {};
	Task m_frame {};
	Task m_teardown {};
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
			const eng::u32 src = static_cast<eng::u32>(sc.planes() - 1u - p) * pb;
			s.move_bitplane_pointer(p, eng::ChipAddress {reinterpret_cast<eng::uintptr>(base + src)});
		}
	};
}

/// **Geometría de display predefinida** (DIWSTRT/DIWSTOP/DDFSTRT/DDFSTOP). Evita cablear
/// los valores habituales en cada llamada a `display(...)`.
struct DisplayGeometry {
	u16 diwstrt = 0x2c81;
	u16 diwstop = 0x2cc1;
	u16 ddfstrt = 0x0038;
	u16 ddfstop = 0x00d0;
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
				sc.set_plane_patch(p, idx);
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
	u8 line = 0;
	eng::PaletteWords colors {};
	u8 first = 0;
	u8 count = 32;
};

/// Etapa de **zonas de paleta** (cambios por línea/banda).
[[nodiscard]] inline auto palette_zones(eng::Span<const PaletteZone> zones) {
	return [=](Scene& sc) {
		for (eng::usize i = 0; i < zones.size(); ++i) {
			const PaletteZone& z = zones[i];
			sc.scheduler().emit_palette_zone(z.line, z.colors, z.first, z.count);
		}
	};
}

/// **Un MOVE parcheable dentro de una zona**: registro destino + valor inicial. Es la
/// unidad de la base común de toda modificación dinámica del copper (un color, un
/// `BPL1MOD` de scanline, un puntero `BPLxPT`, un `BPLCON1`…).
struct PatchSlot {
	copper::Register reg = copper::Register::COLOR00;
	u16 value = 0;
};

/// **Grupo de MOVEs parcheables emitidos en una línea** (base común). Guarda el índice del
/// primer MOVE; cada slot es una instrucción de 2 words (MOVE + dato), de ahí el `+2*i`.
struct PatchZone {
	u8 line = 0;
	u16 first_move = 0;
	u8 count = 0;

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

/// Etapa de **zonas de paleta parcheables**: convierte cada zona a slots `COLORxx`, los
/// emite con la base común (`patchable_zone`) y escribe su `PatchZone` en `out`.
[[nodiscard]] inline auto palette_zones_patchable(eng::Span<const PaletteZone> zones,
						  eng::Span<PatchZone> out) {
	return [=](Scene& sc) {
		const eng::usize n = zones.size() < out.size() ? zones.size() : out.size();
		PatchSlot slots[32];
		for (eng::usize i = 0; i < n; ++i) {
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
			for (u8 k = 0u; k < count; ++k) {
				slots[k] = PatchSlot {
					static_cast<copper::Register>(
						copper::color_register(static_cast<u8>(first + k))),
					z.colors[first + k]};
			}
			patchable_zone(z.line, eng::Span<const PatchSlot> {slots, count}, &out[i])(sc);
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
