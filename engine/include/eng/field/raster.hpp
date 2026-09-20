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
///
/// ```text
///   Surface (API de dibujo uniforme)        Rasterizer (seam)              destino
///   ───────────────────────────────         ─────────────────              ───────
///   fill_rect / draw_line / fill_polygon ─► ¿CPU o Blitter? ──► CpuRaster     → píxeles (CPU)
///   blit                                    según RasterPolicy   BlitterRaster → Playfield::fill_polygon
///                                           vs RasterCaps(backend)              (PolygonFillSink / Blitter)
///   La app/escena fija la política (AccelMode::Auto + umbral); el consumidor no sabe qué hay detrás.
/// ```

#include <eng/core/box.hpp>
#include <eng/field/playfield.hpp>
#include <eng/graphics/c2p.hpp>
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

/// Conversión `eng::Box` → `ClipRect` inclusivo (bordes `x1`/`y1`).
[[nodiscard]] constexpr ClipRect clip_rect_of(const eng::Box& b) {
	return { b.x, b.y, b.right(), b.bottom() };
}

/// Conversión `ClipRect` → `eng::Box`.
[[nodiscard]] constexpr eng::Box box_of(const ClipRect& c) {
	return eng::Box::from_ltrb(static_cast<eng::s16>(c.x0), static_cast<eng::s16>(c.y0),
				   static_cast<eng::s16>(c.x1), static_cast<eng::s16>(c.y1));
}

/// **Recorta el segmento** `(x0,y0)-(x1,y1)` al rect `clip` (Cohen-Sutherland entero).
/// Devuelve `false` si no hay intersección; si `true`, deja el segmento recortado en los
/// mismos parámetros. Se usa antes de encolar una línea al Blitter (que no recorta). La
/// intersección usa una división de 32 bits (una vez por lado recortado, no por píxel).
[[nodiscard]] inline bool clip_segment(const ClipRect& clip, eng::s32& x0, eng::s32& y0,
				       eng::s32& x1, eng::s32& y1) {
	enum : int { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };
	auto code = [&](eng::s32 x, eng::s32 y) -> int {
		int c = 0;
		if (x < clip.x0) c |= kLeft;
		else if (x > clip.x1) c |= kRight;
		if (y < clip.y0) c |= kTop;
		else if (y > clip.y1) c |= kBottom;
		return c;
	};
	for (;;) {
		const int c0 = code(x0, y0);
		const int c1 = code(x1, y1);
		if ((c0 | c1) == 0) return true;   // dentro
		if ((c0 & c1) != 0) return false;  // fuera por el mismo lado
		const int c = (c0 != 0) ? c0 : c1;
		eng::s32 x = 0;
		eng::s32 y = 0;
		if ((c & kTop) != 0) {
			y = clip.y0;
			x = x0 + (x1 - x0) * (clip.y0 - y0) / (y1 - y0);
		} else if ((c & kBottom) != 0) {
			y = clip.y1;
			x = x0 + (x1 - x0) * (clip.y1 - y0) / (y1 - y0);
		} else if ((c & kRight) != 0) {
			x = clip.x1;
			y = y0 + (y1 - y0) * (clip.x1 - x0) / (x1 - x0);
		} else {
			x = clip.x0;
			y = y0 + (y1 - y0) * (clip.x0 - x0) / (x1 - x0);
		}
		if (c == c0) { x0 = x; y0 = y; } else { x1 = x; y1 = y; }
	}
}

