// ============================================================================
// Test HOST-022: utilidades de division por constante / potencia de dos.
// ============================================================================
//
// Respalda la API de `eng/core/fast_div.hpp` que usa el hot path del campo de
// tiles (`TileFieldController`): deteccion runtime de potencia de dos, log2,
// shift aritmetico (floor) y `fast_div<N>` (pow2 -> shift/mask; general ->
// cociente/resto exactos). En 68000 la division por una constante NO potencia
// de dos es un libcall (~150 ciclos), asi que estas utilidades son la base para
// evitarlo.
//
//   bash tools/run-host-tests.sh tests/host/022_fast_div

#include <cstdio>

#include <eng/core/fast_div.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
} // namespace

int main() {
	// is_pow2: potencias de dos vs no.
	check(eng::is_pow2(1u) && eng::is_pow2(2u) && eng::is_pow2(16u) &&
	      eng::is_pow2(256u) && eng::is_pow2(65536u), "is_pow2(true)");
	check(!eng::is_pow2(0u) && !eng::is_pow2(3u) && !eng::is_pow2(224u) &&
	      !eng::is_pow2(288u) && !eng::is_pow2(768u), "is_pow2(false)");

	// ilog2 (solo valido en potencias de dos).
	check(eng::ilog2(1u) == 0u && eng::ilog2(2u) == 1u && eng::ilog2(16u) == 4u &&
	      eng::ilog2(256u) == 8u && eng::ilog2(65536u) == 16u, "ilog2");

	// asr_floor == floor(v / 2^shift), tambien con negativos.
	for (int shift = 0; shift <= 8; ++shift) {
		const int d = 1 << shift;
		for (int v = -700; v <= 700; ++v) {
			const int want = (v >= 0) ? (v / d) : -static_cast<int>((-v + d - 1) / d);
			if (eng::asr_floor(v, static_cast<eng::u32>(shift)) != want) {
				std::printf("[FAIL] asr_floor(%d,%d)=%d want %d\n", v, shift,
				            (int)eng::asr_floor(v, static_cast<eng::u32>(shift)), want);
				++g_fail;
				break;
			}
		}
	}

	// fast_div<N>: exacto en un rango, pow2 y no-potencia-de-dos.
	auto check_div = [](auto tag, auto qfn, auto rfn, unsigned n) {
		for (unsigned v = 0; v < 5000u; ++v) {
			if (qfn(v) != v / n || rfn(v) != v % n) {
				std::printf("[FAIL] fast_div<%u>(%u)\n", n, v);
				++g_fail;
				return;
			}
		}
		(void)tag;
	};
	check_div(0, eng::fast_div<16>::q, eng::fast_div<16>::r, 16u);
	check_div(0, eng::fast_div<256>::q, eng::fast_div<256>::r, 256u);
	check_div(0, eng::fast_div<288>::q, eng::fast_div<288>::r, 288u);
	check_div(0, eng::fast_div<768>::q, eng::fast_div<768>::r, 768u);

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: fast_div/pow2 (is_pow2, ilog2, asr_floor, q/r) exactos.\n");
	return 0;
}
