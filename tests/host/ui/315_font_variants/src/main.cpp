// ============================================================================
// Test HOST-315: fuentes derivadas — cursiva (Font8/Font5x7) y micro-fuente Font3x5.
// ============================================================================
//
// Valida `eng/graphics/font_italic.hpp`: el *shear* cursivo (`italic_shift`,
// `font8_row_italic`, `font5x7_row_italic`) y la micro-fuente `Font3x5` derivada de `Font5x7`.
// Ver GUI_LIBRARY.md 6 y ROADMAP_GUI.md (coleccion de glifos).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/315_font_variants

#include <cstdio>

#include <eng/graphics/font5x7.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/font_italic.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Peso (nº de bits activos) de una fila.
int popcount8(eng::u8 b) {
	int n = 0;
	for (eng::u8 k = 0u; k < 8u; ++k) {
		if (((b >> k) & 1u) != 0u) ++n;
	}
	return n;
}

} // namespace

int main() {
	// --- Shear: creciente por fila, 0 arriba, max abajo ---
	check(eng::italic_shift(0u, eng::Font8::kRows, 2u) == 0u, "italic_shift fila 0 = 0");
	check(eng::italic_shift(eng::Font8::kRows - 1u, eng::Font8::kRows, 2u) == 2u,
	      "italic_shift ultima fila = max");
	bool growing = true;
	eng::u8 prev = 0u;
	for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
		const eng::u8 s = eng::italic_shift(r, eng::Font8::kRows, 2u);
		if (s < prev) growing = false;
		prev = s;
	}
	check(growing, "italic_shift no decrece");

	// --- Font8 cursiva: el shear nunca AÑADE bits (puede recortar en el borde) ---
	// Un glifo que no toca la columna 7 conserva los bits; el shear solo mueve a la derecha.
	bool no_add = true;
	bool preserved = true;
	for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
		const eng::u8 base = eng::Font8::row('I', r); // 'I' estrecha (no toca la col. 7)
		const eng::u8 ital = eng::font8_row_italic('I', r);
		if (popcount8(ital) > popcount8(base)) no_add = false;
		if (popcount8(base) != popcount8(ital)) preserved = false;
	}
	check(no_add, "Font8 cursiva no añade bits");
	check(preserved, "Font8 cursiva conserva los bits (glifo estrecho)");
	// La fila inferior (r=7) se desplaza a la derecha: sus bits son distintos de la base.
	check(eng::font8_row_italic('I', 7u) != eng::Font8::row('I', 7u) ||
		      eng::Font8::row('I', 7u) == 0u,
	      "Font8 cursiva desplaza la fila inferior");

	// --- Font5x7 cursiva: no añade bits ---
	check(eng::italic_shift(0u, eng::Font5x7::kRows, 2u) == 0u, "Font5x7 shear fila 0 = 0");
	bool no_add5 = true;
	for (eng::u8 r = 0u; r < eng::Font5x7::kRows; ++r) {
		const eng::u8 base = eng::Font5x7::row('L', r);
		int bc = 0, ic = 0;
		for (eng::u8 k = 0u; k < 5u; ++k) {
			if (((base >> k) & 1u) != 0u) ++bc;
			if (((eng::font5x7_row_italic('L', r) >> k) & 1u) != 0u) ++ic;
		}
		if (ic > bc) no_add5 = false;
	}
	check(no_add5, "Font5x7 cursiva no añade bits");

	// --- Micro-fuente Font3x5 derivada de Font5x7 ---
	// Toma filas 1..5 (no la 0 ni la 6): para 'A' debe tener tinta en el cuerpo.
	bool any = false;
	for (eng::u8 r = 0u; r < eng::Font3x5::kRows; ++r) {
		if (eng::Font3x5::row('A', r) != 0u) any = true;
	}
	check(any, "Font3x5 de 'A' tiene tinta");
	// Dimensiones: 3 bits de ancho máximo.
	bool width_ok = true;
	for (eng::u8 r = 0u; r < eng::Font3x5::kRows; ++r) {
		if ((eng::Font3x5::row('W', r) & 0xf8u) != 0u) width_ok = false;
	}
	check(width_ok, "Font3x5 usa <= 3 bits de ancho");
	// Cursiva de la micro-fuente: no añade bits respecto a la base.
	bool no_add3 = false;
	{
		const eng::u8 base = eng::Font3x5::row('A', eng::Font3x5::kRows - 1u);
		const eng::u8 ital = eng::Font3x5::row_italic('A', eng::Font3x5::kRows - 1u);
		int bc = 0, ic = 0;
		for (eng::u8 k = 0u; k < 3u; ++k) {
			if (((base >> k) & 1u) != 0u) ++bc;
			if (((ital >> k) & 1u) != 0u) ++ic;
		}
		no_add3 = ic <= bc;
	}
	check(no_add3, "Font3x5 cursiva no añade bits");

	// --- La colección cubre lo mismo (mismos code points que sus fuentes) ---
	check(eng::Font3x5::row('Z', 2u) != 0u, "Font3x5 cubre ASCII");

	if (failures == 0) {
		std::printf("OK: variantes de fuente (cursiva + micro) validadas.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
