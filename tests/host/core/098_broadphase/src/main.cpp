// ============================================================================
// Test HOST-098: broadphase (eng::util::broadphase).
// ============================================================================
//
// Respalda `eng/core/util/broadphase.hpp`: SpatialHash con Pool + listas intrusivas.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/098_broadphase

#include <cstdio>

#include <eng/core/util/broadphase.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool contains(const eng::u16* ids, eng::usize n, eng::u16 id) {
	for (eng::usize i = 0; i < n; ++i) {
		if (ids[i] == id) {
			return true;
		}
	}
	return false;
}

} // namespace

int main() {
	std::printf("== HOST-098 broadphase ==\n");

	eu::SpatialHash<8, 4, 4, 8> grid; // celda 8, 4x4 celdas (32x32), 8 items
	static_assert(grid.cell_count() == 16u, "cell_count");

	check(grid.empty(), "vacío al inicio");
	check(grid.insert(1u, 2, 2), "insert (2,2)");
	check(grid.insert(2u, 10, 10), "insert (10,10)");
	check(grid.insert(3u, 2, 10), "insert (2,10)");
	check(grid.size() == 3u, "size");
	check(!grid.insert(4u, 32, 0), "insert fuera de la rejilla falla");

	// Consulta por celda.
	{
		eng::u16 hits[8] = {};
		const eng::usize n = grid.query(eu::Aabb {0, 0, 8, 8}, eng::Span<eng::u16> {hits, 8});
		check(n == 1u && contains(hits, n, 1u), "query celda (0,0) solo id 1");
	}
	{
		eng::u16 hits[8] = {};
		const eng::usize n = grid.query(eu::Aabb {0, 0, 16, 16}, eng::Span<eng::u16> {hits, 8});
		check(n == 3u && contains(hits, n, 1u) && contains(hits, n, 2u) && contains(hits, n, 3u),
		      "query 16x16 encuentra los 3");
	}
	{
		eng::u16 hits[2] = {};
		const eng::usize n = grid.query(eu::Aabb {0, 0, 16, 16}, eng::Span<eng::u16> {hits, 2});
		check(n == 3u, "query reporta el total aunque el buffer sea pequeño");
	}

	// for_each_in.
	{
		int count = 0;
		grid.for_each_in(eu::Aabb {0, 0, 32, 32}, [&](eng::u16, eng::s16, eng::s16) { ++count; });
		check(count == 3, "for_each_in recorre los 3");
	}

	// Llenado del pool.
	{
		eu::SpatialHash<8, 4, 4, 2> small;
		check(small.insert(1u, 1, 1) && small.insert(2u, 2, 2), "llena (2 items)");
		check(!small.insert(3u, 3, 3), "rechaza al llenar el pool");
		small.clear();
		check(small.empty(), "clear");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: broadphase validado.\n");
	return 0;
}