/// **Colisión pixel-perfect por CPU** (referencia del camino Blitter): `true` si algún
/// bit de `a & b` (mismos planos, layout contiguo) está a 1 en el rect de `words`×`rows`
/// que empieza en `(x_word, y)`. El backend hace el AND con el Blitter (`$80`) y escanea
/// el resultado; esta es la misma prueba sin hardware.
[[nodiscard]] inline bool collide_cpu(eng::PlaneBytes a, eng::PlaneBytes b,
				      eng::u32 plane_bytes, eng::u16 row_bytes, eng::u8 planes,
				      eng::u16 x_word, eng::u16 y, eng::u16 words, eng::u16 rows) {
	if (a.empty() || b.empty() || planes == 0u) return false;
	for (eng::u8 p = 0; p < planes; ++p) {
		const eng::u8* pa = a.data() + static_cast<eng::u32>(p) * plane_bytes;
		const eng::u8* pb = b.data() + static_cast<eng::u32>(p) * plane_bytes;
		for (eng::u16 r = 0; r < rows; ++r) {
			const eng::u16* wa = reinterpret_cast<const eng::u16*>(
				pa + static_cast<eng::u32>(y + r) * row_bytes +
				static_cast<eng::u32>(x_word) * 2u);
			const eng::u16* wb = reinterpret_cast<const eng::u16*>(
				pb + static_cast<eng::u32>(y + r) * row_bytes +
				static_cast<eng::u32>(x_word) * 2u);
			for (eng::u16 i = 0; i < words; ++i) {
				if ((wa[i] & wb[i]) != 0u) return true;
			}
		}
	}
	return false;
}

/// **Conversión chunky→planar** pedida a través del seam: la misma llamada con CPU
/// (Kalms `c2p_1x1_4`) o Blitter detrás. `chunky` = 1 byte por pixel (nibble bajo =
/// índice), `planes` = destino planar contiguo, `plane_stride` = bytes entre planos.
struct C2pRequest {
	eng::ChunkyView chunky {};
	eng::PlaneBytes planes {};
	eng::u32 width = 0;
	eng::u32 height = 0;
	eng::u32 plane_stride = 0;
	eng::u8 plane_count = 4;
};

