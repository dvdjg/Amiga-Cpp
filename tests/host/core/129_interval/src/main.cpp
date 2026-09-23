// ============================================================================
// Test HOST-129: intervalos y conjunto ordenado (eng::util::IntervalSet)
// ============================================================================
//
// Valida `engine/include/eng/core/util/interval.hpp`: rangos semiabiertos [lo,hi) y un
// conjunto que fusiona solapes y adyacencias, con consulta por búsqueda binaria.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/129_interval

#include <cstdio>

#include <eng/core/util/interval.hpp>

namespace {

using eng::u16;
using Set = eng::util::IntervalSet<8>;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_merge_and_contains() {
	Set s;
	check(s.empty() && s.capacity() == 8u, "intervalo: arranca vacio");

	check(s.add(0, 10) && s.add(20, 30), "intervalo: dos separados");
	check(s.size() == 2u, "intervalo: dos en el conjunto");
	check(s.contains(0) && s.contains(5) && s.contains(20) && s.contains(25),
	      "intervalo: contains dentro");
	check(!s.contains(10) && !s.contains(15) && !s.contains(30),
	      "intervalo: bordes semiabiertos");
	check(!s.contains(-1) && !s.contains(100), "intervalo: fuera");

	// Adyacencia por ambos lados: [0,10) + [10,20) + [20,30) -> [0,30).
	check(s.add(10, 20), "intervalo: fusiona adyacentes");
	check(s.size() == 1u && s.at(0).lo == 0 && s.at(0).hi == 30,
	      "intervalo: un solo [0,30)");
	check(s.contains(15), "intervalo: el hueco ya esta cubierto");

	// Solape por la derecha.
	check(s.add(28, 40) && s.size() == 1u && s.at(0).hi == 40, "intervalo: extiende a 40");

	// Un intervalo contenido no cambia nada.
	check(s.add(5, 8) && s.size() == 1u && s.at(0).lo == 0 && s.at(0).hi == 40,
	      "intervalo: contenido no cambia");

	// Rango vacio: no hace nada.
	check(s.add(50, 50) && s.size() == 1u, "intervalo: rango vacio ignorado");

	// Dos separados y su fusion cubriendo el hueco.
	check(s.add(45, 50) && s.size() == 2u, "intervalo: tercero separado");
	check(s.add(40, 45) && s.size() == 1u && s.at(0).hi == 50,
	      "intervalo: fusiona los dos y el hueco");
}

void test_capacity() {
	eng::util::IntervalSet<2> s;
	check(s.add(0, 10) && s.add(20, 30), "intervalo: caben 2");
	check(!s.add(40, 50), "intervalo: el tercero no cabe");
	check(s.add(5, 6), "intervalo: uno contenido si cabe");
}

void test_overlaps() {
	check(eng::util::interval_overlaps({0, 10}, {5, 15}), "intervalo: solapan");
	check(!eng::util::interval_overlaps({0, 10}, {10, 20}), "intervalo: tocarse no solapa");
	check(!eng::util::interval_overlaps({0, 10}, {15, 25}), "intervalo: separados");
}

} // namespace

int main() {
	std::printf("Interval:\n");
	test_merge_and_contains();
	test_capacity();
	test_overlaps();

	if (g_fail == 0u) {
		std::printf("OK: Interval (fusion de solapes/adyacencias, contains, capacidad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
