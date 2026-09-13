// ============================================================================
// Test HOST-024: offset de parallax "soft DPF" (fondo RoboCod)
// ============================================================================
//
// Valida `eng::field::parallax_pattern_offset_px` (engine/include/eng/field/
// xlimited.hpp), la parte pura del fondo con scroll propio: el plano de fondo
// comparte el scroll del FG (el display lee el bitmap desde `camx`), así que el
// CONTENIDO del patrón debe desfasarse `-camx*(div-1)/div` para que el fondo
// avance a `1/div`. La posición aparente en pantalla es `src + camx`.
//
//   bash tools/run-host-tests.sh tests/host/024_parallax_offset

#include <cstdio>

#include <eng/field/xlimited.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

using eng::s32;
using eng::u16;
using eng::field::parallax_pattern_offset_px;
using eng::field::fixed_bg_offset_px;

constexpr u16 kPeriod = 512;   // periodo horizontal del patrón (2 bytes = 16 px word)

} // namespace

int main() {
	// div=1: el fondo va igual que el FG -> sin desfase.
	for (s32 c = 0; c < 1000; c += 7) {
		check(parallax_pattern_offset_px(c, 1, kPeriod) == 0, "div=1 -> offset 0");
	}
	// div=0 se trata como 1 (config sin parallax).
	check(parallax_pattern_offset_px(123, 0, kPeriod) == 0, "div=0 -> offset 0");

	// div=2, paso de cámara 2 px: offset entero EXACTO y fondo a 1/2 sin saltos.
	{
		s32 prev = -1;
		for (s32 c = 0; c < 2 * kPeriod; c += 2) {
			const s32 src = parallax_pattern_offset_px(c, 2, kPeriod);
			check(src >= 0 && src < kPeriod, "div=2: offset en rango [0,period)");
			const s32 apparent = ((src + c) % kPeriod + kPeriod) % kPeriod;
			check(apparent == (c / 2) % kPeriod, "div=2: fondo a mitad de velocidad");
			if (prev >= 0) {
				check(apparent == (prev + 1) % kPeriod, "div=2: avanza 1 px por frame");
			}
			prev = apparent;
		}
	}

	// div=4, paso 4 px: fondo a 1/4.
	for (s32 c = 0; c < 4 * kPeriod; c += 4) {
		const s32 src = parallax_pattern_offset_px(c, 4, kPeriod);
		const s32 apparent = ((src + c) % kPeriod + kPeriod) % kPeriod;
		check(apparent == (c / 4) % kPeriod, "div=4: fondo a 1/4 de velocidad");
	}

	// El offset se reparte en word (2 bytes = 16 px) + shift (0..15) sin perder píxel.
	for (s32 c = 0; c < 2 * kPeriod; c += 2) {
		const s32 src = parallax_pattern_offset_px(c, 2, kPeriod);
		const s32 word = src / 16;
		const s32 bit = src % 16;
		check(word * 16 + bit == src, "split word+shift exacto");
	}

	// Fondo FIJO (`fixed_bg_offset_px`, desired_bg=0): src = -camx mod P. La posición
	// aparente en pantalla (src + camx) queda constante => imagen estática.
	for (s32 c = -300; c < 3000; c += 7) {
		const s32 src = fixed_bg_offset_px(c, kPeriod);
		check(src >= 0 && src < kPeriod, "fijo: offset en rango [0,period)");
		check((((src + c) % kPeriod) + kPeriod) % kPeriod == 0,
		      "fijo: cancela TODO el scroll (coarse+fine)");
	}

	// Compensación del Copper split: las dos mitades muestrean filas CONTIGUAS del
	// patrón -> `src_y` superior = 0 (filas [0,split)), inferior = split (filas
	// [split,viewport)); juntas cubren [0,viewport) sin hueco ni solape.
	constexpr s32 kViewport = 208;
	for (s32 D = 240; D <= 288; D += 16) {
		for (s32 d = 0; d < D; ++d) {
			const s32 split = D - d;
			if (split <= 0 || split >= kViewport) continue; // sin split
			const s32 upper_src0 = 0, upper_n = split;
			const s32 lower_src0 = split, lower_n = kViewport - split;
			check(upper_src0 + upper_n == lower_src0, "split: source contiguo arriba/abajo");
			check(lower_src0 + lower_n == kViewport, "split: cubre toda la ventana");
		}
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: parallax/fijo + compensacion de split (soft DPF) validados.\n");
	return 0;
}
