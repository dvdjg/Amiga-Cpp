// ============================================================================
// Test HOST-470: politica de paneo de ventana (`eng::graphics/pan_window.hpp`)
// ============================================================================
//
// Valida la decision Full/Fake de los fotogramas intermedios (tecnica de
// `docs/reference/amiga/techniques/camera-pan-fake-frames.md`):
//
//   1) Primer paso siempre Full y ancla la base; fakes con el offset EXACTO.
//   2) Margen: un delta mayor que el margen fuerza Full (re-ancla la base).
//   3) Racha: `max_consecutive_fakes` limita los fakes seguidos.
//   4) Ejes independientes (x/y) y offsets negativos.
//   5) `pan_window_force_full` y el invariante `pan_window_offset_fits`.
//   6) Convencion de scroll del engine (`fine_scroll_coarse`/`fine_delay`):
//      la descomposicion de una ventana (puntero coarse + fino) es la que usa
//      la demo 080 para aplicar el offset por los registros del display.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/470_pan_window

#include <cstdio>

#include <eng/graphics/pan_window.hpp>
#include <eng/graphics/playfield_scroll.hpp>

namespace {

using eng::graphics::PanWindowPolicy;
using eng::graphics::PanWindowState;
using eng::graphics::PanWindowStep;
using eng::graphics::pan_window_force_full;
using eng::graphics::pan_window_offset_fits;
using eng::graphics::pan_window_step;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_full_fake_basics() {
	const PanWindowPolicy policy {16, 8, 4}; // racha holgada: aqui se prueba el offset, no el limite
	PanWindowState st {};
	// Primer paso: Full, offset 0, base anclada a 100.
	PanWindowStep s = pan_window_step(st, 100, 0, policy);
	check(s.full && s.offset_x == 0 && s.offset_y == 0, "primer paso = Full en el origen");
	// Paneo +5 px: Fake con el offset exacto.
	s = pan_window_step(st, 105, 0, policy);
	check(!s.full && s.offset_x == 5 && s.offset_y == 0, "fake con offset exacto (+5)");
	check(pan_window_offset_fits(s, policy), "el offset del fake cabe en el margen");
	// Paneo -4 px respecto a la base: Fake negativo.
	s = pan_window_step(st, 96, 0, policy);
	check(!s.full && s.offset_x == -4, "fake con offset negativo (-4)");
	// Y independiente de X.
	s = pan_window_step(st, 103, 3, policy);
	check(!s.full && s.offset_x == 3 && s.offset_y == 3, "ejes x/y independientes");
}

void test_margin_forces_full() {
	const PanWindowPolicy policy {8, 4, 2};
	PanWindowState st {};
	(void)pan_window_step(st, 0, 0, policy); // Full, base = 0
	PanWindowStep s = pan_window_step(st, 9, 0, policy);
	check(s.full && s.offset_x == 0, "delta 9 > margen 8: Full (no fake)");
	s = pan_window_step(st, 9, -5, policy);
	check(s.full, "delta y 5 > margen 4: Full");
	// La base quedo anclada al re-render: el siguiente delta pequeno es Fake.
	s = pan_window_step(st, 12, -5, policy);
	check(!s.full && s.offset_x == 3, "tras el Full, la base se re-ancla");
}

void test_streak_limit() {
	const PanWindowPolicy policy {16, 16, 2};
	PanWindowState st {};
	(void)pan_window_step(st, 0, 0, policy);       // Full, base 0
	PanWindowStep s = pan_window_step(st, 1, 0, policy);
	check(!s.full, "fake 1");
	s = pan_window_step(st, 2, 0, policy);
	check(!s.full && s.offset_x == 2, "fake 2");
	s = pan_window_step(st, 3, 0, policy);
	check(s.full, "racha cumplida (2 fakes): Full aunque el delta quepa");
	s = pan_window_step(st, 4, 0, policy);
	check(!s.full && s.offset_x == 1, "la racha se reinicia tras el Full");
}

void test_force_full_and_fits() {
	const PanWindowPolicy policy {16, 16, 3};
	PanWindowState st {};
	(void)pan_window_step(st, 40, 40, policy);
	PanWindowStep s = pan_window_step(st, 41, 40, policy);
	check(!s.full, "fake antes del forzado");
	pan_window_force_full(st);
	s = pan_window_step(st, 42, 40, policy);
	check(s.full && s.offset_x == 0, "force_full fuerza el siguiente paso a Full");
	// Invariante: todo Fake producido cabe en los margenes (barrido de deltas).
	PanWindowState sweep {};
	(void)pan_window_step(sweep, 0, 0, policy);
	eng::s32 pan = 0;
	bool any_fake = false;
	for (int i = 0; i < 512; ++i) {
		pan += (i % 7) - 3; // deltas -3..+3
		const PanWindowStep p = pan_window_step(sweep, pan, 0, policy);
		if (!p.full) {
			any_fake = true;
			check(pan_window_offset_fits(p, policy), "invariante: offset dentro del margen");
		}
	}
	check(any_fake, "el barrido produce fakes");
	// Margen 0: cualquier movimiento exige Full; delta 0 da un fake identidad.
	const PanWindowPolicy zero {0, 0, 4};
	PanWindowState zst {};
	(void)pan_window_step(zst, 0, 0, zero);
	const PanWindowStep zid = pan_window_step(zst, 0, 0, zero);
	check(!zid.full && zid.offset_x == 0, "margen 0: delta 0 = fake identidad");
	check(pan_window_step(zst, 1, 0, zero).full, "margen 0: delta 1 = Full");
}

void test_scroll_convention() {
	// La misma convencion que usa la demo 080 al parchear BPLxPT/BPLCON1.
	// ventana S en [1..400] px: puntero coarse alineado a 16 y fino = retardo.
	for (eng::u16 s = 1; s <= 400u; ++s) {
		const eng::u16 coarse = eng::graphics::fine_scroll_coarse(s);
		const eng::u16 fine = eng::graphics::fine_delay(s);
		check((coarse & 15u) == 0u, "coarse alineado a 16 px (puntero word-aligned)");
		check(coarse <= s - 1u, "coarse no pasa del inicio de la ventana");
		check(fine == ((16u - (s & 15u)) & 15u), "fine = retardo de BPLCON1");
		if ((s & 15u) == 0u) {
			check(fine == 0u, "S multiplo de 16: sin retardo");
		}
	}
	// Casos concretos de la demo (centro S=16 y un fake S=17).
	check(eng::graphics::fine_scroll_coarse(16u) == 0u &&
		  eng::graphics::fine_delay(16u) == 0u,
	      "S=16 (centro): puntero al inicio, sin fino");
	check(eng::graphics::fine_scroll_coarse(17u) == 16u &&
		  eng::graphics::fine_delay(17u) == 15u,
	      "S=17 (fake +1): coarse una palabra, fino 15");
	check(eng::graphics::fine_scroll_coarse(31u) == 16u &&
		  eng::graphics::fine_delay(31u) == 1u,
	      "S=31 (margen maximo): mismo coarse, fino 1");
	// El regimen util del paneo (S = 16 + offset, offset -15..+15) no sale del
	// patron representable (nunca S=0, que `fine_scroll_coarse` mapea raro).
	check(eng::graphics::fine_scroll_coarse(1u) == 0u, "S=1: coarse 0 (guarda al inicio del FB)");
}

} // namespace

int main() {
	std::printf("Politica de paneo de ventana (pan_window):\n");
	test_full_fake_basics();
	test_margin_forces_full();
	test_streak_limit();
	test_force_full_and_fits();
	test_scroll_convention();
	if (g_fail != 0u) {
		std::printf("FALLOS: %u\n", g_fail);
		return 1;
	}
	std::printf("OK: pan window (full/fake, margenes, racha, forzado y convencion de scroll)\n");
	return 0;
}
