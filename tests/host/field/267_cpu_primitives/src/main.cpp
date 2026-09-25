// ============================================================================
// Test HOST-267: primitivas CPU optimizadas (rect, linea run-slice, poligono).
// ============================================================================
//
// Valida `eng/field/cpu_primitives.hpp` sobre un `ContiguousPlayfield`:
//   - `cpu_fill_rect`: relleno por spans (todas las filas del rect).
//   - `cpu_line`: run-slice (spans por fila); se compara con un **Bresenham de referencia**
//     independiente para horizontales, verticales y diagonales.
//   - `cpu_fill_polygon`: triangulo relleno (even-odd) sin agujeros.
//
// Es la referencia CPU portable (Amiga y futuro Atari ST) frente a la ruta Blitter. Ver
// PROTOCOLO_ETAPAS_GRAFICOS.md (etapa 2/4) y RASTER.md.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/field/267_cpu_primitives

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/cpu_primitives.hpp>

// El ancho del tipo de coordenada de pixel es el contrato de rendimiento del 68000:
// 16 bits alli (aritmetica word) y 32 en host. Un cambio accidental aqui pasaria
// desapercibido en los valores pero romperia el objetivo en el target.
#if defined(__m68k__)
static_assert(sizeof(eng::pix) == 2u, "eng::pix debe ser s16 en 68000");
#else
static_assert(sizeof(eng::pix) == 4u, "eng::pix debe ser s32 en host");
#endif

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kW = 64u;
constexpr eng::u16 kH = 40u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kW / 8u) + 3u) & ~3u); // 8
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kH;
constexpr eng::u8 kPlanes = 4u;

eng::u8 pixel_at(const eng::u8* base, eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			base + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

/// Bresenham de referencia (independiente) para comparar la linea.
void ref_line(bool* out, eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1) {
	const eng::s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
	const eng::s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
	const eng::s32 sx = x0 < x1 ? 1 : -1;
	const eng::s32 sy = y0 < y1 ? 1 : -1;
	eng::s32 err = dx - dy;
	for (;;) {
		if (x0 >= 0 && x0 < kW && y0 >= 0 && y0 < kH) out[y0 * kW + x0] = true;
		if (x0 == x1 && y0 == y1) break;
		const eng::s32 e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
}

/// Una linea run-slice debe **cubrir** los pixeles de Bresenham (superset: rellena huecos de
/// 1 px entre pasos, lo que en lineas gruesas/anchos >1 es correcto). Se exige cobertura total.
bool line_covers(eng::u8* mem, eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1) {
	for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) mem[i] = 0u;
	eng::field::ContiguousPlayfield pf {};
	pf.bind_raw(mem, kPlaneStride * kPlanes, kW, kH, kPlanes);
	eng::field::cpu_line(pf, x0, y0, x1, y1, 3u);
	bool ref[kW * kH] = {};
	ref_line(ref, x0, y0, x1, y1);
	for (eng::s16 y = 0; y < static_cast<eng::s16>(kH); ++y) {
		for (eng::s16 x = 0; x < static_cast<eng::s16>(kW); ++x) {
			if (ref[y * kW + x] && pixel_at(mem, x, y) != 3u) return false;
		}
	}
	return true;
}

} // namespace

int main() {
	alignas(2) eng::u8 mem[kPlaneStride * kPlanes] {};
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(mem, sizeof(mem), kW, kH, kPlanes), "bind playfield");
	// Ya blindado con static_assert arriba; aqui solo se deja constancia en el log.
	std::printf("  eng::pix = %u bytes\n", static_cast<unsigned>(sizeof(eng::pix)));

	// --- Rectangulo por spans ---
	const eng::u32 rows = eng::field::cpu_fill_rect(pf, 4, 6, 20u, 10u, 5u);
	check(rows == 10u, "cpu_fill_rect pinta 10 filas");
	check(pixel_at(mem, 4, 6) == 5u && pixel_at(mem, 23, 15) == 5u, "rect esquinas dentro");
	check(pixel_at(mem, 3, 6) == 0u && pixel_at(mem, 24, 15) == 0u, "rect fuera intacto");

	// --- Linea horizontal ---
	for (eng::u32 i = 0u; i < sizeof(mem); ++i) mem[i] = 0u;
	eng::field::cpu_line(pf, 2, 30, 40, 30, 7u);
	check(pixel_at(mem, 2, 30) == 7u && pixel_at(mem, 21, 30) == 7u && pixel_at(mem, 40, 30) == 7u,
	      "linea horizontal completa");
	check(pixel_at(mem, 1, 30) == 0u && pixel_at(mem, 41, 30) == 0u, "horizontal no desborda");

	// --- Linea vertical ---
	for (eng::u32 i = 0u; i < sizeof(mem); ++i) mem[i] = 0u;
	eng::field::cpu_line(pf, 10, 2, 10, 35, 7u);
	bool v_ok = true;
	for (eng::s16 y = 2; y <= 35; ++y) {
		if (pixel_at(mem, 10, y) != 7u) v_ok = false;
	}
	check(v_ok, "linea vertical continua");

	// --- Lineas diagonales: run-slice cubre el Bresenham de referencia ---
	for (eng::s16 dx = -20; dx <= 20; dx += 7) {
		for (eng::s16 dy = -14; dy <= 14; dy += 7) {
			const eng::s32 x1 = 32 + dx, y1 = 20 + dy;
			check(line_covers(mem, 32, 20, x1, y1), "linea diagonal cubre Bresenham");
		}
	}

	// --- Triangulo relleno (even-odd) sin agujeros ---
	for (eng::u32 i = 0u; i < sizeof(mem); ++i) mem[i] = 0u;
	const eng::s16 xs[3] = {6, 40, 20};
	const eng::s16 ys[3] = {34, 34, 8};
	const eng::u32 prows = eng::field::cpu_fill_polygon(
		pf, eng::Span<const eng::s16>(xs, 3u), eng::Span<const eng::s16>(ys, 3u), 6u);
	check(prows > 20u, "poligono rellena varias filas");
	// Un punto interior conocido debe estar relleno (centroide ~ (22,25)).
	check(pixel_at(mem, 22, 25) == 6u, "poligono: interior relleno");
	// Un punto claramente fuera (esquina) no.
	check(pixel_at(mem, 2, 2) == 0u, "poligono: fuera vacio");
	// Los vertices estan cubiertos (al menos uno).
	check(pixel_at(mem, 20, 8) == 6u || pixel_at(mem, 20, 9) == 6u, "poligono: vertice superior");

	if (failures == 0) {
		std::printf("OK: primitivas CPU (rect/linea run-slice/poligono) validadas.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
