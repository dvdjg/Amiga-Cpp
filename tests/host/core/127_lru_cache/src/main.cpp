// ============================================================================
// Test HOST-127: cache LRU de capacidad fija (eng::util::LruCache)
// ============================================================================
//
// Valida `engine/include/eng/core/util/lru_cache.hpp`: get/put/erase O(1) y desalojo de
// la entrada menos recientemente usada al llenarse.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/127_lru_cache

#include <cstdio>

#include <eng/core/util/lru_cache.hpp>

namespace {

using eng::u16;
using eng::s32;
using Cache = eng::util::LruCache<u16, s32, 4>;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_eviction() {
	Cache c;
	check(c.empty() && c.capacity() == 4u, "lru: arranca vacia");
	check(c.put(1u, 10) && c.put(2u, 20) && c.put(3u, 30) && c.put(4u, 40),
	      "lru: caben 4 claves");
	check(c.full() && c.size() == 4u, "lru: llena");

	// get(1) lo hace el mas reciente; el orden queda 1,4,3,2.
	const s32* v = c.get(1u);
	check(v != nullptr && *v == 10, "lru: get devuelve el valor");

	// Insertar 5 desaloja la LRU (2).
	check(c.put(5u, 50), "lru: clave nueva");
	check(!c.contains(2u), "lru: desaloja la menos reciente (2)");
	check(c.contains(1u) && c.contains(3u) && c.contains(4u) && c.contains(5u),
	      "lru: las demas siguen");

	// Actualizar una existente no desaloja.
	check(c.put(1u, 11) == false, "lru: put existente devuelve false");
	check(*c.get(1u) == 11, "lru: se actualiza el valor");
}

void test_peek_does_not_touch() {
	Cache c;
	c.put(1u, 1);
	c.put(2u, 2);
	c.put(3u, 3);
	c.put(4u, 4); // orden MRU: 4,3,2,1
	const s32* p = c.peek(1u);
	check(p != nullptr && *p == 1, "lru: peek lee el valor");

	c.put(5u, 5); // debe desalojar la LRU (1): peek no la toco
	check(!c.contains(1u), "lru: peek no cambia la recencia");
	check(c.contains(2u) && c.contains(3u) && c.contains(4u) && c.contains(5u),
	      "lru: el resto sigue");
}

void test_erase_and_clear() {
	Cache c;
	for (u16 i = 1u; i <= 4u; ++i) {
		(void)c.put(i, static_cast<s32>(i) * 10);
	}
	check(c.erase(3u), "lru: erase");
	check(!c.contains(3u) && c.size() == 3u, "lru: desaparece");
	check(!c.erase(3u), "lru: erase ausente -> false");

	check(c.put(9u, 90), "lru: reutiliza una ranura libre");
	check(c.contains(9u) && c.size() == 4u, "lru: vuelve a estar llena");

	c.clear();
	check(c.empty() && !c.contains(9u), "lru: clear");
}

} // namespace

int main() {
	std::printf("LruCache:\n");
	test_eviction();
	test_peek_does_not_touch();
	test_erase_and_clear();

	if (g_fail == 0u) {
		std::printf("OK: LruCache (eviccion LRU, get/peek, erase, clear)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
