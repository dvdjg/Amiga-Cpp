// ============================================================================
// Test HOST-088: Pool con handles generacionales.
// ============================================================================
//
// Respalda `eng/core/util/pool.hpp`: alta/baja O(1), reciclado de slots y validación
// por generación (un handle viejo deja de ser válido al reutilizar su slot).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/088_pool

#include <cstdio>

#include <eng/core/util/pool.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Item {
	int v = 0;
	bool flag = false;
};

} // namespace

int main() {
	std::printf("== HOST-088 pool ==\n");

	eu::Pool<Item, 3> pool;
	static_assert(pool.capacity() == 3u, "capacity()");
	check(pool.empty() && !pool.full(), "Pool vacío");

	const auto h0 = pool.add();
	const auto h1 = pool.add();
	const auto h2 = pool.add();
	check(h0.valid() && h1.valid() && h2.valid() && pool.size() == 3u, "3 altas");
	check(pool.full() && !pool.add().valid(), "lleno rechaza el alta 4");
	check(pool.get(h0) != nullptr && pool.get(h0)->v == 0, "get y slot reiniciado");

	pool.get(h0)->v = 42;
	check(pool.get(h0)->v == 42, "escritura a través del handle");
	check(pool.valid(h0) && pool.used(h0.index), "valid/used");
	check(pool.handle_at(h0.index) == h0, "handle_at coincide");
	check(pool.handle_at(0u) == h0 || pool.handle_at(1u) == h0 || pool.handle_at(2u) == h0,
	      "handle_at de un índice vivo coincide con algún handle");

	// Baja: el handle deja de ser válido y el slot se recicla con otra generación.
	const Item* old_ptr = pool.get(h1);
	check(pool.remove(h1) && pool.get(h1) == nullptr, "remove invalida el handle");
	check(!pool.valid(h1) && !pool.used(h1.index), "estado tras remove");
	check(!pool.remove(h1), "remove repetido devuelve false");
	check(pool.size() == 2u, "tamaño tras remove");

	const auto h1b = pool.add();
	check(h1b.valid() && h1b.index == h1.index && h1b.generation != h1.generation,
	      "el slot se recicla con nueva generación");
	check(!pool.valid(h1) && pool.valid(h1b), "el handle viejo sigue inválido");
	(void)old_ptr;

	// for_each recorre los vivos.
	eng::usize live = 0;
	pool.for_each([&](Item& it, eu::Pool<Item, 3>::Handle) {
		++live;
		it.flag = true;
	});
	check(live == pool.size(), "for_each cuenta los vivos");
	check(pool.get(h0)->flag && pool.get(h1b)->flag, "for_each permite mutar");

	pool.reset();
	check(pool.empty() && pool.size() == 0u && !pool.valid(h0), "reset vacía e invalida");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Pool validado.\n");
	return 0;
}
