#pragma once

/// \file stages.hpp
/// **Etapas y presets de composición** (`composition::display`, `palette`, `palette_zones`,
/// `patchable_zone`, `intents`, `row_repeat`, `compose`…): piden recursos a la `Scene` y
/// emiten al `copper::Scheduler`. Incluye las huellas en palabras de cada etapa
/// (`*_words`) para el presupuesto de Copper. La escena vive en `scene.hpp`.

#include <eng/graphics/composition/scene.hpp>

namespace eng::graphics::composition {

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
		const eng::ChipPlaneView planes = sc.chip_planes();
		const u32 pb = sc.plane_bytes();
		for (eng::u8 p = 0u; p < sc.planes(); ++p) {
			const eng::u32 src = eng::math::mulu16(static_cast<u16>(sc.planes() - 1u - p),
							      static_cast<u16>(pb));
			const eng::Address<eng::MemoryKind::Chip> ip = planes.address(src);
			const u16 idx = s.move_at(copper::bitplane_pointer_high_register(p),
						  static_cast<u16>(ip.value >> 16));
			(void)s.move_at(copper::bitplane_pointer_low_register(p),
					static_cast<u16>(ip.value & 0xffffu));
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
				s.move_bitplane_pointer(p, hv.bitplanes + p * row);
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
			const eng::ChipPlaneView planes = sc.chip_planes();
			for (u8 p = 0u; p < sc.planes(); ++p) {
				const eng::Address<eng::MemoryKind::Chip> ip =
					planes.address(p * sc.plane_bytes());
				const u16 idx = s.move_at(copper::bitplane_pointer_high_register(p),
							  static_cast<u16>(ip.value >> 16));
				(void)s.move_at(copper::bitplane_pointer_low_register(p),
						static_cast<u16>(ip.value & 0xffffu));
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
