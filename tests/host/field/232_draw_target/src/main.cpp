// ============================================================================
// Test HOST-232: DrawTarget (Surface + Rasterizer + FramePlan + clip).
// ============================================================================
//
// Valida que `DrawTarget` agrupa el destino y enruta las primitivas por el `Surface`
// (`fill`/`fill_run`/`line`/`frame`/`c2p`/`bitmap`), y que `box()` expone el clip del destino. Se enlaza un
// `ContiguousPlayfield` a memoria host (sin Chip), de modo que el test es puro.
//
// Las comprobaciones cuentan BITS por plano (no bytes exactos): el playfield escribe
// palabras y el orden de bits depende del endianness del host; el conteo es portable.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/field/232_draw_target

#include <cstdio>
#include <cstring>

#include <eng/api/screen.hpp>
#include <eng/core/types/box.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/bitmap_view.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kW = 32;
constexpr eng::u16 kH = 8;
constexpr eng::u16 kRow = 4;                // (32/8) redondeado a 4
constexpr eng::u32 kPlaneBytes = kRow * kH; // 32

eng::u32 bits_in_plane(const eng::u8* planes, eng::u32 p) {
	eng::u32 n = 0;
	for (eng::u32 i = 0; i < kPlaneBytes; ++i) {
		eng::u8 b = planes[p * kPlaneBytes + i];
		while (b != 0u) {
			n += static_cast<eng::u32>(b & 1u);
			b = static_cast<eng::u8>(b >> 1u);
		}
	}
	return n;
}

} // namespace

