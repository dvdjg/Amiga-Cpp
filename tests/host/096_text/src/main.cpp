// ============================================================================
// Test HOST-096: texto (eng::util::text).
// ============================================================================
//
// Respalda `eng/core/util/text.hpp`: trim, split_next, equal_ci, parse de enteros,
// conversion a decimal (sin division) y join.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/096_text

#include <cstdio>

#include <eng/core/util/text.hpp>

namespace eu = eng::util;

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
