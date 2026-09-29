// ============================================================================
// Test HOST-333: idioma de error eng::Expected<T> (valor o Result).
// ============================================================================
//
// Respalda `eng/core/types/result.hpp`: el análogo de `std::expected<T, E>` del engine, sin
// excepciones ni heap. Comprueba construcción desde valor y desde error, `ok`/`status`,
// `value`/`operator*` y `value_or`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/333_expected

#include <cstdio>

#include <eng/core/types/result.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::Expected<int> half(int x) {
	if (x % 2 != 0) {
		return eng::Result::InvalidArgument;
	}
	return x / 2;
}

} // namespace

int main() {
	std::printf("== HOST-333 expected ==\n");

	// Exito con valor.
	const eng::Expected<int> ok {10};
	check(ok.ok() && bool(ok), "exito -> ok");
	check(ok.status() == eng::Result::Ok, "exito -> status Ok");
	check(ok.value() == 10 && *ok == 10, "exito -> value/operator*");
	check(ok.value_or(0) == 10, "exito -> value_or devuelve el valor");

	// Error.
	const eng::Expected<int> err {eng::Result::OutOfMemory};
	check(!err.ok() && !bool(err), "error -> no ok");
	check(err.status() == eng::Result::OutOfMemory, "error -> status");
	check(err.value_or(7) == 7, "error -> value_or fallback");

	// Uso tipico: devolver valor o error.
	const auto a = half(8);
	const auto b = half(3);
	check(a.ok() && a.value() == 4, "half(8) = 4");
	check(!b.ok() && b.status() == eng::Result::InvalidArgument, "half(3) -> error");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Expected (valor/error, status, value_or) validado.\n");
	return 0;
}
