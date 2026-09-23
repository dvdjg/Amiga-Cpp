// ============================================================================
// Test HOST-073: rasgos de tipo y utilidades de lenguaje (eng::util).
// ============================================================================
//
// Respalda `eng/core/util/type_traits.hpp` y `eng/core/util/util.hpp`. Son
// metadatos de compilación y operaciones de valor que el runtime freestanding no
// puede tomar de `<type_traits>`/`<utility>`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/073_basic_type_traits

#include <cstdio>

#include <eng/core/util/type_traits.hpp>
#include <eng/core/util/util.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

enum class Channel : eng::u16 { Red = 3, Green = 7, Blue = 11 };

struct Counter {
	int value = 0;
};

} // namespace

int main() {
	std::printf("== HOST-073 type_traits + util ==\n");

	// --- Identidad y cualificadores -----------------------------------------
	static_assert(eu::is_same_v<eng::u32, unsigned long>, "u32 es unsigned long");
	static_assert(eu::is_same_v<eng::util::remove_cv_t<const volatile int>, int>, "remove_cv");
	static_assert(eu::is_same_v<eng::util::remove_reference_t<int&>, int>, "remove_reference");
	static_assert(eu::is_same_v<eng::util::remove_reference_t<int&&>, int>, "remove_reference rvalue");
	static_assert(eu::is_same_v<eng::util::remove_cvref_t<const int&>, int>, "remove_cvref");
	static_assert(eu::is_same_v<eng::util::remove_pointer_t<const int*>, const int>, "remove_pointer");
	static_assert(eu::is_same_v<eng::util::conditional_t<true, char, long>, char>, "conditional true");
	static_assert(eu::is_same_v<eng::util::conditional_t<false, char, long>, long>, "conditional false");
	static_assert(eu::is_same_v<eng::util::enable_if_t<true, char>, char>, "enable_if true");
	static_assert(eu::is_same_v<eng::util::make_unsigned_t<eng::s16>, eng::u16>, "make_unsigned s16");
	static_assert(eu::is_lvalue_reference_v<int&> && !eu::is_lvalue_reference_v<int>, "lvalue ref");
	static_assert(eu::is_rvalue_reference_v<int&&> && !eu::is_rvalue_reference_v<int&>, "rvalue ref");

	// --- Categorías ----------------------------------------------------------
	static_assert(eu::is_integral_v<int> && eu::is_integral_v<eng::u16>, "is_integral");
	static_assert(!eu::is_integral_v<float> && !eu::is_integral_v<Counter>, "is_integral false");
	static_assert(eu::is_floating_point_v<float> && !eu::is_floating_point_v<int>, "is_floating_point");
	static_assert(eu::is_arithmetic_v<double> && !eu::is_arithmetic_v<Counter>, "is_arithmetic");
	static_assert(eu::is_signed_v<eng::s16> && !eu::is_signed_v<eng::u16>, "is_signed");
	static_assert(eu::is_unsigned_v<eng::u16> && !eu::is_unsigned_v<eng::s16>, "is_unsigned");
	static_assert(eu::is_pointer_v<int*> && !eu::is_pointer_v<int>, "is_pointer");
	static_assert(eu::is_enum_v<Channel> && !eu::is_enum_v<int>, "is_enum");
	static_assert(eu::is_class_v<Counter> && !eu::is_class_v<int>, "is_class");
	static_assert(eu::is_trivially_copyable_v<eng::u32>, "trivially_copyable");
	static_assert(eu::is_trivially_destructible_v<Counter>, "trivially_destructible");

	// --- to_underlying -------------------------------------------------------
	static_assert(eu::to_underlying(Channel::Blue) == 11u, "to_underlying");

	// --- move / forward ------------------------------------------------------
	int probe = 0;
	static_assert(eu::is_same_v<decltype(eu::move(probe)), int&&>, "move -> rvalue ref");
	static_assert(eu::is_same_v<decltype(eu::as_const(probe)), const int&>, "as_const");

	// --- exchange ------------------------------------------------------------
	int state = 10;
	const int previous = eu::exchange(state, 42);
	check(previous == 10, "exchange devuelve el valor anterior");
	check(state == 42, "exchange deja el valor nuevo");

	// --- swap ----------------------------------------------------------------
	int a = 1;
	int b = 2;
	eu::swap(a, b);
	check(a == 2 && b == 1, "swap enteros");
	int arr1[3] = {1, 2, 3};
	int arr2[3] = {4, 5, 6};
	eu::swap(arr1, arr2);
	check(arr1[0] == 4 && arr1[2] == 6 && arr2[0] == 1 && arr2[2] == 3, "swap arrays");

	// --- min / max / clamp (genéricos, por referencia) -----------------------
	const int lo = eu::min(3, 8);
	const int hi = eu::max(3, 8);
	check(lo == 3 && hi == 8, "min/max devuelven el correcto");
	check(eu::clamp(5, 0, 4) == 4, "clamp por arriba");
	check(eu::clamp(-1, 0, 4) == 0, "clamp por abajo");
	check(eu::clamp(3, 0, 4) == 3, "clamp dentro");
	static_assert(eu::is_same_v<decltype(eu::min(1, 2)), const int&>, "min devuelve referencia");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: type_traits y util validados.\n");
	return 0;
}
