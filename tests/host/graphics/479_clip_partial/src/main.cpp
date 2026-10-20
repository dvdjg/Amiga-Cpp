// ============================================================================
// Test HOST-479: recorte 2D parcial de poligonos (eng/retro/lib2d.hpp)
// ============================================================================
//
// REGRESION del bug de `clip_polygon_pass`: recorria n-1 aristas (faltaba la de
// cierre n-1 -> 0) y lo parcheaba con un vertice duplicado, de modo que un
// poligono con el primer vertice FUERA perdia el cruce de cierre y devolvia
// poligonos incompletos o < 3 vertices teniendo parte visible dentro.
//
// REGRESION de la aritmetica de cruce (DT-041): un tramo de arista > s16 (una
// cara proyectada con un vertice detras de la camara dista mas de 32767) envolvia
// en 16 bits y el cruce salia basura lejos de la interseccion real (la 080 se
// quedaba clavada). `clip_interp` usa camino ancho para ese caso.
//
// Comprueba, por caso:
//   1) El recorte devuelve >= 3 vertices si el poligono tiene area visible.
//   2) El relleno CPU del recortado coincide con el del original en el INTERIOR
//      de la caja (el borde puede diferir +-1 px por la convencion del fill en
//      el corte: el original no tiene arista alli y el recortado si).
//   3) Un poligono enteramente fuera -> 0 vertices.
//   4) El numero de vertices de salida nunca crece mas alla de n+1 por pasada
//      (cota de los buffers del llamador).
//   5) Hull: todo vertice de salida cae dentro del rango de la entrada (+-1 px);
//      un tramo > s16 mal calculado lo viola con vertices basura.
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
	s16 xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
	for (u8 i = 1u; i < n; ++i) {
		if (xs[i] < xmin) xmin = xs[i];
		if (xs[i] > xmax) xmax = xs[i];
		if (ys[i] < ymin) ymin = ys[i];
		if (ys[i] > ymax) ymax = ys[i];
	}
	for (u32 i = 0u; i < m; ++i) {
		cx[i] = in[i].x().v;
		cy[i] = in[i].y().v;
		// Hull: el recorte solo elimina y sustituye por puntos de cruce SOBRE las
		// aristas, asi que ningun vertice puede salirse del rango de la entrada.
		check(cx[i] >= xmin - 1 && cx[i] <= xmax + 1 && cy[i] >= ymin - 1 && cy[i] <= ymax + 1,
		      "vertice de salida fuera del hull de la entrada");
	}
	std::vector<u8> got(kPlaneBytes, 0u);
	fill(got, cx, cy, static_cast<u8>(m));
	const u32 bad = fill_violations(ref, got);
	std::printf("  %-30s caras=%u violaciones=%u\n", name, m, bad);
	std::snprintf(msg, sizeof(msg),
		      "%s: got sin basura y ref sin perdidas mas alla de 1 px", name);
	check(bad == 0u, msg);
}

