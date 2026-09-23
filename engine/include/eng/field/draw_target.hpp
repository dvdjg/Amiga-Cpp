#pragma once

/// \file draw_target.hpp
/// **Objetivo de dibujo**: agrupa `Surface` + `Rasterizer` + `FramePlan` + clip en un solo
/// valor para que el llamador no junte los tres a mano. Es la puerta única a las
/// primitivas (relleno, línea, texto, blit, chunky→planar) sobre un destino.
///
/// Motivo: antes, dibujar exigía coordinar `Scene::surface()`, el `Rasterizer` del
/// playfield y el `FramePlan`; y `Playfield` tenía que conocer el seam de C2P
/// (`rasterize_c2p`), lo que forzaba a definir un método de `Playfield` en `raster.hpp`
/// (incluye circular). Con `DrawTarget`, el C2P se resuelve contra el `Rasterizer` del
/// playfield y `Playfield` deja de conocer el seam.
///
/// ```cpp
/// eng::graphics::FramePlan plan;
/// auto dt = scene.draw_target(&plan);
/// dt.fill({10, 10, 40, 12}, color);
/// dt.line(0, 0, 319, 0, color);
/// dt.c2p(c2p_request);            // encola en `plan` si el rasterizador es Blitter
/// backend.execute_frame_plan(plan);
/// ```

#include <eng/core/types/box.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::field {

/// Destino de dibujo: `Surface` + `Rasterizer` + `FramePlan` (opcional).
class DrawTarget {
public:
	DrawTarget(Surface surface, eng::Ref<Rasterizer> rasterizer,
		   eng::Ref<graphics::FramePlan> plan) noexcept
		: m_surface(surface), m_rasterizer(rasterizer), m_plan(plan) {}

	[[nodiscard]] Surface& surface() noexcept { return m_surface; }
	[[nodiscard]] const Surface& surface() const noexcept { return m_surface; }
	[[nodiscard]] eng::Ref<graphics::FramePlan> plan() const noexcept { return m_plan; }
	/// Rasterizador efectivo del destino (el del playfield o el CPU por defecto; nunca nulo).
	[[nodiscard]] eng::NonNull<Rasterizer> rasterizer() const noexcept {
		return m_rasterizer.valid() ? eng::NonNull<Rasterizer>(*m_rasterizer)
					    : eng::NonNull<Rasterizer>(kCpuRaster);
	}
	[[nodiscard]] bool valid() const noexcept { return m_surface.valid(); }

	/// El clip del destino como `Box`.
	[[nodiscard]] eng::Box box() const noexcept { return box_of(m_surface.clip()); }

	// --- Primitivas (delegan en `Surface`, que recorta y enruta CPU/Blitter) ---
	bool fill(eng::Box b, eng::u8 color, RasterOp op = RasterOp::Copy) {
		return m_surface.fill_rect(b.x, b.y, b.w, b.h, color, op);
	}
	bool line(eng::s16 x0, eng::s16 y0, eng::s16 x1, eng::s16 y1, eng::u8 color,
		  RasterOp op = RasterOp::Copy) {
		return m_surface.draw_line(x0, y0, x1, y1, color, m_plan, op);
	}
	/// Marco de 1 px alrededor de `b` (4 líneas).
	bool frame(eng::Box b, eng::u8 color) {
		const eng::s16 x1 = b.right();
		const eng::s16 y1 = b.bottom();
		return line(b.x, b.y, x1, b.y, color) & line(b.x, y1, x1, y1, color) &
		       line(b.x, b.y, b.x, y1, color) & line(x1, b.y, x1, y1, color);
	}
	bool text(eng::s16 x, eng::s16 y, const char* s, eng::u8 color) {
		return m_surface.draw_text(x, y, s, color);
	}
	bool blit(graphics::FramePlan& plan, eng::Span<const eng::u16> src, eng::s32 x, eng::s32 y,
		  eng::u16 w, eng::u16 h, eng::u16 src_row_bytes, eng::u32 src_plane_stride,
		  eng::u8 planes, eng::u8 source_shift = 0u, bool descending = false,
		  RasterOp op = RasterOp::Copy) {
		return m_surface.blit(plan, src, x, y, w, h, src_row_bytes, src_plane_stride, planes,
				      source_shift, descending, op);
	}

	/// **Chunky→planar** por el seam: con `plan` encola un `BlitJobKind::C2P`; sin él,
	/// convierte ya con el rasterizador del playfield.
	///
	/// Se usa el rasterizador **Blitter** cuando hay `plan` (el del backend), para que el
	/// `BlitJobKind::C2P` se encole ahí; el `rasterizer()` del playfield suele ser el CPU y
	/// convertiría al instante sin usar el plan (bug: `Scene::c2p` devolvía `true` pero no
	/// llegaba al display). Sin `plan`, o fuera de 4 planos, se cae al CPU del playfield.
	bool c2p(const C2pRequest& req) {
		if (m_plan.valid()) {
			return eng::field::kBlitterRaster.c2p(req, m_plan);
		}
		return rasterizer()->c2p(req, {});
	}

private:
	Surface m_surface;
	eng::Ref<Rasterizer> m_rasterizer {};          ///< seam CPU/Blitter (no propietario)
	eng::Ref<graphics::FramePlan> m_plan {};       ///< cola de BlitJobs del frame (no propietaria)
};

} // namespace eng::field
