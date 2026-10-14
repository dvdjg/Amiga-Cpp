// ============================================================================
// Test HOST-440: versionado de demos (`eng/debug/version.hpp`, norma 2026-10).
// ============================================================================
//
// Respalda la norma de versionado (AGENTS §0, PROCEDIMIENTO §3.18): toda demo lleva
// `major.minor.build` y lo muestra en la franja inferior (16 px, 4 bpp). Aquí se valida la
// única pieza pura: `version_label()` (formato `vM.m.B`, sin stdlib, con límite de
// capacidad y sin desbordar).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/debug/440_version_label

#include <cstdio>
#include <cstring>

#include <eng/debug/version.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

[[nodiscard]] bool label_is(eng::debug::Version v, const char* want) {
	char buf[16];
	const eng::u8 n = eng::debug::version_label(v, buf, static_cast<eng::u8>(sizeof(buf)));
	return n == std::strlen(want) && std::strcmp(buf, want) == 0;
}

} // namespace

int main() {
	std::printf("Test HOST-440 version_label (versionado de demos)\n");
	std::printf("=================================================\n");

	// Formato básico major.minor.build.
	CHECK(label_is({1u, 0u, 1u}, "v1.0.1"));
	CHECK(label_is({2u, 13u, 456u}, "v2.13.456"));
	CHECK(label_is({0u, 0u, 0u}, "v0.0.0"));

	// El build por defecto viene del define del build (`ENG_BUILD_NUMBER`); sin define = 0.
	{
		char buf[16];
		(void)eng::debug::version_label(eng::debug::Version {1u, 0u}, buf,
						static_cast<eng::u8>(sizeof(buf)));
		char want[16];
		std::snprintf(want, sizeof(want), "v1.0.%u", static_cast<unsigned>(ENG_BUILD_NUMBER));
		CHECK(std::strcmp(buf, want) == 0);
	}

	// Valores grandes: se formatean completos dentro de la capacidad (buffer amplio).
	{
		char big[32];
		const eng::u8 n = eng::debug::version_label({65535u, 65535u, 4294967295u}, big,
							    static_cast<eng::u8>(sizeof(big)));
		CHECK(n == std::strlen("v65535.65535.4294967295"));
		CHECK(std::strcmp(big, "v65535.65535.4294967295") == 0);
	}

	// Capacidad corta: trunca sin desbordar y siempre NUL-termina.
	{
		char small[6];
		std::memset(small, 'X', sizeof(small));
		const eng::u8 n = eng::debug::version_label({1u, 0u, 1u}, small,
							    static_cast<eng::u8>(sizeof(small)));
		CHECK(n <= 5u);
		CHECK(small[5] == '\0');
		CHECK(small[0] == 'v');
	}

	if (g_fail == 0) {
		std::printf("OK: version_label validado (formato, build, valores grandes, capacidad).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_fail);
	return 1;
}
