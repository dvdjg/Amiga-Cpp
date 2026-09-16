#pragma once

/// \file polygon_fill.hpp
/// Puente entre el seam `field::PolygonFillSink` y el **Blitter** del backend
/// Amiga: aporta el relleno de polígonos por hardware que un playfield instala con
/// `Playfield::set_polygon_fill_sink`. Así `Surface::fill_polygon` / el
/// `mesh_render_filled` rellenan caras sólidas por Blitter (máscara 1 bit +
/// cookie-cut) sin que el llamador vea registros ni planos.
///
/// La escena/demo aporta el backend y una **máscara scratch** de 1 bit en Chip RAM
/// (mismo `row_bytes` que el bitmap del playfield); aquí se empaqueta todo en el
/// sink. El motor vive en `MinimalBackend::blitter_fill_polygon_strided`.

#include <eng/field/playfield.hpp>
#include <eng/platform/amiga_minimal.hpp>

namespace eng::amiga {

/// Contexto vivo del relleno por Blitter. `mask` debe ser un plano 1 bit en CHIP
/// con al menos `row_bytes * bitmap_h` bytes (stride de fila = `row_bytes`).
struct PolygonFillService {
	MinimalBackend* backend = nullptr;
	eng::MaskBuffer mask {};

	static bool dispatch(void* ctx, eng::u8* plane_base, eng::u8 planes, eng::u32 plane_stride,
			     eng::u32 row_stride, eng::u16 row_bytes, eng::u16 bitmap_w, eng::u16 bitmap_h,
			     const eng::s16* xs, const eng::s16* ys, eng::u8 n, eng::u8 color) {
		auto* self = static_cast<PolygonFillService*>(ctx);
		if (self == nullptr || self->backend == nullptr) return false;
		return self->backend->blitter_fill_polygon_strided(
			plane_base, planes, plane_stride, row_stride, row_bytes, bitmap_w, bitmap_h,
			xs, ys, n, color, self->mask);
	}

	/// Sink listo para `Playfield::set_polygon_fill_sink`. No valida la máscara: el
	/// llamador garantiza su tamaño (`>= row_bytes*bitmap_h` en Chip RAM).
	eng::field::PolygonFillSink sink() { return eng::field::PolygonFillSink { this, &dispatch }; }
};

} // namespace eng::amiga
