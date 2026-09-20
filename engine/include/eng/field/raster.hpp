#pragma once

/// \file raster.hpp
/// **Seam de rasterizado**: `Surface` pide operaciones de dibujo y un `Rasterizer`
/// decide si las ejecuta la **CPU** o el **Blitter**, según la `RasterPolicy` (la
/// elige la app/escena) contrastada con las `RasterCaps` (las declara el backend).
///
/// Así la API de dibujo es uniforme (`Surface::fill_rect`/`draw_line`/`fill_polygon`/
/// `blit`) y el consumidor **no** sabe si detrás hay CPU o Blitter:
///
/// ```cpp
/// // backend OCS: Blitter de 16 bits; AGA: FMODE 32/64; host: CPU
/// pf.set_raster_policy({AccelMode::Auto, /*min_blit_pixels=*/128, /*cpu_fast=*/true});
/// pf.set_rasterizer(&kBlitterRaster);   // o &kCpuRaster
/// surface.fill_rect(x, y, w, h, color, RasterOp::Xor); // misma llamada
/// ```
///
/// Estado: `CpuRaster` implementa el relleno por CPU (con `RasterOp`) y delega las
/// copias en el blit del playfield; `BlitterRaster` enruta el relleno por
/// `Playfield::fill_polygon` (que usa el `PolygonFillSink`/Blitter si está instalado).
/// El relleno de rect **directo por Blitter** (BLTCON fill) y la copia **por CPU**
/// (con rutas de 32 bits / *blit-assist* en 68020+) quedan como extensión del seam.

#include <eng/field/playfield.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::field {

/// **Rectángulo de recorte** inclusivo (en píxeles) que `Surface` pasa al rasterizador.
/// Evita que `raster.hpp` dependa de `surface.hpp` (que lo incluye) y permite que un
/// futuro rasterizador Blitter recorte la línea antes de emitirla.
struct ClipRect {
	eng::s32 x0 = 0;
	eng::s32 y0 = 0;
	eng::s32 x1 = 0;
	eng::s32 y1 = 0;
};

/// Interfaz de rasterizado. `Surface` no sabe qué implementación hay detrás.
class Rasterizer {
public:
	/// Rellena un rectángulo (ya recortado por `Surface`) con `color` y `op`.
	virtual bool fill_rect(Playfield& pf, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			       eng::u8 color, RasterOp op) = 0;
	/// Traza una línea recortada al `clip` (el rasterizador decide CPU/Blitter). Si
	/// `plan != nullptr` y el backend tiene Blitter, se encola una `Line` en el plan.
	virtual bool draw_line(Playfield& pf, const ClipRect& clip, eng::s32 x0, eng::s32 y0,
			       eng::s32 x1, eng::s32 y1, eng::u8 color,
			       graphics::FramePlan* plan = nullptr) = 0;
	/// Copia rectangular (blit planar): encola el trabajo en `plan`.
	virtual bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
			       eng::u8 source_shift = 0u, bool descending = false) = 0;
	/// BOB enmascarado (cookie-cut): encola el trabajo en `plan`.
	virtual bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
				 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
				 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
				 eng::u8 source_shift = 0u) = 0;
};

/// Rasterizador **CPU**: relleno por scanline (`Playfield::draw_span_op`, con
/// `RasterOp`) y copias por el blit del playfield (el `FramePlan` las ejecuta en el
/// backend). Es el rasterizador por defecto.
class CpuRaster : public Rasterizer {
public:
	/// Rellena un rectángulo por CPU (`Playfield::draw_span_op`).
	bool fill_rect(Playfield& pf, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u8 color, RasterOp op) override {
		if (!pf.initialized() || w == 0u || h == 0u) return false;
		const eng::s32 x1 = x + static_cast<eng::s32>(w) - 1;
		const eng::s32 y1 = y + static_cast<eng::s32>(h) - 1;
		for (eng::s32 gy = y; gy <= y1; ++gy) {
			pf.draw_span_op(x, x1, gy, color, op);
		}
		return true;
	}
	/// Línea por CPU (Bresenham), recortada al `clip` (un tramo horizontal usa `draw_span`).
	bool draw_line(Playfield& pf, const ClipRect& clip, eng::s32 x0, eng::s32 y0,
		       eng::s32 x1, eng::s32 y1, eng::u8 color,
		       graphics::FramePlan* plan = nullptr) override {
		(void)plan;
		if (y0 == y1) {
			if (y0 < clip.y0 || y0 > clip.y1) return false;
			eng::s32 a = x0 < x1 ? x0 : x1;
			eng::s32 b = x0 < x1 ? x1 : x0;
			const bool inside = a >= clip.x0 && b <= clip.x1;
			if (a < clip.x0) a = clip.x0;
			if (b > clip.x1) b = clip.x1;
			if (b < a) return false;
			return pf.draw_span(a, b, y0, color) && inside;
		}
		const eng::s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
		const eng::s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
		const eng::s32 sx = x0 < x1 ? 1 : -1;
		const eng::s32 sy = y0 < y1 ? 1 : -1;
		eng::s32 err = dx - dy;
		bool ok = true;
		for (;;) {
			if (x0 >= clip.x0 && x0 <= clip.x1 && y0 >= clip.y0 && y0 <= clip.y1) {
				pf.write_pixel(x0, y0, color);
			} else {
				ok = false;
			}
			if (x0 == x1 && y0 == y1) break;
			const eng::s32 e2 = 2 * err;
			if (e2 > -dy) { err -= dy; x0 += sx; }
			if (e2 < dx) { err += dx; y0 += sy; }
		}
		return ok;
	}
	/// Copia rectangular por **CPU** (`Playfield::copy_rect_cpu`, con ruta de 32 bits en
	/// 68020+); no encola trabajo. `source_shift`/`descending` no aplican al camino CPU.
	bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
		       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
		       eng::u8 source_shift = 0u, bool descending = false) override {
		(void)plan;
		(void)source_shift;
		(void)descending;
		return pf.copy_rect_cpu(src, x, y, w, h, src_row_bytes, src_plane_stride, planes);
	}
	/// BOB enmascarado por **CPU** (`Playfield::copy_masked_cpu`); no encola trabajo.
	bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
			 eng::u8 source_shift = 0u) override {
		(void)plan;
		(void)source_shift;
		return pf.copy_masked_cpu(src, mask, x, y, w, h, src_row_bytes, src_plane_stride, planes);
	}
};

