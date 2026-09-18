// ============================================================================
// Test HOST-118: navmesh lite (poligonos convexos + A* por portales)
// ============================================================================
//
// Valida `engine/include/eng/ai/navigation/navmesh_lite.hpp`:
//
//   1) localizacion punto-en-poligono y punto fuera de la malla.
//   2) camino a traves de portales (dos y tres poligonos): puntos medios + destino.
//   3) camino dentro de un solo poligono y origen/destino fuera.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/118_navmesh

#include <cstdio>

#include <eng/ai/navigation/navmesh_lite.hpp>

namespace {

using eng::u16;
using eng::u8;
using eng::s16;
using eng::usize;
using eng::Point2s;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

[[nodiscard]] bool eq(Point2s a, Point2s b) { return a.x == b.x && a.y == b.y; }

// Dos rectangulos que comparten la arista x=10.
constexpr Point2s kRectA[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
constexpr Point2s kRectB[4] = {{10, 0}, {20, 0}, {20, 10}, {10, 10}};
constexpr Point2s kRectC[4] = {{20, 0}, {30, 0}, {30, 10}, {20, 10}};

u16 g_g[8] {};
s16 g_came[8] {};
u8 g_closed[8] {};
Point2s g_path[8] {};

void test_locate() {
	using Mesh = eng::ai::NavMesh<8, 4, 8>;
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	check(a == 0u && b == 1u, "navmesh: dos poligonos");
	check(mesh.poly_count() == 2u, "navmesh: cuenta de poligonos");
	check(mesh.locate({5, 5}) == a, "navmesh: localiza el primero");
	check(mesh.locate({15, 5}) == b, "navmesh: localiza el segundo");
	check(mesh.locate({-1, 5}) == Mesh::no_poly, "navmesh: punto fuera -> no_poly");
}

void test_two_polys() {
	using Mesh = eng::ai::NavMesh<8, 4, 8>;
	Mesh mesh;
	const u16 a = mesh.add_polygon(kRectA);
	const u16 b = mesh.add_polygon(kRectB);
	check(mesh.add_portal(a, b, {10, 0}, {10, 10}), "navmesh: portal A-B");
	check(mesh.portal_count() == 1u, "navmesh: un portal");

	const usize n = mesh.find_path({5, 5}, {15, 5}, g_g, g_came, g_closed, g_path);
	check(n == 2u, "navmesh: camino de 2 puntos (portal + destino)");
	check(eq(g_path[0], {10, 5}), "navmesh: primer punto = medio del portal");
	check(eq(g_path[1], {15, 5}), "navmesh: ultimo punto = destino");

	// Dentro del mismo poligono: solo el destino.
	const usize n2 = mesh.find_path({2, 2}, {8, 8}, g_g, g_came, g_closed, g_path);
	check(n2 == 1u && eq(g_path[0], {8, 8}), "navmesh: mismo poligono -> destino");

	// Origen fuera de la malla.
	check(mesh.find_path({-5, 5}, {15, 5}, g_g, g_came, g_closed, g_path) == 0u,
	      "navmesh: origen fuera -> sin camino");
}

void test_three_polys() {
	using Mesh = eng::ai::NavMesh<8, 4, 8>;
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

} // namespace

int main() {
	std::printf("NavMeshLite:\n");
	test_locate();
	test_two_polys();
	test_three_polys();

	if (g_fail == 0u) {
		std::printf("OK: NavMeshLite (localizar, portales, uno/dos/tres poligonos)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
