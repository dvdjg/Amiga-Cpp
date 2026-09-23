// ============================================================================
// Test HOST-085: DirectMap (clave densa, acceso O(1)).
// ============================================================================
//
// Respalda `eng/core/util/direct_map.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/085_direct_map

#include <cstdio>

#include <eng/core/util/direct_map.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Pixel {
	eng::u8 r = 0;
	eng::u8 g = 0;
	eng::u8 b = 0;
};

} // namespace

int main() {
	std::printf("== HOST-085 direct_map ==\n");

	eu::DirectMap<int, 8> map;
	static_assert(map.key_range() == 8u, "key_range()");
	check(map.empty() && map.size() == 0u, "DirectMap vacío");

	check(map.insert(0u, 10) != nullptr && *map.find(0u) == 10, "insert/find");
	check(map.insert(0u, 99) == nullptr, "duplicado rechazado");
	check(*map.find(0u) == 10, "el duplicado no sobrescribe");
	check(map.insert(3u, 30) != nullptr && map.insert(7u, 70) != nullptr, "insert disperso");
	check(map.contains(3u) && !map.contains(4u), "contains");
	check(map.size() == 3u, "size");

	// Clave fuera de rango: nunca toca memoria ni aborta.
	check(!map.contains(100u) && map.find(100u) == nullptr, "clave fuera de rango");
	check(map.insert(100u, 1) == nullptr, "insert fuera de rango = null");
	check(map.insert_or_assign(100u, 1) == false && !map.contains(100u),
	      "insert_or_assign fuera de rango no toca");

	check(!map.insert_or_assign(3u, 33) && *map.find(3u) == 33, "insert_or_assign actualiza");
	check(map.insert_or_assign(5u, 55) && *map.find(5u) == 55, "insert_or_assign inserta");
	check(map.erase(3u) && !map.contains(3u) && map.size() == 3u, "erase");
	check(!map.erase(3u), "erase ausente");

	// for_each recorre las claves presentes (en orden) y coincide con size.
	eng::usize seen = 0;
	map.for_each([&](eng::usize key, int value) {
		++seen;
		check(map.contains(key) && *map.find(key) == value, "for_each coherente");
	});
	check(seen == map.size(), "for_each cuenta igual que size");

	// Lleno.
	eu::DirectMap<int, 2> full_map;
	check(full_map.insert(0u, 1) && full_map.insert(1u, 2), "llena");
	check(full_map.full() && full_map.size() == 2u, "full()");
	full_map.clear();
	check(full_map.empty(), "clear");

	// Valor no escalar (struct) sobre el almacenamiento tipado.
	eu::DirectMap<Pixel, 4> pixels;
	check(pixels.insert(1u, Pixel {10u, 20u, 30u}) != nullptr, "insert struct");
	const Pixel* p = pixels.find(1u);
	check(p != nullptr && p->r == 10u && p->g == 20u && p->b == 30u, "contenido struct");
	check(pixels.find(2u) == nullptr, "clave ausente");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: DirectMap validado.\n");
	return 0;
}