void test_camera_safe_rect_clip() {
	const s16 min_x = 1, max_x = 95, min_y = 0, max_y = 63;
	const s16 inside_x[4] = {10, 50, 50, 10};
	const s16 inside_y[4] = {10, 10, 50, 50};
	s16 out_x[20] {}, out_y[20] {};
	const u8 inside_n = eng::graphics::clip_face_to_rect(
		inside_x, inside_y, 4u, min_x, max_x, min_y, max_y, out_x, out_y);
	bool same = inside_n == 4u;
	for (u8 i = 0u; i < inside_n; ++i) {
		same = same && out_x[i] == inside_x[i] && out_y[i] == inside_y[i];
	}
	check(same, "cara interior: clip_face_to_rect preserva los vértices");

	const s16 crossing_x[4] = {-10, 50, 50, -10};
	const s16 crossing_y[4] = {10, 10, 70, 70};
	static constexpr s16 expected_x[4] = {1, 50, 50, 1};
	static constexpr s16 expected_y[4] = {10, 10, 63, 63};
	const u8 crossing_n = eng::graphics::clip_face_to_rect(
		crossing_x, crossing_y, 4u, min_x, max_x, min_y, max_y, out_x, out_y);
	same = crossing_n == 4u;
	for (u8 i = 0u; i < crossing_n; ++i) {
		same = same && out_x[i] == expected_x[i] && out_y[i] == expected_y[i];
	}
	check(same, "cara que cruza izquierda/abajo: recorte CPU exacto");

	// Dos caras adyacentes recorren el borde compartido en sentidos opuestos. La
	// intersección con x=1 debe ser idéntica para que el contorno EOR no abra una costura.
	const s16 edge_a_x[3] = {0, 3, 3};
	const s16 edge_a_y[3] = {0, 2, 5};
	const s16 edge_b_x[3] = {3, 0, 3};
	const s16 edge_b_y[3] = {2, 0, -5};
	const s16 edge_min_y = -100, edge_max_y = 100;
	const u8 edge_a_n = eng::graphics::clip_face_to_rect(
		edge_a_x, edge_a_y, 3u, min_x, max_x, edge_min_y, edge_max_y, out_x, out_y);
	s16 edge_b_out_x[20] {}, edge_b_out_y[20] {};
	const u8 edge_b_n = eng::graphics::clip_face_to_rect(
		edge_b_x, edge_b_y, 3u, min_x, max_x, edge_min_y, edge_max_y, edge_b_out_x,
		edge_b_out_y);
	bool shared_cut_a = false, shared_cut_b = false;
	for (u8 i = 0u; i < edge_a_n; ++i) {
		shared_cut_a = shared_cut_a || (out_x[i] == 1 && out_y[i] == 1);
	}
	for (u8 i = 0u; i < edge_b_n; ++i) {
		shared_cut_b = shared_cut_b || (edge_b_out_x[i] == 1 && edge_b_out_y[i] == 1);
	}
	check(shared_cut_a && shared_cut_b,
	      "arista compartida: el corte izquierdo no depende del winding");

	// La unión de ambos triángulos es el triángulo P-S-R. Su raster CPU debe coincidir
	// bit a bit con rasterizar las dos caras recortadas: una discrepancia sería una fila
	// o columna negra interna, no una diferencia del límite exterior.
	const s16 p_x[3] = {0, 3, 3};
	const s16 p_y[3] = {10, 12, 15};
	const s16 q_x[3] = {3, 0, 3};
	const s16 q_y[3] = {12, 10, 5};
	s16 p_out_x[20] {}, p_out_y[20] {}, q_out_x[20] {}, q_out_y[20] {};
	const u8 p_n = eng::graphics::clip_face_to_rect(
		p_x, p_y, 3u, min_x, max_x, edge_min_y, edge_max_y, p_out_x, p_out_y);
	const u8 q_n = eng::graphics::clip_face_to_rect(
		q_x, q_y, 3u, min_x, max_x, edge_min_y, edge_max_y, q_out_x, q_out_y);
	const eng::graphics::PlanePolygon adjacent[2] = {
		{p_out_x, p_out_y, p_n, 1u}, {q_out_x, q_out_y, q_n, 1u}};
	const s16 union_x[4] = {1, 3, 3, 1};
	const s16 union_y[4] = {8, 5, 15, 12};
	const eng::graphics::PlanePolygon union_face {union_x, union_y, 4u, 1u};
	std::vector<u8> got(kPlaneBytes, 0u), want(kPlaneBytes, 0u);
	(void)eng::graphics::fill_polygons_by_plane_cpu(
		eng::Span<const eng::graphics::PlanePolygon>(adjacent, 2u),
		eng::PlaneBytes {got.data(), static_cast<u32>(got.size())}, kRowBytes, kPlaneBytes,
		1u, kW, kH);
	(void)eng::graphics::fill_polygons_by_plane_cpu(
		eng::Span<const eng::graphics::PlanePolygon>(&union_face, 1u),
		eng::PlaneBytes {want.data(), static_cast<u32>(want.size())}, kRowBytes, kPlaneBytes,
		1u, kW, kH);
	check(got == want, "caras adyacentes recortadas: raster CPU sin costura interna");
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
	// Arista de CIERRE con tramo > s16 (dy = 60000 entre v3 y v0): el cruce real
	// con el borde superior queda en x=40.5; con el tramo envuelto en 16 bits el
	// vertice de salida se iba a x<0 (lo caza la comprobacion de hull).
	{
		const s16 xs[4] = {51, 70, 60, 30};
		const s16 ys[4] = {30000, 25, 35, -30000};
		case_partial("cierre con span>s16", xs, ys, 4u, true);
	}
	// Arista NO de cierre con tramo > s16 (dy = 60000 entre v1 y v2); cruce con el
	// borde superior en x=31.5 (no entero, para que el ±1 px del fill no ambigue).
	{
		const s16 xs[4] = {30, 10, 53, 40};
		const s16 ys[4] = {20, -30000, 30000, 30};
		case_partial("span>s16 no de cierre", xs, ys, 4u, true);
	}
	test_camera_safe_rect_clip();
	if (g_fail == 0) {
		std::printf("OK: recorte 2D parcial (interior identico, sin perder vertices visibles)\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
