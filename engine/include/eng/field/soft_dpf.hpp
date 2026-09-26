#pragma once

/// \file soft_dpf.hpp
/// Composición **soft DPF** (RoboCod): un plano de fondo de 1 bit (vista de los
/// planos de OTRO bitmap) con **doble buffer**, su patrón y los blits de copia.
/// Es la pieza que estaba embebida en `XLimitedPlayfield`; separarla deja el
/// playfield como puro scroll y la composición como concepto reutilizable.
///
/// Incluye los helpers puros y host-testables del fondo: offsets de parallax/fijo,
/// reparto del offset al barrel shifter (`bg_shift_for`), ventana horizontal
/// (`bg_window_for`) y descomposición en 1-2 rects por el split del corkscrew
/// (`bg_split_rects`).
///
/// Ver `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.2 y
/// `docs/reference/amiga/techniques/robocod-layered-scroll.md`.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/plane_view.hpp>
#include <eng/graphics/bitmap.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Offset en píxeles de la ventana del patrón para un fondo con **parallax**
/// (velocidad `1/div`): `src = -camx*(div-1)/div`, envuelto en `[0, period_px)`.
/// `div=1` devuelve 0 (mismo fondo); `div=0` se trata como 1. Con pasos de cámara
/// múltiplos de `div` el offset avanza 1 píxel por frame y el barrel shifter del
/// Blitter reparte los 4 bits bajos -> continuidad sin saltos de columna.
constexpr s32 parallax_pattern_offset_px(s32 camx, u8 div, u16 period_px) {
	if (period_px == 0u || div <= 1u) return 0;
	s32 off = -(camx * static_cast<s32>(div - 1u) / static_cast<s32>(div));
	off %= static_cast<s32>(period_px);
	if (off < 0) off += static_cast<s32>(period_px);
	return off;
}

/// Offset en píxeles de la ventana del patrón para un fondo **FIJO** en pantalla
/// (velocidad 0): anula TODO el scroll X (`src = -camx`, envuelto en `[0,P)`).
constexpr s32 fixed_bg_offset_px(s32 camx, u16 period_px) {
	if (period_px == 0u) return 0;
	s32 off = -camx % static_cast<s32>(period_px);
	if (off < 0) off += static_cast<s32>(period_px);
	return off;
}

/// Desglose del blit de fondo en **1 o 2 rectángulos** para compensar el split
/// vertical del corkscrew: la ventana visible puede ser un tramo o dos; el fondo
/// se muestrea SIEMPRE en filas contiguas `[bg_y, bg_y+viewport_h)`.
struct BgSplitRects {
	u16 dest_row[2] = {0u, 0u};
	u16 src_y[2] = {0u, 0u};
	u16 rows[2] = {0u, 0u};
	u8 count = 1u;
};

/// Desglosa el blit de fondo en 1 o 2 rectángulos que compensan el **split vertical** del
/// corkscrew (`d` = desplazamiento del split en filas). El fondo se muestrea siempre en filas
/// contiguas `[bg_y, bg_y+viewport_h)`. Devuelve `BgSplitRects` (1 rect, o 2 si el split cae
/// dentro de la ventana). Lo usa el driver corkscrew / soft-DPF.
constexpr BgSplitRects bg_split_rects(u16 d, u16 display_h, u16 viewport_h, u16 bg_y) {
	BgSplitRects r {};
	if (display_h == 0u || viewport_h == 0u) { r.count = 0u; return r; }
	d = static_cast<u16>(d % display_h);
	const u16 split = static_cast<u16>(display_h - d);
	if (split >= viewport_h) {
		r.dest_row[0] = d;
		r.src_y[0] = bg_y;
		r.rows[0] = viewport_h;
		r.count = 1u;
	} else {
		r.dest_row[0] = d;
		r.src_y[0] = bg_y;
		r.rows[0] = split;
		r.dest_row[1] = 0u;
		r.src_y[1] = static_cast<u16>(bg_y + split);
		r.rows[1] = static_cast<u16>(viewport_h - split);
		r.count = 2u;
	}
	return r;
}

