// ============================================================================
// Test HOST-217: relleno de poligonos compuesto por bitplane (CPU).
// ============================================================================
//
// Valida `eng/graphics/polygon_planes.hpp`: en vez de rellenar poligono a poligono
// (outline + fill + aplicar a los planos de su color), se rellena **por plano**:
// para cada plano `p`, la union de las caras cuyo color tiene el bit `p` a 1. Las
// aristas compartidas por caras con el mismo bit se cancelan (doble cruce even-odd).
//
//   1) Dos triangulos que forman un cuadrado, MISMO color (1) -> plano 0 = cuadrado.
//   2) Colores distintos (1 y 2) -> plano 0 = triangulo A, plano 1 = triangulo B.
//   3) color 0 no rellena.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/217_polygon_planes

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/polygon_planes.hpp>

namespace {

using eng::graphics::PlanePolygon;
using eng::graphics::fill_polygons_by_plane_cpu;

constexpr eng::u16 kW = 32, kH = 16;
constexpr eng::u16 kRow = kW / 8u;                 // 4 bytes/fila
constexpr eng::u32 kPlane = static_cast<eng::u32>(kRow) * kH; // 64 B/plano
constexpr eng::u8 kPlanes = 2;

alignas(16) eng::u8 g_dest[kPlane * kPlanes];

// Cuadrado [0,16]x[0,16] partido por la diagonal (0,16)-(16,0) en dos triangulos.
constexpr eng::s16 kAx[3] = {0, 16, 0};
constexpr eng::s16 kAy[3] = {0, 0, 16};
constexpr eng::s16 kBx[3] = {16, 16, 0};
constexpr eng::s16 kBy[3] = {0, 16, 16};

bool bit(const eng::u8* plane, eng::u16 x, eng::u16 y) {
	const eng::u8 v = plane[static_cast<eng::u32>(y) * kRow + (x >> 3u)];
	return ((v >> (7u - (x & 7u))) & 1u) != 0u;
}

void clear() {
	for (eng::u32 i = 0; i < kPlane * kPlanes; ++i) g_dest[i] = 0u;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	// 1) Mismo color (1): el plano 0 rellena la union (el cuadrado).
	{
		clear();
		const PlanePolygon faces[2] = {
			{ kAx, kAy, 3u, 1u },
			{ kBx, kBy, 3u, 1u },
		};
		(void)fill_polygons_by_plane_cpu(eng::Span<const PlanePolygon> {faces, 2},
						 eng::PlaneBytes {g_dest, kPlane * kPlanes},
						 kRow, kPlane, kPlanes, kW, kH);
		const eng::u8* p0 = g_dest;
		const eng::u8* p1 = g_dest + kPlane;
		check(bit(p0, 2, 2) && bit(p0, 13, 5), "mismo color: el cuadrado queda lleno");
		check(!bit(p1, 2, 2) && !bit(p1, 13, 5), "mismo color: el plano 1 queda vacio");
		check(bit(p0, 0, 0) && bit(p0, 15, 15), "esquinas del cuadrado llenas");
	}

	// 2) Colores distintos (1 y 2): plano 0 = triangulo A, plano 1 = triangulo B.
	{
		clear();
		const PlanePolygon faces[2] = {
			{ kAx, kAy, 3u, 1u },
			{ kBx, kBy, 3u, 2u },
		};
		(void)fill_polygons_by_plane_cpu(eng::Span<const PlanePolygon> {faces, 2},
						 eng::PlaneBytes {g_dest, kPlane * kPlanes},
						 kRow, kPlane, kPlanes, kW, kH);
		const eng::u8* p0 = g_dest;
		const eng::u8* p1 = g_dest + kPlane;
		check(bit(p0, 2, 2) && !bit(p1, 2, 2), "color 1: solo el plano 0 (triangulo A)");
		check(bit(p1, 13, 5) && !bit(p0, 13, 5), "color 2: solo el plano 1 (triangulo B)");
	}

	// 3) color 0 no rellena.
	{
		clear();
		const PlanePolygon faces[1] = {
			{ kAx, kAy, 3u, 0u },
		};
		const eng::u32 spans = fill_polygons_by_plane_cpu(
			eng::Span<const PlanePolygon> {faces, 1},
			eng::PlaneBytes {g_dest, kPlane * kPlanes}, kRow, kPlane, kPlanes, kW, kH);
		check(spans == 0u, "color 0 no escribe ningun span");
	}

	if (failures == 0) {
		std::printf("OK: polygon_planes (relleno compuesto por bitplane, CPU).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
