// ============================================================================
// Test HOST-080: asignadores y hashes de eng::util.
// ============================================================================
//
// Respalda `eng/core/util/allocator.hpp` y `eng/core/util/hash.hpp`: el contrato
// `Allocator`, los bumps (Null/Bump/Inline) y los hashes sin `__mulsi3`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/080_allocator_hash

#include <cstdio>

#include <eng/core/util/allocator.hpp>
#include <eng/core/util/hash.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool aligned(const void* p, eng::uintptr a) {
	return (reinterpret_cast<eng::uintptr>(p) & (a - 1u)) == 0u;
}

} // namespace

int main() {
	std::printf("== HOST-080 allocator + hash ==\n");

	static_assert(eu::Allocator<eu::NullAlloc>, "NullAlloc cumple Allocator");
	static_assert(eu::Allocator<eu::BumpAlloc>, "BumpAlloc cumple Allocator");
	static_assert(eu::Allocator<eu::InlineAlloc<64>>, "InlineAlloc cumple Allocator");

	// --- NullAlloc -----------------------------------------------------------
	eu::NullAlloc null_alloc;
	check(null_alloc.allocate(16u, 4u).empty(), "NullAlloc no asigna");

	// --- BumpAlloc -----------------------------------------------------------
	alignas(16) eng::u8 buf[64] {};
	eu::BumpAlloc bump {eng::Span<eng::u8> {buf, sizeof(buf)}};
	const auto first = bump.allocate(8u, 16u);
	check(!first.empty() && first.size() == 8u, "BumpAlloc asigna");
	check(aligned(first.data(), 16u), "BumpAlloc respeta la alineacion");
	const auto second = bump.allocate(16u, 4u);
	check(!second.empty() && second.data() != first.data(), "BumpAlloc no solapa");
	check(bump.used() >= 24u && bump.remaining() == bump.capacity() - bump.used(), "contabilidad");
	check(bump.allocate(4096u, 4u).empty(), "BumpAlloc falla si no cabe");
	bump.clear();
	check(bump.used() == 0u, "clear reinicia el offset");

	// --- InlineAlloc ---------------------------------------------------------
	eu::InlineAlloc<32> inline_alloc;
	const auto a1 = inline_alloc.allocate(16u, 8u);
	check(!a1.empty() && aligned(a1.data(), 8u), "InlineAlloc asigna alineado");
	check(inline_alloc.allocate(32u, 1u).empty(), "InlineAlloc falla si no queda hueco");
	inline_alloc.clear();
	check(inline_alloc.remaining() == eu::InlineAlloc<32>::capacity(), "InlineAlloc clear");

	// --- hash de enteros -----------------------------------------------------
	check(eu::hash_u32(0u) != eu::hash_u32(1u), "hash_u32 distingue 0 y 1");
	check(eu::hash_value(static_cast<eng::u16>(5u)) == eu::hash_u16(5u), "hash_value(u16) = hash_u16");
	check(eu::hash_value(static_cast<eng::u32>(5u)) == eu::hash_u32(5u), "hash_value(u32) = hash_u32");
	check(eu::hash_value(static_cast<eng::u8>(7u)) == eu::hash_u8(7u), "hash_value(u8) = hash_u8");

	// Dispersión: 16 claves u16 consecutivas deben dar casi 16 hashes distintos.
	int distinct = 0;
	for (eng::u16 i = 0; i < 16u; ++i) {
		bool seen = false;
		for (eng::u16 j = 0; j < i && !seen; ++j) {
			seen = eu::hash_u16(i) == eu::hash_u16(j);
		}
		if (!seen) {
			++distinct;
		}
	}
	check(distinct == 16, "hash_u16 dispersa 16 claves consecutivas");

	// Enums y punteros.
	enum class Kind : eng::u16 { A = 1, B = 2 };
	check(eu::hash_value(Kind::A) != eu::hash_value(Kind::B), "hash_value(enum)");
	int value = 0;
	check(eu::hash_value(&value) == eu::hash_value(&value), "hash_value(puntero) estable");

	// --- hash de texto -------------------------------------------------------
	const eu::StringView hello {"hello"};
	check(eu::hash_string(hello) == eu::hash_string(eu::StringView("hello")),
	      "hash_string estable");
	check(eu::hash_string(hello) != eu::hash_string(eu::StringView("world")),
	      "hash_string distingue");
	check(eu::Hash<eu::StringView> {}(hello) == eu::hash_string(hello), "Hash<StringView>");
	check(eu::Hash<eng::u16> {}(static_cast<eng::u16>(9u)) == eu::hash_u16(9u), "Hash<u16>");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: asignadores y hashes validados.\n");
	return 0;
}
