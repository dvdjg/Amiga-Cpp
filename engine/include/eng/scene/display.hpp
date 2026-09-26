#pragma once

/// \file display.hpp
/// **Escena declarativa de bajo nivel** (`eng::scene`): emite el display estándar (DMACON,
/// BPLCON0, módulos, DIW/DDF y punteros `BPLxPT`) y **efectos de Copper** (degradado de `COLOR00`,
/// fine-scroll parcheable, paleta) sobre un `copper::Scheduler`, sin que el juego nombre registros
/// ni compute direcciones. Encima del `Scheduler`/`Plan` existentes.
///
/// ```cpp
/// eng::copper::SchedulerT<false> sched { copper_block };
/// eng::scene::emit_display(sched, { .planes_view = planes, .planes = 5u });
/// auto scroll = eng::scene::emit_fine_scroll(sched, 0u);
/// eng::scene::emit_gradient(sched, 0x41u, 0x4fu, 0x0111u);
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/field/playfield_base.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/mode_switch.hpp>

namespace eng::scene {

/// Descripción de un display de bajo-res estándar (valores por defecto: 320x256, 5 planos).
struct DisplayDesc {
	eng::u16 diwstrt = 0x2c81u;
	eng::u16 diwstop = 0x2cc1u;
	eng::u16 ddfstrt = 0x0038u;
	eng::u16 ddfstop = 0x00d0u;
	eng::u16 bplcon0 = 0x5200u; ///< 5 planos + COLOR
	eng::u16 bplcon1 = 0u;      ///< fine scroll (nibble bajo = PF1, alto = PF2 en DPF)
	eng::u16 bplcon2 = 0u;
	eng::u8 planes = 5u;
	eng::u16 bytes_per_row = 40u; ///< bytes por fila de un plano (320 px / 8)
	/// `BPLxMOD` explícitos (corkscrew). Con `auto_mod = true` se calculan como interleaved.
	bool auto_mod = true;
	eng::u16 bpl1mod = 0u;
	eng::u16 bpl2mod = 0u;
	eng::ChipPlaneView planes_view {}; ///< base Chip de los planos (DMA)
	/// Segunda superficie (**dual playfield**): si no está vacía, `planes_view` alimenta los
	/// planos pares (PF1) y `planes_view_b` los impares (PF2), intercalados uno a uno.
	eng::ChipPlaneView planes_view_b {};
	eng::u16 dmacon = static_cast<eng::u16>(::eng::copper::DmaSetClear | ::eng::copper::DmaMaster |
						::eng::copper::DmaCopper | ::eng::copper::DmaBitplane |
						::eng::copper::DmaBlitter);
};

/// Emite el display estándar en `sched`: DMACON/BPLCONx, módulos interleaved, DIW/DDF y `BPLxPT`
/// (un plano por paso `row_bytes`). El juego no ve registros. Con `pointers = false` emite solo la
/// cabecera (geometría), para que la composición emita los punteros con `emit_band_pointers`.
template <class Scheduler>
void emit_display(Scheduler& sched, const DisplayDesc& d, bool pointers = true) {
	sched.move(::eng::copper::Register::DMACON, d.dmacon);
	sched.move(::eng::copper::Register::BPLCON0, d.bplcon0);
	sched.move(::eng::copper::Register::BPLCON1, d.bplcon1);
	sched.move(::eng::copper::Register::BPLCON2, d.bplcon2);
	// Interleaved: tras leer una fila de un plano (bytes_per_row), el siguiente plano está a
	// bytes_per_row; el "salto" a la fila siguiente es (planes-1)*bytes_per_row. Con
	// `auto_mod = false` se usan los módulos explícitos (p. ej. corkscrew).
	const eng::u16 auto_mod = static_cast<eng::u16>(d.bytes_per_row * (d.planes - 1u));
	const eng::u16 mod1 = d.auto_mod ? auto_mod : d.bpl1mod;
	const eng::u16 mod2 = d.auto_mod ? auto_mod : d.bpl2mod;
	sched.move(::eng::copper::Register::BPL1MOD, mod1);
	sched.move(::eng::copper::Register::BPL2MOD, mod2);
	sched.move(::eng::copper::Register::DIWSTRT, d.diwstrt);
	sched.move(::eng::copper::Register::DIWSTOP, d.diwstop);
	sched.move(::eng::copper::Register::DDFSTRT, d.ddfstrt);
	sched.move(::eng::copper::Register::DDFSTOP, d.ddfstop);
	if (!pointers) {
		return;
	}
	for (eng::u8 p = 0u; p < d.planes; ++p) {
		sched.move_bitplane_pointer(p, d.planes_view.address(
						      static_cast<eng::s32>(static_cast<eng::u32>(p) *
									    d.bytes_per_row)));
	}
}

/// Emite una **paleta** `count` colores (registros `COLOR00..`).
template <class Scheduler>
void emit_palette(Scheduler& sched, const eng::u16* palette, eng::u8 count) {
	for (eng::u8 i = 0u; i < count; ++i) {
		sched.move(::eng::copper::color_register(i), palette[i]);
	}
}

/// **Degradado** de `COLOR00` en las líneas `[first, last]`: `step * (linea - first + 1)`.
template <class Scheduler>
void emit_gradient(Scheduler& sched, eng::u8 first, eng::u8 last, eng::u16 step) {
	eng::u16 k = 1u;
	for (eng::u8 line = first; line <= last; ++line, ++k) {
		sched.wait_line(line);
		sched.move(::eng::copper::Register::COLOR00, static_cast<eng::u16>(step * k));
	}
}

/// **Fine-scroll horizontal** parcheable por frame: MOVE de `BPLCON1` con `handle` tipado.
template <class Scheduler>
[[nodiscard]] ::eng::copper::PatchHandle emit_fine_scroll(Scheduler& sched,
							  eng::u16 initial = 0u) {
	return sched.patchable(::eng::copper::Register::BPLCON1, initial);
}

/// **Banda (tramo) de pantalla**: geometría de vídeo propia dentro del campo.
///
/// Cada banda describe un tramo vertical con su número de planos (`0` = sin DMA de planos, p.ej.
/// una franja de efectos *copper chunky*), los bits de modo (`DBLPF`/HIRES/EHB) y la base de sus
/// planos en Chip. La **primera** banda es el campo principal (emite el display completo); las
/// siguientes conmutan la geometría con `ModeSwitchZone` en su `top`, en orden ascendente.
struct Band {
	eng::u16 top = 0;
	eng::u16 height = 0; ///< alto en líneas (informativo; el recorte lo fija DIW)
	eng::u8  planes = 0;
	eng::u16 ddfstrt = 0x0038u;
	eng::u16 ddfstop = 0x00d0u;
	eng::u16 bplcon1 = 0u; ///< fine scroll (PF1 en el nibble bajo, PF2 en el alto)
	eng::u16 bytes_per_row = 40u; ///< bytes por fila de un plano (320 px / 8)
	/// Stride entre planos: `0` = interleaved (módulo `bytes_per_row × (planes − 1)`); si no,
	/// los planos van en bloques contiguos separados `plane_bytes` (módulo `0`).
	eng::u16 plane_bytes = 0;
	eng::ChipPlaneView planes_view {}; ///< base Chip del plano 0 (DMA); vacío si `planes == 0`
	/// Segunda superficie (**dual playfield**): PF1 en `planes_view` (planos pares) y PF2 en
	/// `planes_view_b` (impares). No la usa `bob_target` (los BOB van a PF1).
	eng::ChipPlaneView planes_view_b {};
	/// Superficie de origen si la banda viene de un `PlayfieldHardwareView` (`band_from_view` /
	/// `update_from_view`): habilita la emisión de punteros con `eng::field::emit_view_pointers`
	/// (fuente única, con parallax/soft-DPF). Vacía = banda explícita (bloque o DPF de dos vistas).
	eng::field::PlayfieldHardwareView source_view {};
	/// Desplazamiento de la base por el scroll actual (bytes): alinea los BOB con la posición de
	/// pantalla. Lo fija `update_from_view` (`planeaddx + planeaddy`).
	eng::s32 scroll_off = 0;
	/// Vista **mutable** de los planos (Chip) para dibujar BOBs en esta banda: la misma memoria
	/// que `planes_view`, pero escribible. Vacía = la banda no dibuja BOBs.
	eng::Bytes<eng::PlaneTag> bob_base {};
	eng::u16 bplcon2 = 0;
	bool color = true;         ///< bit COLOR de `BPLCON0`
	bool dual_playfield = false; ///< `DBLPF`: 3+3 (PF1 planos pares, PF2 impares)
	bool hires = false;
	bool ehb = false;
	eng::PaletteWords palette {}; ///< paleta opcional de la banda
	eng::u8 palette_colors = 0;