int main() {
	alignas(2) eng::u8 planes[kPlaneBytes * 4u] {};
	eng::playfield::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, 4u), "bind_raw del playfield");

	eng::playfield::Surface surf {pf, eng::playfield::SurfaceRect {0, 0, kW, kH}};
	eng::playfield::DrawTarget dt {surf, &eng::playfield::kCpuRaster,
				   nullptr};
	check(dt.valid(), "DrawTarget valido");
	check(dt.box().w == kW && dt.box().h == kH, "box() = clip del destino");

	// fill: color 1 -> plano 0 con 4x2 = 8 bits.
	check(dt.fill(eng::Box {0, 0, 4u, 2u}, 1u), "fill");
	check(bits_in_plane(planes, 0) == 8u, "fill: 8 bits en el plano 0");
	check(bits_in_plane(planes, 1) == 0u, "fill no toca otros planos");

	// line: color 2 -> plano 1 con 32 bits (fila completa).
	check(dt.line(0, 0, static_cast<eng::s16>(kW - 1), 0, 2u), "line");
	check(bits_in_plane(planes, 1) == kW, "line: fila completa en el plano 1");

	// frame: color 4 -> plano 2 con el borde (2 filas + 2 columnas interiores).
	check(dt.frame(eng::Box {0, 0, kW, kH}, 4u), "frame");
	check(bits_in_plane(planes, 2) == 2u * kW + 2u * (kH - 2u), "frame: perimetro en el plano 2");

	// c2p: 16x1 chunky con indice 8 -> plano 3 con 16 bits.
	eng::u8 chunky[16u] {};
	for (eng::u32 i = 0; i < 16u; ++i) {
		chunky[i] = 8u; // solo el bit 3
	}
	const eng::playfield::C2pRequest req {
		eng::Block<eng::ChunkyTag> {eng::Bytes<eng::ChunkyTag> {chunky, sizeof(chunky)},
					    eng::MemoryKind::Chip},
		eng::Block<eng::PlaneTag> {eng::Bytes<eng::PlaneTag> {planes, sizeof(planes)},
					   eng::MemoryKind::Chip},
		16u,
		1u,
		kPlaneBytes,
		4u,
	};
	check(dt.c2p(req), "c2p por el seam");
	check(bits_in_plane(planes, 3) == 16u, "c2p: 16 bits en el plano 3");

	// circle: contorno de radio 3 en (16,4), color 2 -> plano 1.
	// Extremos por eje + diagonales del punto medio; el centro NO pertenece al contorno.
	eng::Screen screen {dt};
	check(screen.valid(), "Screen sobre el DrawTarget");
	check(screen.circle(16, 4, 3, 2u), "circle");
	// El playfield escribe la palabra como VALOR u16; el test la lee igual (memcpy) y
	// consulta el bit con el orden Amiga (bit 15 = píxel izquierdo), portable al host.
	auto bit_at = [&](eng::u16 x, eng::u16 y) {
		const eng::u32 off = static_cast<eng::u32>(y) * kRow + static_cast<eng::u32>(x >> 4u) * 2u;
		eng::u16 word = 0u;
		std::memcpy(&word, planes + kPlaneBytes + off, sizeof(word));
		return (word & static_cast<eng::u16>(0x8000u >> (x & 15u))) != 0u;
	};
	check(bit_at(13, 4) && bit_at(19, 4), "circle: extremos horizontales (16±3,4)");
	check(bit_at(16, 1) && bit_at(16, 7), "circle: extremos verticales (16,4±3)");
	check(bit_at(14, 2) && bit_at(18, 2), "circle: diagonal del octante (16±2,2)");
	check(!bit_at(16, 4), "circle: el centro NO se pinta (es contorno)");
	// Radio 0: un solo punto en el centro.
	check(screen.circle(8, 4, 0, 2u), "circle radio 0");
	check(bit_at(8, 4), "circle radio 0 pinta el centro");
	// Fuera del clip por completo: true sin dibujar.
	check(screen.circle(200, 4, 5, 2u), "circle fuera del clip no falla");
	// Xor sin duplicados: dos pasadas del MISMO círculo se cancelan exactamente (si el
	// algoritmo repitiera un punto del eje/diagonal, ese píxel quedaría encendido).
	const eng::u32 p1_before = bits_in_plane(planes, 1);
	check(screen.circle(24, 4, 3, 2u, eng::playfield::RasterOp::Xor), "circle Xor");
	const eng::u32 p1_xor = bits_in_plane(planes, 1);
	check(p1_xor > p1_before, "circle Xor enciende puntos");
	check(screen.circle(24, 4, 3, 2u, eng::playfield::RasterOp::Xor), "circle Xor segunda pasada");
	check(bits_in_plane(planes, 1) == p1_before, "circle Xor se cancela sin puntos duplicados");

	// fill_run: tramo horizontal por CPU (el materializador de capas indexadas emite uno por
	// run); mismo resultado que `fill` de una fila, con recorte al clip.
	const eng::u32 p2_before = bits_in_plane(planes, 2);
	check(screen.fill_run(eng::Box {4, 3, 8u, 1u}, 4u), "fill_run");
	check(bits_in_plane(planes, 2) == p2_before + 8u, "fill_run: 8 bits nuevos en el plano 2");
	// Tramo que cruza el borde izquierdo: se recorta (x=0 ya lo pintó el frame -> 3 nuevos).
	check(screen.fill_run(eng::Box {-4, 3, 8u, 1u}, 4u), "fill_run recortado por la izquierda");
	check(bits_in_plane(planes, 2) == p2_before + 11u, "fill_run: recorte deja 3 px nuevos");
	// Fuera del clip: false sin dibujar.
	check(!screen.fill_run(eng::Box {-8, 3, 8u, 1u}, 4u), "fill_run fuera del clip");
	check(!screen.fill_run(eng::Box {4, 100, 8u, 1u}, 4u), "fill_run fuera por abajo");

	// bitmap(): construcción del job de ventana (origen + shift fino + módulo del stride del asset).
	{
		eng::graphics::FramePlan plan {};
		eng::playfield::Surface surf2 {pf, eng::playfield::SurfaceRect {0, 0, kW, kH}};
		const eng::graphics::BobTarget bt = eng::graphics::make_bob_target(
			eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(planes), sizeof(planes)},
			kRow, kH, 4u, eng::graphics::BobLayout::Planar);
		eng::playfield::DrawTarget dt2 {surf2, &eng::playfield::kCpuRaster, &plan, bt};
		eng::Screen scr2 {dt2};
		// Asset de 32 px con fila de 8 B; ventana de 16 px en x=16 con shift 4 (necesita la
		// palabra extra: 2 B de origen + 2 copiados + 2 de guarda = 6 <= 8).
		eng::u8 asset_bytes[8u * kH * 4u] {};
		eng::graphics::ChipBitmapView<eng::PlaneTag> asset {};
		asset.planes = eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(asset_bytes), sizeof(asset_bytes)};
		asset.width = kW;
		asset.height = kH;
		asset.row_bytes = 8u;
		asset.plane_count = 4u;
		asset.layout = eng::graphics::PlaneLayout::Contiguous;
		check(scr2.bitmap(asset, eng::Box {0, 0, 16u, kH}, 16u, 4u), "bitmap ventana + shift");
		check(plan.blit_job_count() == 1u, "bitmap: un solo job (4 planos)");
		if (plan.blit_job_count() == 1u) {
			const eng::graphics::BlitJob& j = plan.blit_job(0u);
			check(j.kind == eng::graphics::BlitJobKind::CopyRect, "bitmap: CopyRect");
			check(j.words_per_row == 1u && j.height == kH, "bitmap: 1 palabra x altura");
			check(j.source_shift == 4u, "bitmap: shift fino");
			check(j.source_modulo_bytes == 6, "bitmap: modulo = stride(8) - copiado(2)");
			check(j.destination_modulo_bytes == 2, "bitmap: destino 32 px copiando 16 -> modulo 2");
			check(j.bitplane_count == 4u, "bitmap: 4 planos en un job");
		}
		// La guarda del shift no cabe en la fila del asset (6 + 2 + 2 > 8): rechazado.
		check(!scr2.bitmap(asset, eng::Box {0, 0, 16u, kH}, 48u, 4u), "bitmap: guarda fuera de fila");
	}

	if (failures == 0) {
		std::printf("OK: DrawTarget (fill/fill_run/line/frame/c2p/circle/bitmap + box) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
