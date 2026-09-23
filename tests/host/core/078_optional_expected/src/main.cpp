// ============================================================================
// Test HOST-078: Optional y Expected (resultados sin excepciones).
// ============================================================================
//
// Respalda `eng/core/util/optional.hpp` y `eng/core/util/expected.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/078_optional_expected

#include <cstdio>

#include <eng/core/util/expected.hpp>
#include <eng/core/util/optional.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

enum class Err : eng::u8 { None = 0, Invalid = 1, TooBig = 2 };

/// Función que devuelve valor o error, con el patrón `unexpected`.
eu::Expected<int, Err> checked_div(int a, int b) {
	if (b == 0) {
		return eu::unexpected(Err::Invalid);
	}
	return a / b;
}

/// Búsqueda que devuelve "puede no haber", con el patrón `Optional`.
eu::Optional<eng::u8> find_slot(const int* used, eng::usize n) {
	for (eng::usize i = 0; i < n; ++i) {
		if (used[i] == 0) {
			return static_cast<eng::u8>(i);
		}
	}
	return {};
}

} // namespace

int main() {
	std::printf("== HOST-078 optional + expected ==\n");

	// --- Optional ------------------------------------------------------------
	eu::Optional<int> empty;
	check(!empty.has_value() && !static_cast<bool>(empty), "Optional vacío");
	check(empty.value_or(9) == 9, "value_or de vacío");

	eu::Optional<int> full {7};
	check(full.has_value() && static_cast<bool>(full), "Optional con valor");
	check(full.value() == 7 && *full == 7, "value/*");
	check(full.value_or(9) == 7, "value_or con valor");

	full = 12;
	check(full.value() == 12, "operator=(T)");
	full.emplace(20);
	check(full.value() == 20, "emplace");
	full.reset();
	check(!full.has_value(), "reset");

	eu::Optional<int> other {1};
	full = 2;
	full.swap(other);
	check(full.value() == 1 && other.value() == 2, "swap");

	const int used[4] = {1, 1, 0, 1};
	const auto slot = find_slot(used, 4);
	check(slot.has_value() && *slot == 2u, "find_slot encuentra el 2");
	const int full_used[2] = {1, 1};
	check(!find_slot(full_used, 2).has_value(), "find_slot devuelve vacío si no hay hueco");

	// --- Expected ------------------------------------------------------------
	const auto ok = checked_div(10, 2);
	check(ok.has_value() && static_cast<bool>(ok), "Expected con valor");
	check(ok.value() == 5 && *ok == 5, "Expected value/*");
	check(ok.value_or(-1) == 5, "Expected value_or con valor");

	const auto bad = checked_div(10, 0);
	check(!bad.has_value() && !static_cast<bool>(bad), "Expected con error");
	check(bad.error() == Err::Invalid, "Expected error()");
	check(bad.value_or(-1) == -1, "Expected value_or de error");

	eu::Expected<int, Err> assignable = eu::unexpected(Err::TooBig);
	check(!assignable && assignable.error() == Err::TooBig, "Expected construido con unexpected");
	assignable.emplace(3);
	check(assignable.has_value() && assignable.value() == 3, "Expected emplace");

	// --- Expected<void, E> ---------------------------------------------------
	const eu::Expected<void, Err> success;
	check(success.has_value(), "Expected<void> éxito por defecto");
	success.value();
	const eu::Expected<void, Err> failure = eu::unexpected(Err::Invalid);
	check(!failure.has_value() && failure.error() == Err::Invalid, "Expected<void> con error");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Optional y Expected validados.\n");
	return 0;
}
