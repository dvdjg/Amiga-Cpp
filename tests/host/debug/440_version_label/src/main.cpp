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
#include <eng/debug/version_strip.hpp>

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

/// Franja barata (`VersionStrip`): estática dibujada una vez + fps por diff (≤1/s). Sin
/// hardware: el target es un buffer planar sintético y los ciclos se inyectan.
void test_strip() {
	constexpr eng::u16 kWpr = 20u;                 // 320 px / 16
	constexpr eng::u32 kPlaneWords = kWpr * 16u;   // 16 filas
	static eng::u16 planes[4u * kPlaneWords];
	std::memset(planes, 0xAA, sizeof(planes));     // basura previa: la franja la pisa entera

	eng::debug::VersionStrip strip {};
	strip.init({planes, kWpr, kPlaneWords, 240u, 16u, 4u, 4u}, "DEMO X", {1u, 2u, 3u});

	// Fondo color 4 (bit 2) FUERA de los campos de texto: plano 2 a 1, planos 0/1/3 a 0.
	// (El texto pisa el fondo con sus colores: se comprueba en zonas sin texto.)
	const auto bg_word = [&](eng::u8 plane, eng::u16 row, eng::u16 w) {
		return planes[static_cast<eng::u32>(plane) * kPlaneWords +
			      static_cast<eng::u32>(row) * kWpr + w];
	};
	bool bg_ok = true;
	for (eng::u16 w = 12u; w < kWpr; ++w) { // fila 1 (y=241), tras el nombre/mensaje
		if (bg_word(0u, 1u, w) != 0u || bg_word(1u, 1u, w) != 0u ||
		    bg_word(2u, 1u, w) != 0xffffu || bg_word(3u, 1u, w) != 0u) {
			std::printf("  [dbg] fila1 w=%u: p0=%04x p1=%04x p2=%04x p3=%04x\n", w,
				    bg_word(0u, 1u, w), bg_word(1u, 1u, w), bg_word(2u, 1u, w),
				    bg_word(3u, 1u, w));
			bg_ok = false;
			break;
		}
	}
	// Filas SIN texto (los glifos ocupan 7 filas desde su y): 0 (y=240) y 8 (y=248).
	for (eng::u16 row = 0u; row < 16u && bg_ok; row += 8u) {
		for (eng::u16 w = 0u; w < kWpr; ++w) {
			if (bg_word(0u, row, w) != 0u || bg_word(1u, row, w) != 0u ||
			    bg_word(2u, row, w) != 0xffffu || bg_word(3u, row, w) != 0u) {
				std::printf("  [dbg] fila%u w=%u: p0=%04x p1=%04x p2=%04x p3=%04x\n",
					    row, w, bg_word(0u, row, w), bg_word(1u, row, w),
					    bg_word(2u, row, w), bg_word(3u, row, w));
				bg_ok = false;
				break;
			}
		}
	}
	CHECK(bg_ok);

	// Nombre (color 1 = plano 0) en la fila 1 (y=241): bits presentes.
	eng::u16 name_bits = 0u;
	for (eng::u16 i = 0u; i < kWpr; ++i) {
		name_bits = static_cast<eng::u16>(name_bits | planes[1u * kWpr + i]);
	}
	CHECK(name_bits != 0u);

	// fps: 50 frames en 1 s -> "FPS 50" (color 2 = plano 1) en x=268 (palabras 16..19).
	const auto fps_word = [&](eng::u16 w) {
		return planes[kPlaneWords + 9u * kWpr + w]; // plano 1, fila 9 (y=249)
	};
	const eng::u32 c0 = 1000u;
	strip.tick(0u, c0);                                       // arma la referencia
	strip.tick(50u, c0 + eng::debug::VersionStrip::kClockHz); // 1 s después
	eng::u16 fps_bits = 0u;
	for (eng::u16 w = 16u; w < kWpr; ++w) {
		fps_bits = static_cast<eng::u16>(fps_bits | fps_word(w));
	}
	CHECK(fps_bits != 0u);

	// Diff: otro segundo a 49 fps cambia el ÚLTIMO dígito (palabra 18) y no el primero (16).
	const eng::u16 last_before = fps_word(18u);
	const eng::u16 first_before = fps_word(16u);
	strip.tick(99u, c0 + 2u * eng::debug::VersionStrip::kClockHz); // 49 frames en 1 s
	CHECK(fps_word(18u) != last_before);
	CHECK(fps_word(16u) == first_before);

	// Mensaje a demanda: escribe en x=140 (palabras 8..) de la fila 1 con color 2 (plano 1)
	// y borrarlo limpia la celda.
	const auto msg_word = [&](eng::u16 w) { return bg_word(1u, 1u, w); };
	strip.message("HOLA");
	eng::u16 msg_bits = 0u;
	for (eng::u16 w = 8u; w < 12u; ++w) {
		msg_bits = static_cast<eng::u16>(msg_bits | msg_word(w));
	}
	CHECK(msg_bits != 0u);
	strip.message("");
	eng::u16 msg_after = 0u;
	for (eng::u16 w = 8u; w < 12u; ++w) {
		msg_after = static_cast<eng::u16>(msg_after | msg_word(w));
	}
	CHECK(msg_after == 0u);
}

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

	test_strip();

	if (g_fail == 0) {
		std::printf("OK: version_label + VersionStrip validados (formato, build, valores grandes,\n");
		std::printf("    capacidad, franja estatica y fps por diff <=1/s).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_fail);
	return 1;
}