	/// `BPL1MOD`/`BPL2MOD` de la banda. Las fábricas (`band_of_planes`, `band_from_view`) los
	/// rellenan; un desplazamiento discontinuo (corkscrew) los trae de la superficie.
	eng::u16 bpl1mod = 0u;
	eng::u16 bpl2mod = 0u;
	/// **Split vertical** (corkscrew/ring): a partir de `top + split_line` los planos vuelven a la
	/// fila `split_base_off` del bitmap hasta el pie del tramo. Es la “intención de banda” del wrap.
	bool split_active = false;
	eng::u16 split_line = 0;
	eng::s32 split_base_off = 0;

	/// Módulo **interleaved** por defecto: `bytes_per_row × (planes − 1)` (`0` sin planos).
	[[nodiscard]] constexpr eng::u16 modulo() const noexcept {
		return (plane_bytes != 0u || planes == 0u)
			       ? 0u
			       : static_cast<eng::u16>(bytes_per_row *
						       static_cast<eng::u16>(planes - 1u));
	}
	/// `BPLCON0` de la banda: `BPU << 12 | [COLOR] | [HIRES] | [DBLPF] | [EHB]`.
	[[nodiscard]] constexpr eng::u16 bplcon0() const noexcept {
		eng::u16 v = static_cast<eng::u16>(planes << 12u);
		if (color) {
			v = static_cast<eng::u16>(v | 0x0200u);
		}
		if (hires) {
			v = static_cast<eng::u16>(v | 0x8000u);
		}
		if (dual_playfield) {
			v = static_cast<eng::u16>(v | 0x0400u);
		}
		if (ehb) {
			v = static_cast<eng::u16>(v | 0x0080u);
		}
		return v;
	}
	/// Destino de BOBs de esta banda (`BobTarget`), para `BobLayer`/`FastBobLayer`/`clear_box`.
	///
	/// En **dual playfield** devuelve el destino del playfield **frontal** (PF1, planos pares):
	/// como sus planos están intercalados 1 de cada 2 con los de PF2, la fila avanza
	/// `bytes_per_row × planes` y el stride de plano es `bytes_per_row × 2` (con `planes / 2`
	/// planos, layout `Planar`), de modo que el Blitter cubre solo los planos de PF1.
	[[nodiscard]] eng::graphics::BobTarget bob_target() const noexcept {
		eng::graphics::BobTarget t {};
		t.base = (bob_base.data() != nullptr) ? bob_base.data() + scroll_off : nullptr;
		if (dual_playfield && !planes_view_b.empty() && (planes % 2u) == 0u) {
			// DPF de **dos bitmaps** (cada campo interleave propio): PF1 = `planes_view`,
			// interleave de `planes / 2` planos con módulo interleaved estándar.
			t.row_bytes = bytes_per_row;
			t.planes = static_cast<eng::u8>(planes / 2u);
			t.layout = eng::graphics::BobLayout::Interleaved;
		} else if (dual_playfield && planes >= 2u) {
			t.row_bytes = static_cast<eng::u16>(bytes_per_row * static_cast<eng::u16>(planes));
			t.plane_bytes = static_cast<eng::u32>(bytes_per_row) * 2u;
			t.planes = static_cast<eng::u8>(planes / 2u);
			t.layout = eng::graphics::BobLayout::Planar;
		} else {
			t.row_bytes = bytes_per_row;
			t.plane_bytes = plane_bytes;
			t.planes = planes;
			t.layout = (plane_bytes == 0u) ? eng::graphics::BobLayout::Interleaved
						       : eng::graphics::BobLayout::Planar;
		}
		return t;
	}

