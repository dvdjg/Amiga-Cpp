// ============================================================================
// Test HOST-120: SparseSet (conjunto disperso-denso, altas/bajas O(1))
// ============================================================================
//
// Valida `engine/include/eng/core/util/sparse_set.hpp`:
//
//   1) insert/find/contains/size y alta duplicada.
//   2) Iteracion densa (ids y valores contiguos).
//   3) erase con swap-remove (el ultimo ocupa el hueco, sin invalidar el resto).
//   4) insert_or_assign, capacidad, id fuera de rango y clear.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/120_sparse_set

#include <cstdio>

#include <eng/core/util/sparse_set.hpp>

namespace {

using eng::u16;
using eng::s32;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_basic() {
	eng::util::SparseSet<s32, 8> set;
	check(set.empty() && set.size() == 0u, "sparse: arranca vacio");

	check(set.insert(3u, 10), "sparse: inserta un id");
	check(set.contains(3u) && set.size() == 1u, "sparse: contiene el id");
	check(set.find(3u) != nullptr && *set.find(3u) == 10, "sparse: find devuelve el valor");
	check(!set.insert(3u, 20), "sparse: insert duplicado devuelve false");
	check(!set.insert_or_assign(3u, 20), "sparse: assign existente devuelve false");
	check(*set.find(3u) == 20, "sparse: assign actualiza el valor");

	check(set.insert(1u, 5) && set.insert(7u, 9), "sparse: caben mas ids");
	check(set.size() == 3u, "sparse: tres elementos");
	check(set.id_at(0u) == 3u && set.id_at(1u) == 1u && set.id_at(2u) == 7u,
	      "sparse: ids en orden denso");
	check(set.values()[0] == 20 && set.values()[1] == 5 && set.values()[2] == 9,
	      "sparse: valores en orden denso");

	// Borrar el del medio: el ultimo (id 7) ocupa su hueco.
	check(set.erase(1u), "sparse: erase");
	check(!set.contains(1u) && set.size() == 2u, "sparse: desaparece el id");
	check(set.id_at(0u) == 3u && set.id_at(1u) == 7u, "sparse: swap-remove compacta");
	check(*set.find(7u) == 9, "sparse: el movido conserva su valor");
	check(!set.erase(1u) && !set.erase(100u), "sparse: erase ausente/fuera -> false");
}

void test_capacity_and_clear() {
	eng::util::SparseSet<s32, 4> set;
	for (u16 i = 0u; i < 4u; ++i) {
		check(set.insert(i, static_cast<s32>(i) * 10), "sparse: caben 4");
	}
	check(set.full(), "sparse: lleno");
	check(!set.insert(4u, 0), "sparse: id fuera de rango -> false");
	check(!set.insert(0u, 0), "sparse: lleno y duplicado -> false");
	set.clear();
	check(set.empty() && !set.contains(0u), "sparse: clear vacia");
}

void test_assign_new() {
	eng::util::SparseSet<s32, 4> set;
	check(set.insert_or_assign(2u, 42), "sparse: assign de id nuevo devuelve true");
	check(set.contains(2u) && *set.find(2u) == 42, "sparse: assign crea el valor");
}

} // namespace

int main() {
	std::printf("SparseSet:\n");
	test_basic();
	test_capacity_and_clear();
	test_assign_new();

	if (g_fail == 0u) {
		std::printf("OK: SparseSet (insert/find, iteracion densa, swap-remove, capacidad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
