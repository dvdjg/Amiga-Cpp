// ============================================================================
// Test HOST-124: internado de cadenas (StringInterner)
// ============================================================================
//
// Valida `engine/include/eng/core/util/string_interner.hpp`:
//
//   1) Deduplicacion por contenido (mismo texto -> mismo id, distinto puntero).
//   2) `lookup` id -> texto.
//   3) Cadena vacia -> invalid; capacidad de cadenas agotada.
//   4) Sin bytes en la arena -> invalid.
//   5) `clear`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/124_string_interner

#include <cstdio>

#include <eng/core/util/string_interner.hpp>

namespace {

using eng::u16;
using eng::usize;
using eng::util::InlineAlloc;
using eng::util::StringInterner;
using eng::util::StringView;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using Names = StringInterner<16, InlineAlloc<128>>;

void test_dedup() {
	InlineAlloc<128> arena;
	Names names {arena};
	check(names.empty() && names.size() == 0u, "interner: arranca vacio");

	const u16 a = names.intern("hello");
	const u16 b = names.intern("world");
	check(a != Names::invalid && b != Names::invalid, "interner: dos ids validos");
	check(a != b && names.size() == 2u, "interner: ids distintos");
	check(names.intern("hello") == a, "interner: mismo texto -> mismo id");
	check(names.intern("hell") != a, "interner: prefijo distinto -> otro id");

	// Mismo contenido desde OTRO buffer (dedup por contenido, no por puntero).
	char buf[6] = {'h', 'e', 'l', 'l', 'o', '\0'};
	check(names.intern(StringView(buf, 5u)) == a, "interner: mismo contenido -> mismo id");

	check(names.lookup(a) == StringView("hello"), "interner: lookup devuelve el texto");
	check(names.lookup(b) == StringView("world"), "interner: lookup del segundo");
	check(names.lookup(99u).empty(), "interner: lookup de id inexistente -> vacio");
}

void test_empty_and_capacity() {
	InlineAlloc<64> arena;
	StringInterner<2, InlineAlloc<64>> names {arena};
	check(names.intern("") == StringInterner<2, InlineAlloc<64>>::invalid,
	      "interner: cadena vacia -> invalid");
	check(names.intern("aa") != StringInterner<2, InlineAlloc<64>>::invalid,
	      "interner: cabe la primera");
	check(names.intern("bb") != StringInterner<2, InlineAlloc<64>>::invalid,
	      "interner: cabe la segunda");
	check(names.intern("cc") == StringInterner<2, InlineAlloc<64>>::invalid,
	      "interner: no caben mas cadenas");
}

void test_arena_exhaustion() {
	InlineAlloc<4> arena; // 4 bytes
	StringInterner<8, InlineAlloc<4>> names {arena};
	check(names.intern("abc") != StringInterner<8, InlineAlloc<4>>::invalid,
	      "interner: 'abc' cabe en 4 B");
	check(names.intern("de") == StringInterner<8, InlineAlloc<4>>::invalid,
	      "interner: 'de' no cabe (sin bytes)");
}

void test_clear() {
	InlineAlloc<64> arena;
	StringInterner<4, InlineAlloc<64>> names {arena};
	check(names.intern("x") != StringInterner<4, InlineAlloc<64>>::invalid, "interner: x");
	check(names.intern("y") != StringInterner<4, InlineAlloc<64>>::invalid, "interner: y");
	check(names.size() == 2u, "interner: dos antes de clear");
	names.clear();
	check(names.empty(), "interner: clear vacia el indice");
	check(names.intern("z") != StringInterner<4, InlineAlloc<64>>::invalid,
	      "interner: reutilizable tras clear");
}

} // namespace

int main() {
	std::printf("StringInterner:\n");
	test_dedup();
	test_empty_and_capacity();
	test_arena_exhaustion();
	test_clear();

	if (g_fail == 0u) {
		std::printf("OK: StringInterner (dedup por contenido, lookup, capacidad, arena)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
