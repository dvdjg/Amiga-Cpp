// ============================================================================
// Test HOST-232: DrawTarget (Surface + Rasterizer + FramePlan + clip).
// ============================================================================
//
// Valida que `DrawTarget` agrupa el destino y enruta las primitivas por el `Surface`
// (`fill`/`line`/`frame`/`c2p`), y que `box()` expone el clip del destino. Se enlaza un
// `ContiguousPlayfield` a memoria host (sin Chip), de modo que el test es puro.
//
// Las comprobaciones cuentan BITS por plano (no bytes exactos): el playfield escribe
// palabras y el orden de bits depende del endianness del host; el conteo es portable.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/232_draw_target

#include <cstdio>

#include <eng/core/box.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>

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
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, 4u), "bind_raw del playfield");

	eng::field::Surface surf {pf, eng::field::SurfaceRect {0, 0, kW, kH}};
	eng::field::DrawTarget dt {surf, eng::Ref<eng::field::Rasterizer>(&eng::field::kCpuRaster),
				   eng::Ref<eng::graphics::FramePlan>()};
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
	const eng::field::C2pRequest req {
		eng::ChunkyView {chunky, sizeof(chunky)},
		eng::PlaneBytes {planes, sizeof(planes)},
		16u,
		1u,
		kPlaneBytes,
		4u,
	};
	check(dt.c2p(req), "c2p por el seam");
	check(bits_in_plane(planes, 3) == 16u, "c2p: 16 bits en el plano 3");

	if (failures == 0) {
		std::printf("OK: DrawTarget (fill/line/frame/c2p + box) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
