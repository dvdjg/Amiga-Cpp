// ============================================================================
// Test HOST-125: SAT 2D para poligonos convexos (y punto en convexo)
// ============================================================================
//
// Valida las adiciones de `engine/include/eng/core/util/collision.hpp`:
//
//   1) `convex_overlap` con cuadrados alineados (solape, separados, tocar por borde).
//   2) Rombo (ejes no alineados) contra cuadrado: prueba los ejes de arista, no solo X/Y.
//   3) Independencia del sentido de giro (CCW/CW) y triangulo vs cuadrado.
//   4) `point_in_convex`: dentro, fuera, en borde y en vertice.
//   5) Poligono degenerado (<3 vertices) -> false.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/125_convex_sat

#include <cstdio>

#include <eng/core/util/collision.hpp>

namespace {

using eng::Point2s;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// Cuadrado [0,10]x[0,10] en CCW.
constexpr Point2s kA[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
// Mismo cuadrado pero en CW (el sentido no debe importar).
constexpr Point2s kAcw[4] = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
// Solapa con kA por la esquina.
constexpr Point2s kB[4] = {{5, 5}, {15, 5}, {15, 15}, {5, 15}};
// Separado a la derecha.
constexpr Point2s kC[4] = {{20, 0}, {30, 0}, {30, 10}, {20, 10}};
// Comparte exactamente la arista x=10 con kA (tocar = separado).
constexpr Point2s kTouching[4] = {{10, 0}, {20, 0}, {20, 10}, {10, 10}};
// Rombo centrado en (10,10): solapa kA.
constexpr Point2s kDiamond[4] = {{10, 5}, {15, 10}, {10, 15}, {5, 10}};
// Rombo separado (centro (25,25)).
constexpr Point2s kDiamondFar[4] = {{25, 20}, {30, 25}, {25, 30}, {20, 25}};
// Triangulo dentro del hueco de la esquina (separado de kA).
constexpr Point2s kTri[3] = {{20, 20}, {30, 20}, {20, 30}};

void test_sat() {
	check(eng::util::convex_overlap(kA, kB), "sat: cuadrados que solapan");
	check(!eng::util::convex_overlap(kA, kC), "sat: cuadrados separados");
	check(!eng::util::convex_overlap(kA, kTouching), "sat: tocar por borde = separado");
	check(eng::util::convex_overlap(kA, kAcw), "sat: el sentido de giro no importa");
	check(eng::util::convex_overlap(kB, kAcw), "sat: solape con el cuadrado invertido");

	check(eng::util::convex_overlap(kA, kDiamond), "sat: rombo (ejes no alineados) solapa");
	check(!eng::util::convex_overlap(kA, kDiamondFar), "sat: rombo lejano separado");

	check(eng::util::convex_overlap(kB, kTri) == false, "sat: triangulo lejano separado");
	check(!eng::util::convex_overlap(kA, kTri), "sat: triangulo fuera de kA");
}

void test_point_in_convex() {
	check(eng::util::point_in_convex({5, 5}, kA), "punto: dentro");
	check(!eng::util::point_in_convex({11, 5}, kA), "punto: fuera");
	check(eng::util::point_in_convex({10, 5}, kA), "punto: en el borde");
	check(eng::util::point_in_convex({0, 0}, kA), "punto: en un vertice");
	check(!eng::util::point_in_convex({20, 20}, kA), "punto: lejos");
}

void test_degenerate() {
	constexpr Point2s two[2] = {{0, 0}, {1, 1}};
	check(!eng::util::convex_overlap(kA, two), "sat: poligono < 3 vertices -> false");
	check(!eng::util::point_in_convex({0, 0}, two), "punto: poligono < 3 vertices -> false");
}

} // namespace

int main() {
	std::printf("ConvexSAT:\n");
	test_sat();
	test_point_in_convex();
	test_degenerate();

	if (g_fail == 0u) {
		std::printf("OK: SAT 2D (cuadrados, rombo, sentido de giro, bordes y punto)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
