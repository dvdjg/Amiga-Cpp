// ============================================================================
// Test HOST-122: bitset de tamano elegido en init (DynamicBitSet)
// ============================================================================
//
// Valida `engine/include/eng/core/util/dynamic_bitset.hpp`:
//
//   1) init reserva en un Allocator y queda a cero; set/test/flip/reset/count/any.
//   2) Capacidad insuficiente -> init devuelve false.
//   3) init(0) y mascara de la ultima palabra parcial.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/122_dynamic_bitset

#include <cstdio>

#include <eng/core/util/dynamic_bitset.hpp>

namespace {

using eng::usize;
using eng::util::DynamicBitSet;
using eng::util::InlineAlloc;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_basic() {
	InlineAlloc<64> alloc;
	DynamicBitSet<InlineAlloc<64>> bits {alloc};
	check(bits.init(70u), "dyn: init de 70 bits (12 B)");
	check(bits.size() == 70u && bits.word_count() == 3u, "dyn: 3 palabras");
	check(bits.none() && bits.count() == 0u, "dyn: arranca a cero");

	bits.set(0u);
	bits.set(32u);
	bits.set(69u);
	check(bits.test(0u) && bits.test(32u) && bits.test(69u), "dyn: bits puestos");
	check(bits.count() == 3u && bits.any(), "dyn: cuenta 3");

	bits.flip(0u);
	check(!bits.test(0u) && bits.count() == 2u, "dyn: flip quita un bit");
	bits.reset(69u);
	check(!bits.test(69u) && bits.count() == 1u, "dyn: reset");
	bits.clear();
	check(bits.none(), "dyn: clear");
}

void test_capacity_and_empty() {
	InlineAlloc<4> small; // 4 bytes = 32 bits
	DynamicBitSet<InlineAlloc<4>> bits {small};
	check(!bits.init(64u), "dyn: 64 bits no caben en 4 B");
	check(bits.init(32u), "dyn: 32 bits caben");

	InlineAlloc<8> a2;
	DynamicBitSet<InlineAlloc<8>> zero {a2};
	check(zero.init(0u) && zero.empty() && !zero.any(), "dyn: init(0) es valido y vacio");
}

void test_partial_last_word() {
	InlineAlloc<16> alloc;
	DynamicBitSet<InlineAlloc<16>> bits {alloc};
	check(bits.init(33u), "dyn: 33 bits -> 2 palabras");
	bits.set(32u);
	check(bits.count() == 1u, "dyn: bit en la palabra parcial");
	for (usize i = 0u; i < 33u; ++i) {
		bits.set(i);
	}
	check(bits.count() == 33u, "dyn: la mascara de la ultima palabra no cuenta de mas");
}

} // namespace

int main() {
	std::printf("DynamicBitSet:\n");
	test_basic();
	test_capacity_and_empty();
	test_partial_last_word();

	if (g_fail == 0u) {
		std::printf("OK: DynamicBitSet (init, set/test/count, capacidad, palabra parcial)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
