// ============================================================================
// Test HOST-333: idioma de error eng::util::Expected<T, Result> (valor o error).
// ============================================================================
//
// Respalda `eng/core/util/expected.hpp`: el análogo de `std::expected<T, E>` del engine, sin
// excepciones ni heap (el idiom único de error de las APIs nuevas; `Result` es el enum de causa).
// Comprueba construcción desde valor y desde error, `has_value`/`error`, `value`/`operator*` y
// `value_or`. Ya no hay un `eng::Expected<T>` aparte: el opcional en sitio es `util::Optional<T>`
// y el valor-o-error `util::Expected<T, E>`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/333_expected

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::util::Expected<int, eng::Result> half(int x) {
	if (x % 2 != 0) {
		return eng::util::unexpected(eng::Result::InvalidArgument);
	}
	return x / 2;
}

} // namespace

int main() {
	std::printf("== HOST-333 expected ==\n");

	// Exito con valor.
	const eng::util::Expected<int, eng::Result> ok {10};
	check(ok.has_value() && bool(ok), "exito -> has_value");
	check(ok.value() == 10 && *ok == 10, "exito -> value/operator*");
	check(ok.value_or(0) == 10, "exito -> value_or devuelve el valor");

	// Error.
	const eng::util::Expected<int, eng::Result> err {eng::util::unexpected(eng::Result::OutOfMemory)};
	check(!err.has_value() && !bool(err), "error -> no has_value");
	check(err.error() == eng::Result::OutOfMemory, "error -> error()");
	check(err.value_or(7) == 7, "error -> value_or fallback");

	// Uso tipico: devolver valor o error.
	const auto a = half(8);
	const auto b = half(3);
	check(a.has_value() && a.value() == 4, "half(8) = 4");
	check(!b.has_value() && b.error() == eng::Result::InvalidArgument, "half(3) -> error");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Expected (valor/error, has_value/error, value_or) validado.\n");
	return 0;
}
