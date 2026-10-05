// ============================================================================
// Test HOST-403: normalización de paths del VFS (R6.1)
// ============================================================================
//
// Verifica `eng::os::normalize_path`: colapso de separadores, `.`/`..`, absolutos y errores
// (vacío, escape por encima de la raíz, sin cabida). Función pura, sin E/S.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/os/403_path_normalize

#include <cstdio>

#include <eng/os/path.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

eng::usize slen(const char* s) {
	eng::usize n = 0u;
	while (s[n] != '\0') {
		++n;
	}
	return n;
}

/// Normaliza `src` a un buffer estático; devuelve el resultado (o `nullptr` si falla).
const char* norm(const char* src, eng::os::PathError* err = nullptr) {
	static char buf[128];
	const auto r = eng::os::normalize_path(eng::util::StringView {src, slen(src)},
					       eng::Span<char> {buf, sizeof(buf)});
	if (!r.has_value()) {
		if (err != nullptr) *err = r.error();
		return nullptr;
	}
	buf[*r] = '\0';
	return buf;
}

bool eq(const char* a, const char* b) {
	for (eng::usize i = 0u;; ++i) {
		if (a[i] != b[i]) return false;
		if (a[i] == '\0') return true;
	}
}

void test_ok() {
	check(eq(norm("a/b/c"), "a/b/c"), "relativo simple");
	check(eq(norm("/a//b/./c"), "/a/b/c"), "absoluto + colapso + punto");
	check(eq(norm("a/../b"), "b"), "sube un nivel");
	check(eq(norm("a/b/../../c"), "c"), "sube varios niveles");
	check(eq(norm("DF0:data/spr.bpl"), "DF0:data/spr.bpl"), "dispositivo preservado");
	check(eq(norm("/"), "/"), "raíz absoluta");
	check(eq(norm("a/.."), ""), "todo consumido por .. → vacío");
	check(eq(norm("."), ""), "punto solo → vacío");
}

void test_errors() {
	eng::os::PathError e {};
	check(norm("") == nullptr && e == eng::os::PathError::Empty, "vacío → Empty");
	check(norm("/a/../../b", &e) == nullptr && e == eng::os::PathError::EscapesRoot,
	      "escape por encima de la raíz → EscapesRoot");
	check(norm("a/../../b", &e) == nullptr && e == eng::os::PathError::EscapesRoot,
	      "escape en relativo → EscapesRoot");

	// Destino sin cabida → TooLong.
	char tiny[3] {};
	const auto r = eng::os::normalize_path(eng::util::StringView {"abcd/efgh", 9u},
					       eng::Span<char> {tiny, sizeof(tiny)});
	check(!r.has_value() && r.error() == eng::os::PathError::TooLong, "destino sin cabida → TooLong");
}

} // namespace

int main() {
	test_ok();
	test_errors();
	if (failures == 0) {
		std::printf("OK: normalización de paths (R6.1) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
