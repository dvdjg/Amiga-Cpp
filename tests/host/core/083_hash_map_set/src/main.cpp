// ============================================================================
// Test HOST-083: HashMap y HashSet (direccionamiento abierto + back-shift).
// ============================================================================
//
// Respalda `eng/core/util/hash_map.hpp` y `eng/core/util/hash_set.hpp`. Incluye un
// estrés determinista (xorshift) contra un modelo de referencia en arrays, para
// cubrir el borrado por back-shift en todas sus transiciones.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/083_hash_map_set

#include <cstdio>

#include <eng/core/util/hash_map.hpp>
#include <eng/core/util/hash_set.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Generador determinista del test (xorshift32).
struct Rng {
	eng::u32 state = 0x12345678u;
	eng::u32 next() {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		return state;
	}
};

} // namespace

int main() {
	std::printf("== HOST-083 hash_map + hash_set ==\n");

	// --- HashMap: casos basicos ---------------------------------------------
	eu::HashMap<eng::u16, int, 6> map;
	check(map.empty() && map.size() == 0u, "HashMap vacío");
	check(map.insert(5u, 50) != nullptr, "insert");
	check(map.insert(8u, 80) != nullptr, "insert 2");
	check(map.insert(5u, 99) == nullptr, "duplicado rechazado");
	check(map.find(5u) != nullptr && *map.find(5u) == 50, "el duplicado no sobrescribe");
	check(map.contains(8u) && !map.contains(9u), "contains");
	check(!map.insert_or_assign(5u, 55) && *map.find(5u) == 55, "insert_or_assign actualiza");
	check(map.insert_or_assign(1u, 11) && *map.find(1u) == 11, "insert_or_assign inserta");
	check(map.erase(5u) && !map.contains(5u) && map.size() == 2u, "erase");
	check(!map.erase(5u), "erase ausente");
	map.clear();
	check(map.empty(), "clear");

	// --- HashMap: estrés contra modelo de referencia ------------------------
	{
		constexpr eng::usize kKeys = 512;
		static bool ref_present[kKeys] = {};
		int ref_value[kKeys] = {};
		eng::usize ref_count = 0;
		eu::HashMap<eng::u16, int, 100> hash;
		Rng rng;

		for (int step = 0; step < 6000; ++step) {
			const eng::u16 k = static_cast<eng::u16>(rng.next() % kKeys);
			const eng::u32 op = rng.next() % 3u;
			if (op == 0u) {
				const int v = static_cast<int>(rng.next() % 1000u);
				const bool room = ref_present[k] || ref_count < hash.capacity();
				const bool inserted = hash.insert_or_assign(k, v);
				if (room) {
					check(inserted == !ref_present[k], "insert_or_assign retorno");
					if (!ref_present[k]) {
						ref_present[k] = true;
						++ref_count;
					}
					ref_value[k] = v;
				} else {
					check(!inserted, "rechazo al llenar");
				}
			} else if (op == 1u) {
				const bool erased = hash.erase(k);
				check(erased == ref_present[k], "erase coincide con la referencia");
				if (ref_present[k]) {
					ref_present[k] = false;
					--ref_count;
				}
			} else {
				const int* p = hash.find(k);
				check((p != nullptr) == ref_present[k], "find coincide con la referencia");
				if (ref_present[k]) {
					check(*p == ref_value[k], "valor coincide");
				}
			}
			check(hash.size() == ref_count, "size coincide con la referencia");
		}

		// for_each recorre exactamente las entradas vivas.
		eng::usize seen = 0;
		hash.for_each([&](eng::u16 key, int value) {
			++seen;
			check(ref_present[key] && ref_value[key] == value, "for_each coincide");
		});
		check(seen == ref_count, "for_each cuenta lo mismo que size");
	}

	// --- HashSet -------------------------------------------------------------
	{
		eu::HashSet<eng::u16, 8> set;
		check(set.insert(3u) && set.insert(9u), "insert set");
		check(!set.insert(3u), "duplicado rechazado");
		check(set.contains(3u) && !set.contains(4u), "contains set");
		check(set.erase(3u) && !set.contains(3u) && set.size() == 1u, "erase set");
	}
	{
		constexpr eng::usize kKeys = 256;
		static bool ref_present[kKeys] = {};
		eng::usize ref_count = 0;
		eu::HashSet<eng::u16, 60> set;
		Rng rng;
		for (int step = 0; step < 4000; ++step) {
			const eng::u16 k = static_cast<eng::u16>(rng.next() % kKeys);
			const eng::u32 op = rng.next() % 3u;
			if (op == 0u) {
				const bool room = ref_present[k] || ref_count < set.capacity();
				const bool inserted = set.insert(k);
				if (room) {
					check(inserted == !ref_present[k], "insert set retorno");
					if (!ref_present[k]) {
						ref_present[k] = true;
						++ref_count;
					}
				} else {
					check(!inserted, "rechazo al llenar el set");
				}
			} else if (op == 1u) {
				check(set.erase(k) == ref_present[k], "erase set coincide");
				if (ref_present[k]) {
					ref_present[k] = false;
					--ref_count;
				}
			} else {
				check(set.contains(k) == ref_present[k], "contains set coincide");
			}
			check(set.size() == ref_count, "size set coincide");
		}
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: HashMap y HashSet validados.\n");
	return 0;
}
