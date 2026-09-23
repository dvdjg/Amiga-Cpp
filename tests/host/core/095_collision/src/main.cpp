// ============================================================================
// Test HOST-095: colisión 2D entera (eng::util::collision).
// ============================================================================
//
// Respalda `eng/core/util/collision.hpp`: AABB, orientación, segmentos, triángulo y
// círculos (con `muls.w`, sin división).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/095_collision

#include <cstdio>

#include <eng/core/util/collision.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr eng::Point2s p(eng::s16 x, eng::s16 y) {
	return eng::Point2s {x, y};
}

} // namespace

int main() {
	std::printf("== HOST-095 collision ==\n");

	// --- AABB ----------------------------------------------------------------
	{
		const eu::Aabb a {0, 0, 10, 10};
		check(eu::aabb_overlap(a, eu::Aabb {5, 5, 15, 15}), "AABB solapan");
		check(!eu::aabb_overlap(a, eu::Aabb {10, 0, 20, 10}), "AABB tocando borde: no");
		check(!eu::aabb_overlap(a, eu::Aabb {20, 20, 30, 30}), "AABB separadas: no");
		check(eu::aabb_contains(a, eu::Aabb {2, 2, 8, 8}), "AABB contiene");
		check(!eu::aabb_contains(a, eu::Aabb {2, 2, 12, 8}), "AABB no contiene");
		check(eu::point_in_aabb(a, p(0, 0)) && eu::point_in_aabb(a, p(9, 9)), "punto dentro");
		check(!eu::point_in_aabb(a, p(10, 5)) && !eu::point_in_aabb(a, p(5, -1)),
		      "punto fuera");
	}

	// --- Orientación ---------------------------------------------------------
	{
		check(eu::orient(p(0, 0), p(1, 0), p(0, 1)) > 0, "orientación izquierda");
		check(eu::orient(p(0, 0), p(1, 0), p(0, -1)) < 0, "orientación derecha");
		check(eu::orient(p(0, 0), p(1, 1), p(2, 2)) == 0, "colineal");
	}

	// --- Segmentos -----------------------------------------------------------
	{
		check(eu::segments_intersect(p(0, 0), p(10, 10), p(0, 10), p(10, 0)),
		      "cruz en aspa");
		check(!eu::segments_intersect(p(0, 0), p(10, 0), p(0, 5), p(10, 5)),
		      "paralelos");
		check(eu::segments_intersect(p(0, 0), p(10, 0), p(5, 0), p(15, 0)),
		      "colineales que se solapan");
		check(!eu::segments_intersect(p(0, 0), p(4, 0), p(10, 0), p(14, 0)),
		      "colineales disjuntos");
	}

	// --- Triángulo -----------------------------------------------------------
	{
		check(eu::point_in_triangle(p(1, 1), p(0, 0), p(8, 0), p(0, 8)), "punto en triángulo");
		check(!eu::point_in_triangle(p(6, 6), p(0, 0), p(8, 0), p(0, 8)),
		      "punto fuera (hipotenusa)");
		check(eu::point_in_triangle(p(0, 0), p(0, 0), p(8, 0), p(0, 8)), "vértice dentro");
	}

	// --- Círculos ------------------------------------------------------------
	{
		check(eu::circle_overlap(p(0, 0), 5, p(8, 0), 5), "círculos solapan");
		check(eu::circle_overlap(p(0, 0), 5, p(10, 0), 5), "círculos tangentes");
		check(!eu::circle_overlap(p(0, 0), 5, p(11, 0), 5), "círculos separados");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: colisión validada.\n");
	return 0;
}
