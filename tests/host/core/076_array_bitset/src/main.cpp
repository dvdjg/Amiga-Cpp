// ============================================================================
// Test HOST-076: Array y BitSet (contenedores de tamaño fijo).
// ============================================================================
//
// Respalda `eng/core/util/array.hpp` y `eng/core/util/bitset.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/076_array_bitset

#include <cstdio>

#include <eng/core/util/array.hpp>
#include <eng/core/util/bitset.hpp>

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
	std::printf("== HOST-076 array + bitset ==\n");

	// --- Array ---------------------------------------------------------------
	eu::Array<eng::u16, 4> offsets { {0u, 16u, 16u, 48u} };
	static_assert(eu::Array<eng::u16, 4>::size() == 4u, "size() estática");
	static_assert(!eu::Array<eng::u16, 4>::empty(), "empty() es false con N > 0");
	static_assert(eu::Array<eng::u16, 4>::capacity() == 4u, "capacity()");
	check(offsets[0] == 0u && offsets[3] == 48u, "operator[]");
	check(offsets.at(2) == 16u, "at()");
	check(offsets.front() == 0u && offsets.back() == 48u, "front/back");
	eng::u16 span_sum = 0;
	for (eng::u16 v : offsets) {
		span_sum = static_cast<eng::u16>(span_sum + v);
	}
	check(span_sum == 80u, "range-for del agregado");
	offsets.fill(7u);
	check(offsets[0] == 7u && offsets[3] == 7u, "fill");

	eu::Array<int, 3> a { {1, 2, 3} };
	eu::Array<int, 3> b { {1, 2, 3} };
	check(a == b, "operator==");
	b.fill(9);
	check(a != b, "operator!= (reescrito de ==)");
	a.swap(b);
	check(a[0] == 9 && b[0] == 1, "swap");
	check(a.span().size() == 3u && a.span()[2] == 9, "vista Span del Array");

	// --- BitSet de una palabra (< 32) ---------------------------------------
	eu::BitSet<5> b5;
	check(b5.none() && !b5.any() && b5.count() == 0u && !b5.all(), "BitSet<5> vacío");
	b5.set(0);
	b5.set(4);
	check(b5.test(0) && b5.test(4) && !b5.test(1), "set/test");
	check(b5.count() == 2u && b5.any() && !b5.all(), "count/any/all con 2 de 5");
	b5.set(0, false);
	check(!b5.test(0), "set(i, false)");
	b5.flip(1);
	check(b5.test(1), "flip(i)");
	b5.reset();
	b5.flip();
	check(b5.all() && b5.count() == 5u, "flip() de todos: los bits extra no cuentan");
	b5.reset();
	check(b5.none(), "reset()");

	// --- BitSet de una palabra completa (32) --------------------------------
	eu::BitSet<32> b32;
	b32.set(31);
	check(b32.test(31) && b32.count() == 1u && !b32.all(), "BitSet<32> bit 31");
	b32.reset(31);
	check(b32.none(), "BitSet<32> reset(31)");

	// --- BitSet de varias palabras (40) -------------------------------------
	eu::BitSet<40> b40;
	b40.set(39);
	check(b40.test(39) && b40.count() == 1u, "BitSet<40> cruza la palabra");
	b40.reset();
	b40.flip();
	check(b40.all() && b40.count() == 40u, "BitSet<40> flip(): 8 bits de cola no cuentan");
	b40.reset(39);
	check(!b40.test(39) && b40.count() == 39u, "BitSet<40> reset(39)");
	eu::BitSet<40> copy = b40;
	check(copy == b40, "BitSet operator==");
	copy.reset(0);
	check(copy != b40, "BitSet operator!=");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Array y BitSet validados.\n");
	return 0;
}
