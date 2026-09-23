// ============================================================================
// Test HOST-119: conjuntos disjuntos (Union-Find / DSU)
// ============================================================================
//
// Valida `engine/include/eng/core/util/union_find.hpp`:
//
//   1) Cada elemento arranca en su conjunto; `find` y `connected`.
//   2) `unite` fusiona, no repite y actualiza el tamano de componente.
//   3) Compresion de caminos (varias consultas no cambian el resultado).
//   4) `reset`.
//   5) Caso de uso: islas de celdas transitables en una tira (componentes conexas).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/119_union_find

#include <cstdio>

#include <eng/core/util/union_find.hpp>

namespace {

using eng::u16;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_basic() {
	eng::util::UnionFind<8> uf;
	check(uf.components() == 8u, "dsu: arranca con 8 conjuntos");
	check(uf.find(3) == 3u, "dsu: cada elemento es su raiz");
	check(!uf.connected(0, 1), "dsu: dos elementos sueltos no estan conectados");

	check(uf.unite(0, 1), "dsu: une 0 y 1");
	check(!uf.unite(1, 0), "dsu: unir lo ya unido devuelve false");
	check(uf.connected(0, 1) && !uf.connected(0, 2), "dsu: conecta solo los unidos");
	check(uf.components() == 7u, "dsu: quedan 7 conjuntos");
	check(uf.component_size(0) == 2u && uf.component_size(1) == 2u, "dsu: tamano 2");
}

void test_merge_and_path_compression() {
	eng::util::UnionFind<16> uf;
	uf.unite(0, 1);
	uf.unite(1, 2);
	uf.unite(3, 4);
	check(uf.components() == 13u, "dsu: tres fusiones -> 13 conjuntos");
	check(uf.component_size(2) == 3u, "dsu: componente {0,1,2}");

	uf.unite(0, 3); // fusiona {0,1,2} con {3,4}
	check(uf.find(2) == uf.find(4), "dsu: raiz comun tras la fusion");
	check(uf.component_size(4) == 5u, "dsu: componente de 5");
	check(uf.components() == 12u, "dsu: 12 conjuntos");

	// Consultas repetidas (con compresion de caminos) dan el mismo resultado.
	const u16 root = uf.find(0);
	for (int i = 0; i < 5; ++i) {
		check(uf.find(0) == root && uf.find(3) == root, "dsu: find estable");
	}
}

void test_reset() {
	eng::util::UnionFind<4> uf;
	uf.unite(0, 1);
	check(uf.components() == 3u, "dsu: 3 tras unir");
	uf.reset();
	check(uf.components() == 4u && !uf.connected(0, 1), "dsu: reset vuelve a 4");
}

void test_islands() {
	// Tira de 8 celdas; muros en 3 y 6. Unimos celdas adyacentes transitables.
	const bool walkable[8] = {true, true, true, false, true, true, false, true};
	eng::util::UnionFind<8> uf;
	for (u16 i = 0; i + 1u < 8u; ++i) {
		if (walkable[i] && walkable[i + 1u]) {
			uf.unite(i, static_cast<u16>(i + 1u));
		}
	}
	// Islas: {0,1,2}, {4,5}, {7} (los muros 3 y 6 quedan sueltos).
	check(uf.connected(0, 2) && uf.component_size(0) == 3u, "dsu: isla de 3");
	check(uf.connected(4, 5) && uf.component_size(4) == 2u, "dsu: isla de 2");
	check(!uf.connected(2, 4), "dsu: islas separadas por el muro");
	check(uf.find(7) == 7u, "dsu: celda suelta al final");
}

} // namespace

int main() {
	std::printf("UnionFind:\n");
	test_basic();
	test_merge_and_path_compression();
	test_reset();
	test_islands();

	if (g_fail == 0u) {
		std::printf("OK: UnionFind (unir, conectividad, tamanos, islas)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
