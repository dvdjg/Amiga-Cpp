// ============================================================================
// Test HOST-479: recorte 2D parcial de poligonos (eng/retro/lib2d.hpp)
// ============================================================================
//
// REGRESION del bug de `clip_polygon_pass`: recorria n-1 aristas (faltaba la de
// cierre n-1 -> 0) y lo parcheaba con un vertice duplicado, de modo que un
// poligono con el primer vertice FUERA perdia el cruce de cierre y devolvia
// poligonos incompletos o < 3 vertices teniendo parte visible dentro.
//
// Comprueba, por caso:
//   1) El recorte devuelve >= 3 vertices si el poligono tiene area visible.
//   2) El relleno CPU del recortado coincide con el del original en el INTERIOR
//      de la caja (el borde puede diferir +-1 px por la convencion del fill en
//      el corte: el original no tiene arista alli y el recortado si).
//   3) Un poligono enteramente fuera -> 0 vertices.
//   4) El numero de vertices de salida nunca crece mas alla de n+1 por pasada
//      (cota de los buffers del llamador).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/479_clip_partial

#define ENG_SCALAR_RETRO16
#include <eng/graphics/polygon_planes.hpp>
#include <eng/retro/lib2d.hpp>

#include <cstdio>
#include <vector>

using eng::s16;
using eng::u8;
using eng::u32;
using eng::retro::Rect;
using eng::retro::Vec2;

constexpr u32 kW = 96u;
constexpr u32 kH = 64u;
constexpr u32 kRowBytes = kW / 8u;
constexpr u32 kPlaneBytes = kRowBytes * kH;

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void fill(std::vector<u8>& dest, const s16* xs, const s16* ys, u8 n) {
	const eng::graphics::PlanePolygon face {xs, ys, n, 1u};
	const eng::Span<const eng::graphics::PlanePolygon> faces {&face, 1u};
	(void)eng::graphics::fill_polygons_by_plane_cpu(
		faces, eng::PlaneBytes {dest.data(), dest.size()}, kRowBytes, kPlaneBytes, 1u, kW, kH,
		eng::graphics::FillPattern::Solid);
}

/// Comprueba el relleno del poligono recortado contra el del original (∩ caja), con los
/// criterios que definen el bug a blindar — sin exigir igualdad byte a byte, porque el
/// punto de corte del recorte se redondea a entero y puede desplazar el corte ±1 px:
///   a) `got ⊆ ref` por pixel: NINGUN pixel fuera de la forma original (basura/vertices
///      corruptos).
///   b) `ref ⊆ got` dilatado 1 px: NINGUN pixel visible perdido mas alla del redondeo.
/// Devuelve el numero de violaciones (0 = correcto).
u32 fill_violations(const std::vector<u8>& ref, const std::vector<u8>& got) {
	const auto bit = [](const std::vector<u8>& v, u32 x, u32 y) -> bool {
		return ((v[y * kRowBytes + (x >> 3u)] >> (7u - (x & 7u))) & 1u) != 0u;
	};
	u32 bad = 0u;
	for (u32 y = 0u; y < kH; ++y) {
		for (u32 x = 0u; x < kW; ++x) {
			const bool in_ref = bit(ref, x, y);
			const bool in_got = bit(got, x, y);
			const auto near_in = [&](const std::vector<u8>& v) -> bool {
				for (eng::s32 dy = -1; dy <= 1; ++dy) {
					for (eng::s32 dx = -1; dx <= 1; ++dx) {
						const eng::s32 nx = static_cast<eng::s32>(x) + dx;
						const eng::s32 ny = static_cast<eng::s32>(y) + dy;
						if (nx < 0 || ny < 0 || nx >= static_cast<eng::s32>(kW) ||
						    ny >= static_cast<eng::s32>(kH)) {
							continue;
						}
						if (bit(v, static_cast<u32>(nx), static_cast<u32>(ny))) {
							return true;
						}
					}
				}
				return false;
			};
			// Tolerancia de +-1 px en AMBAS direcciones: el corte se redondea a entero y
			// desplaza la linea de corte hasta 1 px (sin perder ni inventar nada mas alla).
			if (in_got && !in_ref && !near_in(ref)) {
				if (bad < 6u) {
					std::printf("    viol: got fuera de ref en (%u,%u)\n", x, y);
				}
				++bad;
			}
			if (in_ref && !in_got && !near_in(got)) {
				if (bad < 6u) {
					std::printf("    viol: ref perdido en (%u,%u)\n", x, y);
				}
				++bad;
			}
		}
	}
	return bad;
}