/// Interfaz de rasterizado. `Surface` no sabe qué implementación hay detrás.
class Rasterizer {
public:
	/// Rellena un rectángulo (ya recortado por `Surface`) con `color` y `op`.
	virtual bool fill_rect(Playfield& pf, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			       eng::u8 color, RasterOp op) = 0;
	/// Traza una línea recortada al `clip` (el rasterizador decide CPU/Blitter). Si
	/// `plan != nullptr` y el backend tiene Blitter, se encola una `Line` en el plan;
	/// `op == Xor` usa la variante **EOR/ONEDOT** (`LineEor`).
	virtual bool draw_line(Playfield& pf, const ClipRect& clip, eng::s32 x0, eng::s32 y0,
			       eng::s32 x1, eng::s32 y1, eng::u8 color,
			       graphics::FramePlan* plan = nullptr,
			       RasterOp op = RasterOp::Copy) = 0;
	/// Copia rectangular (blit planar): encola el trabajo en `plan`. `op` (`Or`/`And`/
	/// `Xor`) aplica la operación lógica con `B = D` (sombras/glow/máscaras).
	virtual bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
			       eng::u8 source_shift = 0u, bool descending = false,
			       RasterOp op = RasterOp::Copy) = 0;
	/// BOB enmascarado (cookie-cut): encola el trabajo en `plan`.
	virtual bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
				 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
				 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
				 eng::u8 source_shift = 0u) = 0;
	/// **Chunky→planar** bajo la misma interfaz: la CPU usa el merge de Kalms
	/// (`c2p_1x1_4`, o `c2p_1x1_naive` para 1..6 planos). El `BlitterRaster`, si recibe
	/// un `plan` y 4 planos, **encola** un `BlitJobKind::C2P` (el backend ejecuta las 13
	/// fases); sin plan, cae a CPU.
	virtual bool c2p(const C2pRequest& req, graphics::FramePlan* plan = nullptr) = 0;
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
		       graphics::FramePlan* plan = nullptr,
		       RasterOp op = RasterOp::Copy) override {
		(void)plan;
		(void)op; // el camino CPU dibuja con Copy (el EOR es del Blitter)
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
		       eng::u8 source_shift = 0u, bool descending = false,
		       RasterOp op = RasterOp::Copy) override {
		(void)plan;
		(void)source_shift;
		(void)descending;
		(void)op;
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
	/// Chunky→planar por CPU: `c2p_1x1_4` (4 planos) o `c2p_1x1_naive` (1..6).
	bool c2p(const C2pRequest& req, graphics::FramePlan* plan = nullptr) override {
		(void)plan; // la CPU convierte ya; no encola trabajo
		if (req.chunky.empty() || req.planes.empty() || req.width == 0u ||
		    req.height == 0u || req.plane_count == 0u) {
			return false;
		}
		if (req.plane_count == 4u) {
			graphics::c2p_1x1_4(req.width, req.height, req.plane_stride, req.chunky,
					    req.planes);
		} else {
			graphics::c2p_1x1_naive(req.width, req.height, req.plane_count,
						req.plane_stride, req.chunky, req.planes);
		}
		return true;
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
	/// Línea por **Blitter** si hay `plan`: recorta el segmento al clip (Cohen-Sutherland)
	/// y encola una `BlitJobKind::Line` por plano. Si no hay plan (o no cabe), CPU.
	bool draw_line(Playfield& pf, const ClipRect& clip, eng::s32 x0, eng::s32 y0,
		       eng::s32 x1, eng::s32 y1, eng::u8 color,
		       graphics::FramePlan* plan = nullptr,
		       RasterOp op = RasterOp::Copy) override {
		if (plan != nullptr) {
			eng::s32 cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1;
			if (clip_segment(clip, cx0, cy0, cx1, cy1) &&
			    pf.add_line(*plan, static_cast<eng::s16>(cx0), static_cast<eng::s16>(cy0),
					static_cast<eng::s16>(cx1), static_cast<eng::s16>(cy1), color,
					op == RasterOp::Xor)) {
				return true;
			}
		}
		return CpuRaster::draw_line(pf, clip, x0, y0, x1, y1, color, plan, op);
	}
	/// Copia por **Blitter**: encola el `CopyRect`/`LogicBlit` en el `FramePlan`.
	bool copy_rect(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
		       eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
		       eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
		       eng::u8 source_shift = 0u, bool descending = false,
		       RasterOp op = RasterOp::Copy) override {
		return pf.add_world_bitmap(plan, src, x, y, w, h, src_row_bytes, src_plane_stride,
					   planes, source_shift, descending, op);
	}
	/// BOB enmascarado por **Blitter**: encola el cookie-cut en el `FramePlan` (con shift).
	bool copy_masked(Playfield& pf, graphics::FramePlan& plan, eng::Span<const eng::u16> src,
			 eng::Span<const eng::u16> mask, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			 eng::u16 src_row_bytes, eng::u32 src_plane_stride, eng::u8 planes,
			 eng::u8 source_shift = 0u) override {
		return pf.add_world_bitmap_masked(plan, src, mask, x, y, w, h, src_row_bytes,
						  src_plane_stride, planes, source_shift);
	}
	/// C2P por **Blitter** si hay `plan` y 4 planos: encola `BlitJobKind::C2P` (el
	/// backend ejecuta las 13 fases). Sin plan (o distinto de 4 planos), CPU.
	bool c2p(const C2pRequest& req, graphics::FramePlan* plan = nullptr) override {
		if (plan != nullptr && req.plane_count == 4u && req.chunky.data() != nullptr &&
		    req.planes.data() != nullptr) {
			const eng::u32 px = req.width * req.height;
			if (px >= 2u && px / 2u <= 0xffffu) {
				graphics::BlitJob job {};
				job.c2p.chunky = const_cast<eng::u8*>(req.chunky.data());
				job.c2p.planes = req.planes.data();
				job.c2p.plane_stride = req.plane_stride;
				job.c2p.bytes = static_cast<eng::u16>(px / 2u);
				if (plan->add_c2p(job)) {
					return true;
				}
			}
		}
		return CpuRaster::c2p(req, plan);
	}
};

/// Instancias estáticas (sin heap): el llamador pasa `&kCpuRaster`/`&kBlitterRaster`.
inline CpuRaster kCpuRaster {};
inline BlitterRaster kBlitterRaster {};

} // namespace eng::field