/// Reparto de un offset horizontal (px) al **barrel shifter** del Blitter.
/// `destino[d] = patrón[q + d - S]` con `q = src_x + S`, `S = (-src_x) & 15`.
struct BgShift {
	u16 word_bytes = 0;   // offset en bytes de la word donde apuntar el canal A
	u8 shift = 0;         // 0..15
};

/// Reparte un offset horizontal en píxeles al **barrel shifter** del Blitter: devuelve el
/// `BgShift` (offset de word para el canal A + `shift` 0..15) tal que el píxel `src_x` cae en
/// el píxel 0 del destino.
constexpr BgShift bg_shift_for(u16 src_x_pixels) {
	const u16 s = graphics::fine_delay(src_x_pixels);
	const u16 q = static_cast<u16>(src_x_pixels + s);
	return { static_cast<u16>((q / 16u) * 2u), static_cast<u8>(s) };
}

/// Ventana horizontal del blit de fondo: copia solo lo que el display puede leer
/// + 1 word de guarda, empezando en `dest_byte_off` con `words` words. `src_x` es
/// el píxel de patrón que debe verse en el píxel 0 (imagen fija).
struct BgWindow {
	u16 dest_byte_off = 0;
	u16 words = 0;
	u16 src_x = 0;
};

/// Calcula la **ventana horizontal** del blit de fondo (`BgWindow`): copia lo que el display
/// puede leer + 1 word de guarda, con `dest_byte_off`/`words` y `src_x` (píxel de patrón
/// visible en el píxel 0). Usa `fixed_bg_offset_px` (definido arriba) y `camx`/`period_px`.
constexpr BgWindow bg_window_for(s32 camx, u16 period_px, u16 fetch_bytes) {
	BgWindow w {};
	if (period_px == 0u || fetch_bytes < 2u) return w;
	const u16 pa = static_cast<u16>(((camx + 15) / 16) * 2); // planeaddx (bytes)
	const u16 dest = pa >= 2u ? static_cast<u16>(pa - 2u) : 0u;
	w.dest_byte_off = dest;
	w.words = static_cast<u16>((pa + fetch_bytes - dest) / 2u);
	s32 base = fixed_bg_offset_px(camx, period_px) + static_cast<s32>(dest) * 8;
	base %= static_cast<s32>(period_px);
	if (base < 0) base += static_cast<s32>(period_px);
	w.src_x = static_cast<u16>(base);
	return w;
}

/// Rellena el plano de fondo con el patrón procedural (bandas diagonales, periodo
/// 64/ancho 32). El scroll de ese plano lo da su `BPLxPT` (no se repinta por frame).
inline void fill_parallax_pattern(eng::u8* frontbuffer, u16 bytes_per_row, eng::u8 planes,
                                  eng::u8 parallax_plane, u16 bitmap_width, u16 bitmap_height) {
	if (frontbuffer == nullptr || parallax_plane >= planes) return;
	const eng::u32 row = bytes_per_row;
	for (eng::u32 y = 0; y < bitmap_height; ++y) {
		eng::u8* pl = frontbuffer + (y * planes + parallax_plane) * row;
		for (eng::u32 x = 0; x < bitmap_width; ++x) {
			if (((x + y) & 63u) >= 32u) continue;
			const eng::u32 wb = (x / 8u) & ~1u;
			const eng::u16 m = static_cast<eng::u16>(0x8000u >> (x & 15u));
			pl[wb] = static_cast<eng::u8>(pl[wb] | (m >> 8));
			pl[wb + 1u] = static_cast<eng::u8>(pl[wb + 1u] | (m & 0xffu));
		}
	}
}

/// Composición soft DPF: vista del plano de fondo (`PlaneView`, doble buffer
/// opcional) + geometría + construcción de los blits de copia. No posee el bitmap
/// principal ni el patrón (los aporta el llamador/demo).
class SoftDpfComposition {
public:
	struct Geometry {
		u16 bytes_per_row = 0;
		u16 display_height = 0;
		u8 planes = 0;
		u8 parallax_plane = 0xffu;
	};