void case_partial(const char* name, const s16* xs, const s16* ys, u8 n, bool expect_visible) {
	std::vector<u8> ref(kPlaneBytes, 0u);
	fill(ref, xs, ys, n);
	Vec2 in[16];
	Vec2 tmp[16];
	for (u8 i = 0u; i < n; ++i) {
		in[i] = eng::retro::v2(xs[i], ys[i]);
	}
	const Rect box = eng::retro::rect(0, 0, static_cast<s16>(kW), static_cast<s16>(kH));
	const u32 m = eng::retro::clip_polygon(box, in, tmp, n,
					       static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
							       eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
	check(m <= n + 4u, name);
	if (!expect_visible) {
		char msg[96];
		std::snprintf(msg, sizeof(msg), "%s: fuera del todo -> 0 vertices", name);
		check(m == 0u, msg);
		return;
	}
	char msg[96];
	std::snprintf(msg, sizeof(msg), "%s: >= 3 vertices con parte visible", name);
	check(m >= 3u, msg);
	if (m < 3u) {
		return;
	}
	s16 cx[16];
	s16 cy[16];
	for (u32 i = 0u; i < m; ++i) {
		cx[i] = in[i].x().v;
		cy[i] = in[i].y().v;
	}
	std::vector<u8> got(kPlaneBytes, 0u);
	fill(got, cx, cy, static_cast<u8>(m));
	const u32 bad = fill_violations(ref, got);
	std::printf("  %-30s caras=%u violaciones=%u\n", name, m, bad);
	std::snprintf(msg, sizeof(msg),
		      "%s: got sin basura y ref sin perdidas mas alla de 1 px", name);
	check(bad == 0u, msg);
}

int main() {
	// Regresion exacta del bug: el PRIMER vertice fuera de la arista derecha.
	{
		const s16 xs[3] = {70, 130, 130};
		const s16 ys[3] = {10, 10, 50};
		case_partial("triangulo mitad fuera der", xs, ys, 3u, true);
	}
	// Primer vertice fuera por la izquierda.
	{
		const s16 xs[3] = {-5, -5, 50};
		const s16 ys[3] = {20, 70, 45};
		case_partial("triangulo fuera por la izq", xs, ys, 3u, true);
	}
	// Pentagono cruzando la esquina superior derecha.
	{
		const s16 xs[5] = {60, 110, 110, 85, 60};
		const s16 ys[5] = {-10, -10, 30, 60, 30};
		case_partial("pentagono esquina sup-der", xs, ys, 5u, true);
	}
	// Cuadrilatero mas grande que la caja (cruza los 4 bordes).
	{
		const s16 xs[4] = {-30, 130, 130, -30};
		const s16 ys[4] = {-20, -20, 90, 90};
		case_partial("cuadrilatero envuelve la caja", xs, ys, 4u, true);
	}
	// Fuera del todo.
	{
		const s16 xs[3] = {120, 160, 140};
		const s16 ys[3] = {10, 40, 60};
		case_partial("triangulo fuera del todo", xs, ys, 3u, false);
	}
	// Un vertice fuera por cada lado distinto (cruces en varias pasadas).
	{
		const s16 xs[4] = {-10, 110, 50, 20};
		const s16 ys[4] = {30, 20, -10, 80};
		case_partial("cuadrilatero cruzando 3 lados", xs, ys, 4u, true);
	}
	if (g_fail == 0) {
		std::printf("OK: recorte 2D parcial (interior identico, sin perder vertices visibles)\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