	/// Refresca la geometría dependiente del **scroll** desde la superficie (base, fine scroll,
	/// módulos), sin tocar `top`/`height`/modo. El driver de scroll actualiza su
	/// `PlayfieldHardwareView`; la composición llama esto por frame y re-`materialize`. Así la
	/// banda **no** lleva un campo `scroll`: refleja lo que el driver ya calculó.
	void update_from_view(const eng::field::PlayfieldHardwareView& view) noexcept {
		planes = view.planes;
		bytes_per_row = view.bitmap_bytes_per_row;
		bplcon1 = view.bplcon1;
		bpl1mod = view.bpl1mod;
		bpl2mod = view.bpl2mod;
		const eng::s32 off = static_cast<eng::s32>(view.planeaddx + view.planeaddy);
		planes_view = eng::ChipPlaneView {view.real_base + off, view.plane_bytes};
		bob_base = eng::Bytes<eng::PlaneTag> {view.real_base.ptr(), view.plane_bytes};
		scroll_off = off;
		source_view = view;
	}
};

/// Emite los `BPLxPT` de una banda desde su base desplazada `off` bytes: un plano por paso
/// `bytes_per_row`. Con **dual playfield** (`planes_view_b`) emite PF1 en los planos pares y PF2
/// en los impares, intercalados. Es lo que usa `RasterLayout` para el display y para el **split
/// vertical** (reapuntar al inicio del bucle a mitad del tramo).
template <class Scheduler>
void emit_band_pointers(Scheduler& sched, const Band& b, eng::s32 off = 0) {
	if (b.planes == 0u) {
		return;
	}
	if (b.source_view.planes != 0u) {
		// Banda desde superficie: fuente única (incluye parallax/soft-DPF).
		eng::field::emit_view_pointers(sched, b.source_view, off);
		return;
	}
	if (!b.planes_view_b.empty() && (b.planes % 2u) == 0u) {
		const eng::u8 ppf = static_cast<eng::u8>(b.planes / 2u);
		for (eng::u8 i = 0u; i < ppf; ++i) {
			const eng::s32 o =
				static_cast<eng::s32>(off + static_cast<eng::s32>(i) * b.bytes_per_row);
			sched.move_bitplane_pointer(static_cast<eng::u8>(i * 2u),
						    b.planes_view.address(o));
			sched.move_bitplane_pointer(static_cast<eng::u8>(i * 2u + 1u),
						    b.planes_view_b.address(o));
		}
		return;
	}
	for (eng::u8 p = 0u; p < b.planes; ++p) {
		sched.move_bitplane_pointer(
			p, b.planes_view.address(off + static_cast<eng::s32>(p) * b.bytes_per_row));
	}
}

/// Banda a partir de un bloque de planos en Chip: fija la vista de display (solo lectura) y la
/// base mutable de BOBs a la **misma** memoria.
[[nodiscard]] inline Band band_of_planes(const eng::Block<eng::PlaneTag, eng::MemoryKind::Chip>& block,
					 eng::u8 planes, eng::u16 bytes_per_row,
					 eng::u16 top) noexcept {
	Band b {};
	b.top = top;
	b.planes = planes;
	b.bytes_per_row = bytes_per_row;
	b.planes_view = block.mem_view();
	b.bob_base = block.view;
	b.bpl1mod = b.modulo();
	b.bpl2mod = b.modulo();
	return b;
}

/// Banda a partir de la **superficie** de un playfield (`PlayfieldHardwareView`): el algoritmo de
/// scroll mantiene esa instantánea (base, fine scroll, módulos) y la composición la coloca en su
/// tramo. Así la banda **no** lleva un campo `scroll`: refleja lo que el driver de scroll ya
/// calculó (ver `PLAYFIELD_SCROLL_ARCHITECTURE.md` §2-§4). El `top` lo fija el llamador.
[[nodiscard]] inline Band band_from_view(const eng::field::PlayfieldHardwareView& view,
					 eng::u16 top) noexcept {
	Band b {};
	b.top = top;
	b.update_from_view(view);
	return b;
}

/// Banda **dual playfield** a partir de dos superficies (PF1 = frontal, PF2 = fondo), cada una un
/// bitmap con su propio interleave/scroll (el layout del `XlimitedDualComposer`): `planes_view` =
/// PF1 (planos de hardware pares) y `planes_view_b` = PF2 (impares), cada plano a `i·bpr`, con los
/// módulos y el fine scroll (dos nibbles) de cada campo. `RasterLayout` emite así el DPF sin bajar
/// al compositor. Es el caso “DPF con bg y fg independientes”.
[[nodiscard]] inline Band band_from_dual_view(const eng::field::PlayfieldHardwareView& pf1,
					      const eng::field::PlayfieldHardwareView& pf2,
					      eng::u16 top) noexcept {
	Band b {};
	const eng::u8 ppf = pf1.planes;
	b.top = top;
	b.planes = static_cast<eng::u8>(ppf + pf2.planes);
	b.dual_playfield = true;
	b.bytes_per_row = pf1.bitmap_bytes_per_row;
	b.bplcon1 = static_cast<eng::u16>(((pf2.bplcon1 & 0x0fu) << 4u) | (pf1.bplcon1 & 0x0fu));
	b.bpl1mod = pf1.bpl1mod;
	// Fetch real de PF1 (la ventana la marca PF1): módulo de PF2 para que lea su propia fila.
	const eng::u16 fetch1 = static_cast<eng::u16>(
		static_cast<eng::u32>(pf1.bitmap_bytes_per_row) * ppf - pf1.bpl1mod);
	b.bpl2mod = static_cast<eng::u16>(
		static_cast<eng::u32>(pf2.bitmap_bytes_per_row) * pf2.planes - fetch1);
	const eng::s32 off1 = static_cast<eng::s32>(pf1.planeaddx + pf1.planeaddy);
	const eng::s32 off2 = static_cast<eng::s32>(pf2.planeaddx + pf2.planeaddy);
	b.planes_view = eng::ChipPlaneView {pf1.real_base + off1, pf1.plane_bytes};
	b.planes_view_b = eng::ChipPlaneView {pf2.real_base + off2, pf2.plane_bytes};
	b.bob_base = eng::Bytes<eng::PlaneTag> {pf1.real_base.ptr(), pf1.plane_bytes};
	b.scroll_off = off1;
	return b;
}

/// **Layout de pantalla por bandas**: compone varios tramos con geometría distinta (p.ej. un
/// dual playfield 3+3 arriba y una franja de 0 planos para efectos *copper chunky* abajo) sobre un
/// `copper::Scheduler`, sin que el juego nombre registros ni compute módulos.
///
/// ```cpp
/// eng::scene::RasterLayout layout {};
/// layout.add({ .planes = 6, .planes_view = pf, .dual_playfield = true });      // 3+3 de 0..207
/// layout.add({ .top = 208, .planes = 0, .color = false, .palette = colors,
///              .palette_colors = 32 });                                        // chunky 208..255
/// layout.materialize(sched);
/// ```
class RasterLayout {
public:
	static constexpr eng::u8 kMaxBands = 8u;

