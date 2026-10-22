// ============================================================================
// Test HOST-096: texto (eng::util::text).
// ============================================================================
//
// Respalda `eng/core/util/text.hpp`: trim, split_next, equal_ci, parse de enteros
// (por ancho y por escalar), conversion a decimal (entera, escalada y de `Fixed`) y join.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/096_text

#include <cstdio>

#include <eng/core/math/fixed.hpp>
#include <eng/core/util/text.hpp>
#include <eng/core/util/text_line.hpp>

namespace eu = eng::util;
using Q12 = eng::math::Fixed<eng::s16, 12>;
using Q8 = eng::math::Fixed<eng::s16, 8>;
using Q0 = eng::math::Fixed<eng::s16, 0>;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-096 text ==\n");

	// --- trim ----------------------------------------------------------------
	check(eu::trim(eu::StringView("  hi \t")) == eu::StringView("hi"), "trim");
	check(eu::trim(eu::StringView("")) == eu::StringView(""), "trim vacío");
	check(eu::trim(eu::StringView("nada")) == eu::StringView("nada"), "trim sin espacios");

	// --- split_next ----------------------------------------------------------
	{
		eu::StringView rest {eu::StringView("a,b,,c")};
		check(eu::split_next(rest, ',') == eu::StringView("a"), "split 1");
		check(eu::split_next(rest, ',') == eu::StringView("b"), "split 2");
		check(eu::split_next(rest, ',') == eu::StringView(""), "split vacío");
		check(eu::split_next(rest, ',') == eu::StringView("c"), "split 4");
		check(rest.empty(), "resto vacío");
		check(eu::split_next(rest, ',') == eu::StringView(""), "split de vacío");
	}

	// --- equal_ci ------------------------------------------------------------
	check(eu::equal_ci(eu::StringView("VidaS"), eu::StringView("vidas")), "equal_ci");
	check(!eu::equal_ci(eu::StringView("a"), eu::StringView("ab")), "equal_ci tamaño");

	// --- parse_u32 / parse_s32 ----------------------------------------------
	{
		eng::u32 u = 0u;
		check(eu::parse_u32(eu::StringView("123"), u) && u == 123u, "parse_u32 123");
		check(eu::parse_u32(eu::StringView("0"), u) && u == 0u, "parse_u32 0");
		check(eu::parse_u32(eu::StringView("4294967295"), u) && u == 0xffffffffu,
		      "parse_u32 max");
		check(!eu::parse_u32(eu::StringView("4294967296"), u), "parse_u32 overflow");
		check(!eu::parse_u32(eu::StringView("12x"), u), "parse_u32 no dígito");
		check(!eu::parse_u32(eu::StringView(""), u), "parse_u32 vacío");

		eng::s32 s = 0;
		check(eu::parse_s32(eu::StringView("-42"), s) && s == -42, "parse_s32 -42");
		check(eu::parse_s32(eu::StringView("2147483647"), s) && s == 2147483647, "parse_s32 max");
		check(eu::parse_s32(eu::StringView("-2147483648"), s) && s == -2147483647 - 1,
		      "parse_s32 min");
		check(!eu::parse_s32(eu::StringView("2147483648"), s), "parse_s32 overflow +");
		check(!eu::parse_s32(eu::StringView("-2147483649"), s), "parse_s32 overflow -");
	}

	// --- to_chars (sin division) --------------------------------------------
	{
		eu::StaticString<16> s;
		check(eu::to_chars_u32(s, 0u) && s.view() == eu::StringView("0"), "to_chars 0");
		s.clear();
		check(eu::to_chars_u32(s, 12345u) && s.view() == eu::StringView("12345"), "to_chars 12345");
		s.clear();
		check(eu::to_chars_u32(s, 0xffffffffu) && s.view() == eu::StringView("4294967295"),
		      "to_chars max");
		s.clear();
		check(eu::to_chars_s32(s, -7) && s.view() == eu::StringView("-7"), "to_chars -7");
		s.clear();
		check(eu::to_chars_s32(s, -2147483647 - 1) && s.view() == eu::StringView("-2147483648"),
		      "to_chars INT_MIN");
		s.clear();
		check(eu::to_chars_u32(s, 1000000u) && s.view() == eu::StringView("1000000"),
		      "to_chars 1000000");

		eu::StaticString<3> small; // capacidad 2
		check(!eu::to_chars_u32(small, 12345u), "to_chars rechaza si no cabe");
	}

	// --- TextLine (operator<<) ----------------------------------------------
	{
		eu::TextLine<32> line;
		line << "F " << 123u << " 0x" << eu::hex(0xabu, 3u);
		check(line.view() == eu::StringView("F 123 0x0ab"), "TextLine cadena");
		check(!line.truncated(), "TextLine sin truncar");

		eu::TextLine<4> small; // capacidad 3
		small << "abc" << 'd' << 'e';
		check(small.truncated(), "TextLine trunca");
		check(small.view() == eu::StringView("abc"), "TextLine conserva lo que cabe");

		eu::TextLine<48> controls;
		controls << "S " << eu::dec(7u, 3u) << " V " << eu::fixed(Q12(6144), 2u)
			 << " P " << eu::scaled(5960u, 2u);
		check(controls.view() == eu::StringView("S 007 V 1.50 P 59.60"), "TextLine dec/fixed/scaled");
	}

	// --- to_chars_hex --------------------------------------------------------
	{
		eu::StaticString<16> s;
		check(eu::to_chars_hex(s, 0u) && s.view() == eu::StringView("0"), "hex 0");
		s.clear();
		check(eu::to_chars_hex(s, 0x1234u) && s.view() == eu::StringView("1234"), "hex 1234");
		s.clear();
		check(eu::to_chars_hex(s, 0xabu, 4u) && s.view() == eu::StringView("00ab"),
		      "hex relleno 4");
		s.clear();
		check(eu::to_chars_hex(s, 0xffffffffu) && s.view() == eu::StringView("ffffffff"),
		      "hex max");
		eu::StaticString<3> small; // capacidad 2
		check(!eu::to_chars_hex(small, 0x1234u), "hex rechaza si no cabe");
	}

	// --- to_chars/to_chars_scaled genéricos ----------------------------------
	{
		eu::StaticString<16> s;

		// Enteros de cualquier ancho por el mismo template (u16/s16/u8/s8).
		check(eu::to_chars(s, eng::u16(1234)) && s.view() == eu::StringView("1234"),
		      "to_chars u16");
		s.clear();
		check(eu::to_chars(s, eng::s16(-7)) && s.view() == eu::StringView("-7"),
		      "to_chars s16");
		s.clear();
		check(eu::to_chars(s, eng::u8(255)) && s.view() == eu::StringView("255"),
		      "to_chars u8");
		s.clear();
		check(eu::to_chars(s, eng::s8(-128)) && s.view() == eu::StringView("-128"),
		      "to_chars s8");

		// Escalado: el valor ya va ×10^k y el punto se inserta sin división.
		s.clear();
		check(eu::to_chars_scaled(s, 5960u, 2u) && s.view() == eu::StringView("59.60"),
		      "scaled 5960/2");
		s.clear();
		check(eu::to_chars_scaled(s, 5u, 2u) && s.view() == eu::StringView("0.05"),
		      "scaled 5/2");
		s.clear();
		check(eu::to_chars_scaled(s, 0u, 2u) && s.view() == eu::StringView("0.00"),
		      "scaled 0/2");
		s.clear();
		check(eu::to_chars_scaled(s, 5u, 4u) && s.view() == eu::StringView("0.0005"),
		      "scaled 5/4");
		s.clear();
		check(eu::to_chars_scaled(s, eng::u16(123), 0u) && s.view() == eu::StringView("123"),
		      "scaled entero (k=0)");
		s.clear();
		check(eu::to_chars_scaled(s, -5960, 2u) && s.view() == eu::StringView("-59.60"),
		      "scaled s32");
		s.clear();
		check(eu::to_chars_scaled(s, -1, 2u) && s.view() == eu::StringView("-0.01"),
		      "scaled s32 -1/2");

		// Nada a medias: si no cabe (signo incluido), el buffer queda como estaba.
		eu::StaticString<6> tiny; // capacidad 5
		check(!eu::to_chars_scaled(tiny, -12345, 0u), "scaled rechaza si no cabe");
		check(tiny.empty(), "scaled no escribe el signo a medias");
		check(!eu::to_chars_scaled(tiny, 123456u, 0u), "scaled rechaza 6 dígitos");
		check(tiny.empty(), "scaled sin escritura parcial");
	}

	// --- parse genérico por ancho --------------------------------------------
	{
		eng::u8 u8 = 0u;
		check(eu::parse(eu::StringView("255"), u8) && u8 == 255u, "parse u8 max");
		check(!eu::parse(eu::StringView("256"), u8), "parse u8 overflow");
		check(!eu::parse(eu::StringView("-1"), u8), "parse u8 negativo");

		eng::s8 s8 = 0;
		check(eu::parse(eu::StringView("-128"), s8) && s8 == -128, "parse s8 min");
		check(!eu::parse(eu::StringView("-129"), s8), "parse s8 overflow -");

		eng::u16 u16 = 0u;
		check(eu::parse(eu::StringView("65535"), u16) && u16 == 65535u, "parse u16 max");
		check(!eu::parse(eu::StringView("65536"), u16), "parse u16 overflow");

		eng::s16 s16 = 0;
		check(eu::parse(eu::StringView("-32768"), s16) && s16 == -32768, "parse s16 min");
		check(!eu::parse(eu::StringView("32768"), s16), "parse s16 overflow +");
		check(!eu::parse(eu::StringView("-32769"), s16), "parse s16 overflow -");
		check(!eu::parse(eu::StringView("12x"), s16), "parse s16 no dígito");
	}

	// --- Fixed: escalar fraccionario por el MISMO template -------------------
	{
		eu::StaticString<16> s;

		// Escritura con punto (redondeo al más cercano sobre la magnitud).
		check(eu::to_chars(s, Q12 {4096}, 2u) && s.view() == eu::StringView("1.00"),
		      "fixed 1.0 k=2");
		s.clear();
		check(eu::to_chars(s, Q12 {6144}, 2u) && s.view() == eu::StringView("1.50"),
		      "fixed 1.5 k=2");
		s.clear();
		check(eu::to_chars(s, Q12 {-8192}, 2u) && s.view() == eu::StringView("-2.00"),
		      "fixed -2.0 k=2");
		s.clear();
		check(eu::to_chars(s, Q12 {4096}, 0u) && s.view() == eu::StringView("1"),
		      "fixed 1.0 k=0");
		s.clear();
		check(eu::to_chars(s, Q12 {5}, 4u) && s.view() == eu::StringView("0.0012"),
		      "fixed 5/4096 k=4");
		s.clear();
		check(eu::to_chars(s, Q12 {409}, 2u) && s.view() == eu::StringView("0.10"),
		      "fixed 0.09985 k=2 redondea a 0.10");
		s.clear();
		check(eu::to_chars(s, Q12 {-1}, 2u) && s.view() == eu::StringView("0.00"),
		      "fixed -1/4096 k=2 (redondea a 0, sin cero negativo)");
		s.clear();
		check(eu::to_chars(s, Q12 {-21}, 2u) && s.view() == eu::StringView("-0.01"),
		      "fixed -21/4096 k=2 (signo conservado si no redondea a 0)");
		s.clear();
		check(eu::to_chars(s, Q12 {32767}, 4u) && s.view() == eu::StringView("7.9998"),
		      "fixed max k=4");

		// Dos escalares más del mismo template: otro exponente (4.8) y entero (q0).
		s.clear();
		check(eu::to_chars(s, Q8 {384}, 2u) && s.view() == eu::StringView("1.50"),
		      "fixed 4.8: 1.5 k=2");
		s.clear();
		check(eu::to_chars(s, Q0 {123}, 2u) && s.view() == eu::StringView("123.00"),
		      "fixed q0 entero k=2");

		// k fuera del contrato (> kDecimalDigitsMax) no inventa dígitos.
		s.clear();
		check(!eu::to_chars(s, Q12 {4096}, 5u) && s.empty(), "fixed k=5 rechazado");

		// Lectura: cuantiza al ulp más cercano y comprueba el rango de Q12.
		Q12 v {};
		check(eu::parse(eu::StringView("1.5"), v, 2u) && v.v == 6144, "parse q12 1.5");
		check(eu::parse(eu::StringView("0.1"), v, 2u) && v.v == 410,
		      "parse q12 0.1 (410/4096 = 0.1001)");
		check(eu::parse(eu::StringView("-2.25"), v, 2u) && v.v == -9216, "parse q12 -2.25");
		check(eu::parse(eu::StringView("-8.0"), v, 2u) && v.v == -32768,
		      "parse q12 -8.0 (min exacto)");
		check(!eu::parse(eu::StringView("8.0"), v, 2u), "parse q12 8.0 fuera de rango");
		check(!eu::parse(eu::StringView("300"), v, 2u), "parse q12 300 fuera de rango");
		check(!eu::parse(eu::StringView("1.234"), v, 2u), "parse q12 decimales de más");
		check(!eu::parse(eu::StringView(".5"), v, 2u), "parse q12 parte entera obligatoria");
		check(!eu::parse(eu::StringView("1."), v, 2u), "parse q12 punto sin decimales");
		check(!eu::parse(eu::StringView("1.2.3"), v, 2u), "parse q12 doble punto");
		check(!eu::parse(eu::StringView(""), v, 2u), "parse q12 vacío");
		check(!eu::parse(eu::StringView("-"), v, 2u), "parse q12 solo signo");
		check(!eu::parse(eu::StringView("1.5"), v, 5u), "parse q12 k=5 rechazado");

		// Round-trip EXACTO a 4 decimales: la cota de error (< 1 ulp de 4.12) lo garantiza.
		bool exacto = true;
		for (int raw = -32768; raw <= 32767; ++raw) {
			eu::StaticString<16> t;
			Q12 back {};
			if (!eu::to_chars(t, Q12 {static_cast<eng::s16>(raw)}, 4u) ||
			    !eu::parse(t.view(), back, 4u) || back.v != static_cast<eng::s16>(raw)) {
				exacto = false;
				std::printf("[FAIL] round-trip q12 k=4 raw=%d → '%s'\n", raw, t.c_str());
				break;
			}
		}
		check(exacto, "round-trip q12 k=4 exacto en todo el rango");

		// 4.8 también es exacto a 4 decimales (misma cota: < 0.51 ulp).
		bool exacto8 = true;
		for (int raw = -32768; raw <= 32767; ++raw) {
			eu::StaticString<16> t;
			Q8 back {};
			if (!eu::to_chars(t, Q8 {static_cast<eng::s16>(raw)}, 4u) ||
			    !eu::parse(t.view(), back, 4u) || back.v != static_cast<eng::s16>(raw)) {
				exacto8 = false;
				std::printf("[FAIL] round-trip q8 k=4 raw=%d → '%s'\n", raw, t.c_str());
				break;
			}
		}
		check(exacto8, "round-trip q8 k=4 exacto en todo el rango");
	}

	// --- join ----------------------------------------------------------------
	{
		const eu::StringView parts[3] = {eu::StringView("a"), eu::StringView("bb"),
						 eu::StringView("c")};
		eu::StaticString<16> out;
		check(eu::join(out, eng::Span<const eu::StringView> {parts, 3}, '-') &&
			      out.view() == eu::StringView("a-bb-c"),
		      "join");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: texto validado.\n");
	return 0;
}