/// Rasterizador que **prefiere el Blitter** para el relleno: enruta el rect a
/// `Playfield::fill_polygon`, que usa el `PolygonFillSink`/Blitter si está instalado
/// (si no, cae al relleno CPU). Las copias van por el blit del playfield.
class BlitterRaster : public CpuRaster {
public:
	bool fill_rect(Playfield& pf, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u8 color, RasterOp op) override {
		if (op != RasterOp::Copy) return CpuRaster::fill_rect(pf, x, y, w, h, color, op);
		if (!pf.initialized() || w == 0u || h == 0u) return false;
		// `Auto` decide por coste: usa el Blitter si hay sink y el área supera el umbral.
		const RasterPolicy& pol = pf.raster_policy();
		const eng::u32 area = static_cast<eng::u32>(w) * static_cast<eng::u32>(h);
		const bool use_blit =
			(pol.mode == AccelMode::Blitter) ||
			(pol.mode == AccelMode::Auto && pf.has_fill_sink() && area >= pol.min_blit_pixels);
		if (!use_blit) return CpuRaster::fill_rect(pf, x, y, w, h, color, op);
		const eng::s16 xs[4] = {static_cast<eng::s16>(x), static_cast<eng::s16>(x + w - 1),
					static_cast<eng::s16>(x + w - 1), static_cast<eng::s16>(x)};
		const eng::s16 ys[4] = {static_cast<eng::s16>(y), static_cast<eng::s16>(y),
					static_cast<eng::s16>(y + h - 1), static_cast<eng::s16>(y + h - 1)};
		return pf.fill_polygon(xs, ys, 4u, color);
	}
	/// Línea por **Blitter** si hay `plan` y la línea cae **dentro** del clip (el Blitter
	/// no recorta): encola una `BlitJobKind::Line` por plano. Si no, CPU (Bresenham).
	bool draw_line(Playfield& pf, const ClipRect& clip, eng::s32 x0, eng::s32 y0,
		       eng::s32 x1, eng::s32 y1, eng::u8 color,
		       graphics::FramePlan* plan = nullptr) override {
		if (plan != nullptr) {
			const eng::s32 xa = x0 < x1 ? x0 : x1;
			const eng::s32 xb = x0 < x1 ? x1 : x0;
			const eng::s32 ya = y0 < y1 ? y0 : y1;
			const eng::s32 yb = y0 < y1 ? y1 : y0;
			const bool inside = xa >= clip.x0 && xb <= clip.x1 &&
					    ya >= clip.y0 && yb <= clip.y1;
			if (inside &&
			    pf.add_line(*plan, static_cast<eng::s16>(x0), static_cast<eng::s16>(y0),
					static_cast<eng::s16>(x1), static_cast<eng::s16>(y1), color)) {
				return true;
			}
		}
		return CpuRaster::draw_line(pf, clip, x0, y0, x1, y1, color, plan);
	}
	/// Copia por **Blitter**: encola el `CopyRect` en el `FramePlan` (con shift/DESC).
	bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
		       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
		       eng::u8 source_shift = 0u, bool descending = false) override {
		return pf.add_world_bitmap(plan, src, x, y, w, h, src_row_bytes, src_plane_stride,
					   planes, source_shift, descending);
	}
	/// BOB enmascarado por **Blitter**: encola el cookie-cut en el `FramePlan` (con shift).
	bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
			 eng::u8 source_shift = 0u) override {
		return pf.add_world_bitmap_masked(plan, src, mask, x, y, w, h, src_row_bytes,
						  src_plane_stride, planes, source_shift);
	}
};

/// Instancias estáticas (sin heap): el llamador pasa `&kCpuRaster`/`&kBlitterRaster`.
inline CpuRaster kCpuRaster {};
inline BlitterRaster kBlitterRaster {};

} // namespace eng::field
