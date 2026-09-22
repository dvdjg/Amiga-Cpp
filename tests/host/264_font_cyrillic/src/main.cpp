// Test HOST-264: glifos cirílicos en Font8 (ruso: А..Я, а..я, Ё/ё).
//
// Comprueba que `eng::Font8` cubre U+0410..U+044F y U+0401/U+0451, que las letras
// que comparten forma con las latinas (А/В/Е/Н/О/Р/С/Т/Х y а/е/о/с/х) usan el
// MISMO dibujo, que Ё/ё se componen con la diéresis, y que el decodificador UTF-8
// de 2 bytes (`eng::utf8::decode`) entrega el code point correcto para un literal
// cirílico (la cadena que un HUD escribiría).
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/264_font_cyrillic

#include <cstdio>

#include <eng/core/utf8.hpp>
#include <eng/graphics/font8.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// ¿El glifo `ch` tiene al menos un píxel en alguna fila?
bool nonempty(eng::u16 ch) {
	for (eng::u8 r = 0; r < eng::Font8::kRows; ++r) {
		if (eng::Font8::row(ch, r) != 0u) {
			return true;
		}
	}
	return false;
}

/// ¿El glifo `ch` es idéntico al glifo ASCII `ascii` en todas las filas?
bool same_as(eng::u16 ch, char ascii) {
	for (eng::u8 r = 0; r < eng::Font8::kRows; ++r) {
		if (eng::Font8::row(ch, r) != eng::Font8::row(static_cast<eng::u16>(ascii), r)) {
			return false;
		}
	}
	return true;
}

} // namespace

int main() {
	std::printf("== HOST-264 font cirilico ==\n");

	// --- Cobertura: las 64 letras (А..Я + а..я) tienen glifo no vacío ---------
	bool all = true;
	for (eng::u16 ch = eng::Font8::kCyrFirst; ch < eng::Font8::kCyrFirst + eng::Font8::kCyrCount; ++ch) {
		if (!nonempty(ch)) {
			std::printf("[FAIL] glifo cirilico vacio: U+%04X\n", ch);
			all = false;
			++g_fail;
		}
	}
	check(all, "las 64 letras cirilicas tienen glifo");

	// --- Formas compartidas con el latin (mayusculas) ------------------------
	check(same_as(0x0410, 'A'), "А == A");
	check(same_as(0x0412, 'B'), "В == B");
	check(same_as(0x0415, 'E'), "Е == E");
	check(same_as(0x041A, 'K'), "К == K");
	check(same_as(0x041C, 'M'), "М == M");
	check(same_as(0x041D, 'H'), "Н == H");
	check(same_as(0x041E, 'O'), "О == O");
	check(same_as(0x0420, 'P'), "Р == P");
	check(same_as(0x0421, 'C'), "С == C");
	check(same_as(0x0422, 'T'), "Т == T");
	check(same_as(0x0425, 'X'), "Х == X");

	// --- Formas compartidas con el latin (minusculas) ------------------------
	check(same_as(0x0430, 'a'), "а == a");
	check(same_as(0x0435, 'e'), "е == e");
	check(same_as(0x043E, 'o'), "о == o");
	check(same_as(0x0441, 'c'), "с == c");
	check(same_as(0x0445, 'x'), "х == x");

	// --- Ё/ё = Е/е + diéresis (id 5: filas 0/1 = 0x36) -----------------------
	check(eng::Font8::row(0x0401, 0) == static_cast<eng::u8>(eng::Font8::row('E', 0) | 0x36u),
	      "Ё = Е + dieresis (fila 0)");
	check(eng::Font8::row(0x0401, 1) == static_cast<eng::u8>(eng::Font8::row('E', 1) | 0x36u),
	      "Ё = Е + dieresis (fila 1)");
	check(eng::Font8::row(0x0451, 0) == static_cast<eng::u8>(eng::Font8::row('e', 0) | 0x36u),
	      "ё = е + dieresis (fila 0)");

	// --- Distintas entre si: letras que NO comparten forma difieren ----------
	check(!same_as(0x0411, 'B'), "Б != B (glifo propio)");
	check(!same_as(0x0418, 'H'), "И != H (diagonal)");
	check(!same_as(0x042F, 'R'), "Я != R (espejo)");

	// --- Fuera de rango: 0 ------------------------------------------------
	check(eng::Font8::row(0x0400, 0) == 0u, "U+0400 fuera de rango");
	check(eng::Font8::row(0x0450, 0) == 0u, "U+0450 fuera de rango");
	check(eng::Font8::row(0x0100, 0) == 0u, "U+0100 fuera de rango");

	// --- Decodificacion UTF-8 de un literal cirilico -------------------------
	{
		// "Привет" = П р и в е т (U+041F U+0440 U+0438 U+0432 U+0435 U+0442).
		const eng::u8* p = reinterpret_cast<const eng::u8*>("Привет");
		const eng::u16 expect[6] = {0x041F, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442};
		bool ok = true;
		for (int i = 0; i < 6; ++i) {
			const eng::u32 cp = eng::utf8::decode(p);
			if (static_cast<eng::u16>(cp) != expect[i] || !nonempty(static_cast<eng::u16>(cp))) {
				ok = false;
				std::printf("[FAIL] decode[%d] = U+%04X (esperado U+%04X)\n", i, cp, expect[i]);
			}
		}
		check(ok, "UTF-8 cirilico decodifica a U+04xx con glifo");
		check(eng::utf8::decode(p) == 0u, "fin de cadena");
	}

	if (g_fail == 0) {
		std::printf("OK: Font8 con cirilico ruso validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