	void configure(const Geometry& g) { m_geo = g; }
	constexpr bool active() const { return m_geo.parallax_plane < m_geo.planes; }
	constexpr bool double_buffered() const { return m_view.double_buffered(); }
	void flip() { m_view.flip(); }
	[[nodiscard]] eng::BitmapBase display_base() const { return m_view.display_base(); }
	[[nodiscard]] eng::FrontBase write_base() const { return m_view.write_base(); }

	/// Enlace crudo del bitmap principal y del extra (tests o memoria ya gestionada).
	void bind_raw(eng::BitmapBase main_real, eng::FrontBase main_front,
	              eng::BitmapBase extra_real, eng::FrontBase extra_front) {
		m_view.bind_raw(main_real, main_front, extra_real, extra_front);
	}

	/// Enlaza el bitmap principal y (si la composición está activa) reserva el
	/// bloque extra con el mismo layout.
	bool init(eng::MemorySystem& memory, const eng::gfx::BitmapConfig& bc,
	          eng::gfx::Bitmap& main) {
		m_view.bind_single(main.base(), main.front());
		if (active() && !m_view.enable_double_buffer(memory, bc)) return false;
		return true;
	}

	/// Blit de copia del patrón al plano de fondo (ventana `words`, `rows` filas).
	/// `pattern` es una vista con dominio y tamaño: si el rectángulo pedido se sale
	/// del patrón (o no hay patrón), dispara `illegal` en vez de corromper memoria.
	eng::graphics::BlitJob make_copy_rect_job(eng::Pattern pattern, u16 pattern_row_bytes,
	                                          u16 src_x_pixels, u16 src_y,
	                                          u16 dest_row, u16 rows,
	                                          u16 dest_byte_off, u16 words) const {
		const u16 row = m_geo.bytes_per_row;
		const u16 pat_row = pattern_row_bytes ? pattern_row_bytes : row;
		const BgShift bs = bg_shift_for(src_x_pixels);
		const eng::u32 first = static_cast<eng::u32>(src_y) * pat_row + bs.word_bytes;
		const eng::u32 width_bytes = static_cast<eng::u32>(words) * 2u;
		const eng::u32 last = first + (rows != 0u
			? static_cast<eng::u32>(rows - 1u) * pat_row + width_bytes : 0u);
		if (pattern.data() == nullptr || last > pattern.size()) eng::detail::typed_range_error();
		const eng::u8* src = pattern.data() + first;
		eng::u8* dst_base = m_view.write_base().value.ptr();
		eng::u16* dst = reinterpret_cast<eng::u16*>(dst_base +
			(static_cast<eng::u32>(dest_row) * m_geo.planes + m_geo.parallax_plane) * row +
			dest_byte_off);
		return { eng::graphics::BlitJobKind::TileBlockCopy, eng::graphics::BlitSource {},
		         eng::graphics::BlitSource { reinterpret_cast<const eng::u16*>(src) },
		         eng::graphics::BlitDest { dst },
		         words, rows,
		         static_cast<s16>(pat_row - width_bytes),
		         static_cast<s16>(row * m_geo.planes - width_bytes),
		         1, bs.shift, 2, 2, false };
	}

	/// Compatibilidad: copia la fila completa del anillo (`display_height` filas).
	eng::graphics::BlitJob make_copy_job(eng::Pattern pattern, u16 pattern_row_bytes,
	                                     u16 src_x_pixels, u16 src_y) const {
		return make_copy_rect_job(pattern, pattern_row_bytes, src_x_pixels, src_y, 0,
		                          m_geo.display_height, 0,
		                          static_cast<u16>(m_geo.bytes_per_row / 2u));
	}

private:
	PlaneView m_view {};
	Geometry m_geo {};
};

} // namespace eng::field
