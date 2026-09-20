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

/// Interfaz de rasterizado. `Surface` no sabe qué implementación hay detrás.
class Rasterizer {
public:
	/// Rellena un rectángulo (ya recortado por `Surface`) con `color` y `op`.
	virtual bool fill_rect(Playfield& pf, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			       eng::u8 color, RasterOp op) = 0;
	/// Copia rectangular (blit planar): encola el trabajo en `plan`.
	virtual bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes) = 0;
	/// BOB enmascarado (cookie-cut): encola el trabajo en `plan`.
	virtual bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
				 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
				 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes) = 0;
};

/// Rasterizador **CPU**: relleno por scanline (`Playfield::draw_span_op`, con
/// `RasterOp`) y copias por el blit del playfield (el `FramePlan` las ejecuta en el
/// backend). Es el rasterizador por defecto.
class CpuRaster : public Rasterizer {
public:
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
	/// Copia rectangular por **CPU** (`Playfield::copy_rect_cpu`, con ruta de 32 bits en
	/// 68020+); no encola trabajo.
	bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
		       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes) override {
		(void)plan;
		return pf.copy_rect_cpu(src, x, y, w, h, src_row_bytes, src_plane_stride, planes);
	}
	/// BOB enmascarado por **CPU** (`Playfield::copy_masked_cpu`); no encola trabajo.
	bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes) override {
		(void)plan;
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
		const eng::s16 xs[4] = {static_cast<eng::s16>(x), static_cast<eng::s16>(x + w - 1),
					static_cast<eng::s16>(x + w - 1), static_cast<eng::s16>(x)};
		const eng::s16 ys[4] = {static_cast<eng::s16>(y), static_cast<eng::s16>(y),
					static_cast<eng::s16>(y + h - 1), static_cast<eng::s16>(y + h - 1)};
		return pf.fill_polygon(xs, ys, 4u, color);
	}
	/// Copia por **Blitter**: encola el `CopyRect` en el `FramePlan`.
	bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
		       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes) override {
		return pf.add_world_bitmap(plan, src, x, y, w, h, src_row_bytes, src_plane_stride, planes);
	}
	/// BOB enmascarado por **Blitter**: encola el cookie-cut en el `FramePlan`.
	bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes) override {
		return pf.add_world_bitmap_masked(plan, src, mask, x, y, w, h, src_row_bytes,
						  src_plane_stride, planes);
	}
};

/// Instancias estáticas (sin heap): el llamador pasa `&kCpuRaster`/`&kBlitterRaster`.
inline CpuRaster kCpuRaster {};
inline BlitterRaster kBlitterRaster {};

} // namespace eng::field
