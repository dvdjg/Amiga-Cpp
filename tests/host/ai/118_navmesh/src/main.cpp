// ============================================================================
// Test HOST-118: navmesh lite (poligonos convexos + A* por portales), generico
// ============================================================================
//
// Valida `engine/include/eng/ai/navigation/navmesh_lite.hpp`:
//
//   1) localizacion punto-en-poligono y punto fuera de la malla.
//   2) camino a traves de portales (dos y tres poligonos): puntos medios + destino.
//   3) camino dentro de un solo poligono y origen/destino fuera.
//   4) string-pulling (funnel) del pasillo recto.
//   5) `locate_from` con pista (cache por agente).
//   6) `MovementProfile` filtrando portales por terreno.
//
// La malla se instancia con el escalar `s32`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/118_navmesh

#include <cstdio>

#include <eng/ai/navigation/navmesh_lite.hpp>

namespace {

using eng::u16;
using eng::u8;
using eng::s16;
using eng::s32;
using eng::usize;
using P = eng::ai::NavPoint<s32>;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

[[nodiscard]] bool eq(P a, P b) { return a.x == b.x && a.y == b.y; }

// Dos rectangulos que comparten la arista x=10.
constexpr P kRectA[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
constexpr P kRectB[4] = {{10, 0}, {20, 0}, {20, 10}, {10, 10}};
constexpr P kRectC[4] = {{20, 0}, {30, 0}, {30, 10}, {20, 10}};

u16 g_g[8] {};
s16 g_came[8] {};
u8 g_closed[8] {};
P g_path[8] {};

using Mesh = eng::ai::NavMesh<s32, 8, 4, 8>;

void test_locate() {
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	check(a == 0u && b == 1u, "navmesh: dos poligonos");
	check(mesh.poly_count() == 2u, "navmesh: cuenta de poligonos");
	check(mesh.locate({5, 5}) == a, "navmesh: localiza el primero");
	check(mesh.locate({15, 5}) == b, "navmesh: localiza el segundo");
	check(mesh.locate({-1, 5}) == Mesh::no_poly, "navmesh: punto fuera -> no_poly");
}

void test_locate_from() {
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	(void)mesh.add_portal(a, b, {10, 0}, {10, 10});
	// Con la pista en el poligono A, un punto en B se localiza por el vecino.
	check(mesh.locate_from({15, 5}, a) == b, "navmesh: locate_from usa la pista y el vecino");
	check(mesh.locate_from({5, 5}, a) == a, "navmesh: locate_from acierta con la pista");
	check(mesh.locate_from({15, 5}, Mesh::no_poly) == b, "navmesh: locate_from sin pista");
}

void test_two_polys() {
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	check(mesh.add_portal(a, b, {10, 0}, {10, 10}), "navmesh: portal A-B");
	check(mesh.portal_count() == 1u, "navmesh: un portal");

	const usize n = mesh.find_path({5, 5}, {15, 5}, g_g, g_came, g_closed, g_path);
	check(n == 2u, "navmesh: camino de 2 puntos (portal + destino)");
	check(eq(g_path[0], {10, 5}), "navmesh: primer punto = medio del portal");
	check(eq(g_path[1], {15, 5}), "navmesh: ultimo punto = destino");

	const usize n2 = mesh.find_path({2, 2}, {8, 8}, g_g, g_came, g_closed, g_path);
	check(n2 == 1u && eq(g_path[0], {8, 8}), "navmesh: mismo poligono -> destino");

	check(mesh.find_path({-5, 5}, {15, 5}, g_g, g_came, g_closed, g_path) == 0u,
	      "navmesh: origen fuera -> sin camino");
}

void test_three_polys() {
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	const u16 c = mesh.add_polygon(kRectC);
	check(mesh.add_portal(a, b, {10, 0}, {10, 10}), "navmesh: portal A-B");
	check(mesh.add_portal(b, c, {20, 0}, {20, 10}), "navmesh: portal B-C");

	const usize n = mesh.find_path({5, 5}, {25, 5}, g_g, g_came, g_closed, g_path);
	check(n == 3u, "navmesh: camino de 3 puntos entre tres poligonos");
	check(eq(g_path[0], {10, 5}) && eq(g_path[1], {20, 5}) && eq(g_path[2], {25, 5}),
	      "navmesh: portales y destino encadenados");
}

void test_smooth() {
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	const u16 c = mesh.add_polygon(kRectC);
	check(mesh.add_portal(a, b, {10, 0}, {10, 10}), "navmesh: portal A-B");
	check(mesh.add_portal(b, c, {20, 0}, {20, 10}), "navmesh: portal B-C");

	const usize n = mesh.find_smooth_path({5, 5}, {25, 5}, g_g, g_came, g_closed, g_path);
	check(n == 2u, "navmesh: string-pulling colapsa el pasillo recto");
	check(eq(g_path[0], {5, 5}) && eq(g_path[1], {25, 5}),
	      "navmesh: camino suavizado = inicio y destino");

	const usize m = mesh.find_path({5, 5}, {25, 5}, g_g, g_came, g_closed, g_path);
	check(m == 3u, "navmesh: find_path mantiene los puntos medios");
}

void test_profile() {
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	// Portal con el bit de terreno 1 (p. ej. agua/colina).
	check(mesh.add_portal(a, b, {10, 0}, {10, 10}, /*cost*/0u, /*terrain*/1u),
	      "navmesh: portal A-B con terreno");
	check(mesh.add_portal(a, b, {10, 0}, {10, 10}, /*cost*/0u, /*terrain*/2u),
	      "navmesh: segundo portal A-B con otro terreno");

	// Perfil por defecto: todo permitido.
	check(mesh.find_path({5, 5}, {15, 5}, g_g, g_came, g_closed, g_path) == 2u,
	      "navmesh: perfil por defecto cruza");
	// Perfil que solo permite el terreno 2: sigue habiendo camino (el segundo portal).
	eng::ai::MovementProfile only2 {};
	only2.allowed = 0x2u;
	check(mesh.find_path({5, 5}, {15, 5}, g_g, g_came, g_closed, g_path, only2) == 2u,
	      "navmesh: perfil permite el terreno 2");
	// Perfil que no permite ni 1 ni 2: sin camino.
	eng::ai::MovementProfile none {};
	none.allowed = 0x0u;
	check(mesh.find_path({5, 5}, {15, 5}, g_g, g_came, g_closed, g_path, none) == 0u,
	      "navmesh: perfil sin terreno -> sin camino");
}

} // namespace

int main() {
	std::printf("NavMeshLite:\n");
	test_locate();
	test_locate_from();
	test_two_polys();
	test_three_polys();
	test_smooth();
	test_profile();

	if (g_fail == 0u) {
		std::printf("OK: NavMeshLite (localizar, portales, string-pulling, perfil)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