	/// Añade una banda (se ignoran las que superen `kMaxBands`).
	Band& add(const Band& b) noexcept {
		if (m_count < kMaxBands) {
			m_bands[m_count] = b;
			++m_count;
		}
		return m_bands[m_count == 0u ? 0u : static_cast<eng::u8>(m_count - 1u)];
	}
	[[nodiscard]] Band& operator[](eng::u8 i) noexcept { return m_bands[i]; }
	[[nodiscard]] const Band& operator[](eng::u8 i) const noexcept { return m_bands[i]; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }

	/// Materializa el campo: la banda 0 como display y las demás como `ModeSwitchZone` en su
	/// `top`. Devuelve `false` si una zona no es utilizable (DDF desalineado, planos fuera de
	/// rango, base ausente con `planes > 0`).
	template <class Scheduler>
	[[nodiscard]] bool materialize(Scheduler& sched) const {
		if (m_count == 0u) {
			return false;
		}
		const Band& b0 = m_bands[0];
		DisplayDesc d {};
		d.bplcon0 = b0.bplcon0();
		d.bplcon1 = b0.bplcon1;
		d.bplcon2 = b0.bplcon2;
		d.planes = b0.planes;
		d.bytes_per_row = b0.bytes_per_row;
		d.auto_mod = false;
		d.bpl1mod = b0.bpl1mod;
		d.bpl2mod = b0.bpl2mod;
		d.planes_view = b0.planes_view;
		d.ddfstrt = b0.ddfstrt;
		d.ddfstop = b0.ddfstop;
		emit_display(sched, d, false);
		emit_band_pointers(sched, b0, 0);
		if (!b0.palette.empty() && b0.palette_colors != 0u) {
			emit_palette(sched, b0.palette.data(), b0.palette_colors);
		}
		if (b0.split_active) {
			// Wrap del corkscrew: a mitad del tramo los planos vuelven al inicio del bucle.
			sched.wait_line(static_cast<eng::u16>(b0.top + b0.split_line));
			emit_band_pointers(sched, b0, b0.split_base_off);
		}
		for (eng::u8 i = 1u; i < m_count; ++i) {
			const Band& b = m_bands[i];
			eng::graphics::ModeSwitchZone z {};
			z.top = b.top;
			z.bplcon0 = b.bplcon0();
			z.set_bplcon1 = true;
			z.bplcon1 = b.bplcon1;
			z.ddfstrt = b.ddfstrt;
			z.ddfstop = b.ddfstop;
			z.bpl1mod = b.bpl1mod;
			z.bpl2mod = b.bpl2mod;
			z.planes = b.planes;
			z.plane_bytes = (b.plane_bytes != 0u) ? b.plane_bytes : b.bytes_per_row;
			z.bitplanes = b.planes_view;
			z.palette = b.palette;
			z.palette_colors = b.palette_colors;
			if (!sched.emit_mode_switch_zone(z)) {
				return false;
			}
			if (b.split_active) {
				// Wrap del corkscrew dentro del tramo.
				sched.wait_line(static_cast<eng::u16>(b.top + b.split_line));
				emit_band_pointers(sched, b, b.split_base_off);
			}
		}
		return true;
	}

private:
	Band m_bands[kMaxBands] {};
	eng::u8 m_count = 0u;
};

} // namespace eng::scene
