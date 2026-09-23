// ============================================================================
// Test HOST-082: FlatMap y FlatSet (mapa/conjunto ordenado, capacidad fija).
// ============================================================================
//
// Respalda `eng/core/util/flat_map.hpp` y `eng/core/util/flat_set.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/082_flat_map_set

#include <cstdio>

#include <eng/core/util/flat_map.hpp>
#include <eng/core/util/flat_set.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-082 flat_map + flat_set ==\n");

	// --- FlatMap -------------------------------------------------------------
	eu::FlatMap<eng::u16, int, 6> map;
	static_assert(map.capacity() == 6u, "capacity()");
	check(map.empty() && map.size() == 0u, "FlatMap vacío");

	check(map.insert(5u, 50) != nullptr, "insert 5");
	check(map.insert(3u, 30) != nullptr, "insert 3");
	check(map.insert(8u, 80) != nullptr, "insert 8");
	check(map.insert(1u, 10) != nullptr, "insert 1");
	check(map.size() == 4u && !map.full(), "tamaño tras inserts");

	// Orden por clave garantizado.
	check(map.at_index(0u).key == 1u && map.at_index(1u).key == 3u &&
		      map.at_index(2u).key == 5u && map.at_index(3u).key == 8u,
	      "entradas ordenadas por clave");

	check(map.find(5u) != nullptr && *map.find(5u) == 50, "find");
	check(map.find(4u) == nullptr, "find ausente");
	check(map.contains(8u) && !map.contains(9u), "contains");

	check(map.insert(5u, 999) == nullptr, "insert duplicado devuelve null");
	check(*map.find(5u) == 50, "el duplicado no sobrescribe");
	check(!map.insert_or_assign(3u, 99) && *map.find(3u) == 99, "insert_or_assign actualiza");
	check(map.insert_or_assign(2u, 20) && *map.find(2u) == 20, "insert_or_assign inserta");

	check(map.erase(5u) && map.find(5u) == nullptr && map.size() == 4u, "erase");
	check(!map.erase(5u), "erase ausente devuelve false");

	// Llenar y comprobar el rechazo controlado.
	eu::FlatMap<eng::u16, int, 3> full_map;
	check(full_map.insert(1u, 1) != nullptr, "llena 1");
	check(full_map.insert(2u, 2) != nullptr, "llena 2");
	check(full_map.insert(3u, 3) != nullptr, "llena 3");
	check(full_map.full(), "full()");
	check(full_map.insert(4u, 4) == nullptr, "insert con el mapa lleno = null");
	check(full_map.size() == 3u && !full_map.contains(4u), "estado intacto al llenar");
	full_map.clear();
	check(full_map.empty(), "clear");

	// --- FlatSet -------------------------------------------------------------
	eu::FlatSet<eng::u16, 5> set;
	static_assert(set.capacity() == 5u, "capacity()");
	check(set.insert(4u) && set.insert(2u) && set.insert(6u), "insert set");
	check(!set.insert(2u), "duplicado rechazado");
	check(set.size() == 3u, "tamaño del set");
	check(set.at_index(0u) == 2u && set.at_index(1u) == 4u && set.at_index(2u) == 6u,
	      "set ordenado");
	check(set.contains(4u) && !set.contains(5u), "contains set");
	check(set.erase(4u) && !set.contains(4u) && set.size() == 2u, "erase set");
	check(!set.erase(9u), "erase ausente");

	// Llenado hasta capacidad con rechazo controlado.
	eu::FlatSet<eng::u16, 2> tiny;
	check(tiny.insert(10u) && tiny.insert(20u) && tiny.full(), "set lleno");
	check(!tiny.insert(30u) && !tiny.contains(30u), "set lleno rechaza");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: FlatMap y FlatSet validados.\n");
	return 0;
}
