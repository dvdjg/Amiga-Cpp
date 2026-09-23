// ============================================================================
// Test HOST-077: StaticVector y RingBuffer (capacidad fija).
// ============================================================================
//
// Respalda `eng/core/util/static_vector.hpp` y `eng/core/util/ring_buffer.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/077_static_vector_ring_buffer

#include <cstdio>

#include <eng/core/util/ring_buffer.hpp>
#include <eng/core/util/static_vector.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Point {
	int x = 0;
	int y = 0;
};

} // namespace

int main() {
	std::printf("== HOST-077 static_vector + ring_buffer ==\n");

	// --- StaticVector --------------------------------------------------------
	eu::StaticVector<int, 4> v;
	static_assert(v.capacity() == 4u, "capacity()");
	check(v.empty() && v.size() == 0u && !v.full(), "StaticVector vacío");
	check(v.push_back(1) && v.push_back(2) && v.push_back(3) && v.push_back(4), "push_back hasta llenar");
	check(v.full() && v.size() == 4u, "lleno");
	check(!v.push_back(5), "push_back rechaza si está lleno");
	check(v[0] == 1 && v[3] == 4, "operator[]");
	check(v.at(1) == 2, "at()");
	check(v.front() == 1 && v.back() == 4, "front/back");
	check(static_cast<eng::usize>(v.end() - v.begin()) == v.size(), "begin/end acotados al tamaño");
	v.pop_back();
	check(v.size() == 3u && v.back() == 3, "pop_back");
	v.erase(0u);
	check(v.size() == 2u && v[0] == 2 && v[1] == 3, "erase mantiene el orden");
	v.fill(8);
	check(v[0] == 8 && v[1] == 8, "fill");
	v.clear();
	check(v.empty() && v.size() == 0u, "clear");

	eu::StaticVector<Point, 2> pts;
	Point* p = pts.emplace_back();
	check(p == &pts[0] && pts.size() == 1u, "emplace_back devuelve el hueco");
	pts.emplace_back();
	check(pts.emplace_back() == nullptr, "emplace_back devuelve nullptr si está lleno");
	check(pts.span().size() == 2u, "span() acotado a los vivos");

	// --- RingBuffer ----------------------------------------------------------
	eu::RingBuffer<int, 3> r;
	static_assert(r.capacity() == 3u, "capacity()");
	check(r.empty() && r.size() == 0u && !r.full(), "RingBuffer vacío");
	check(r.push(1) && r.push(2) && r.push(3), "push hasta llenar");
	check(r.full() && !r.push(4), "push rechaza si está lleno");
	check(r.front() == 1 && r.back() == 3, "front/back");
	check(r.pop() == 1 && r.size() == 2u, "pop devuelve el más antiguo");
	r.push(4);
	check(r.front() == 2 && r[1] == 3 && r[2] == 4, "operator[] por antigüedad");
	r.push_overwrite(5);
	check(r.size() == 3u && r.front() == 3 && r.back() == 5, "push_overwrite descarta el más antiguo");
	r.pop_discard();
	check(r.front() == 4 && r.size() == 2u, "pop_discard");
	r.clear();
	check(r.empty() && r.size() == 0u, "clear");

	// Reutilización tras vaciar (los índices circulares siguen coherentes).
	for (int i = 10; i < 16; ++i) {
		check(r.push(i), "push tras clear");
		check(r.pop() == i, "pop tras clear");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: StaticVector y RingBuffer validados.\n");
	return 0;
}
