// ============================================================================
// Test HOST-465: ventana de fps del HUD (`eng/debug/prof.hpp`, `HudFpsWindow`).
// ============================================================================
//
// Respalda el campo de fps del ultimo segundo de los HUD de 1 bpp (demos 079/116,
// norma en `docs/tools/PROFILING_FROM_AGENT.md`): la ventana se cierra cuando el
// reloj de ciclos (0xB7E928 a 7,093790 MHz) acumula >= 1 s, y entonces emite
// `fps10` (fps x 10, un decimal). Pieza pura: los ciclos se INYECTAN (sin hardware).
//
// Casos: cierre exacto en 1 s, muestreo con un decimal, ventana que se pasa de
// tiempo, armado sin emision (host/arranque), tope de 50 Hz (guard de overflow) y
// wraparound de 32 bits del contador.
//
//   bash tools/run-host-tests.sh tests/host/debug/465_hud_fps_window

#include <cstdio>

#include <eng/debug/prof.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

using eng::debug::HudFpsWindow;
using eng::debug::prof_clock_hz;

/// Cierra una ventana desde el armado: inyecta `frames` frames en `cycles` ciclos y
/// devuelve true/false; `out` recibe fps10.
bool window(eng::u32 frames, eng::u32 cycles, eng::u32& out) {
	HudFpsWindow w;
	w.reset(0u, 1000u); // arma con reloj valido (como `init`)
	bool fired = false;
	// Un update por frame: el ultimo lleva el reloj final de la ventana.
	for (eng::u32 f = 1u; f < frames; ++f) {
		CHECK(!w.update(f, 1000u + 1u, out)); // ventana aun abierta
	}
	fired = w.update(frames, 1000u + cycles, out);
	return fired;
}

/// Cierre exacto: la ventana se cierra SOLO al cumplirse 1 s de reloj.
void test_close() {
	eng::u32 out = 0u;
	CHECK(!window(1u, prof_clock_hz - 1u, out)); // 1 ciclo antes: aun no
	CHECK(window(20u, prof_clock_hz, out));
	CHECK(out == 200u); // 20 fps -> 20.0
	CHECK(window(1u, prof_clock_hz, out) == true);
	CHECK(out == 10u); // 1 fps -> 1.0
}

/// Un decimal real: 18 frames en ~1 s -> 18.0; 25 frames en 1,5 s -> 16.6.
void test_decimals() {
	eng::u32 out = 0u;
	CHECK(window(18u, prof_clock_hz, out));
	CHECK(out == 180u);
	CHECK(window(25u, prof_clock_hz + prof_clock_hz / 2u, out));
	CHECK(out == 166u); // 250 / 1,5 = 166,6 -> truncado
	CHECK(window(9u, prof_clock_hz, out));
	CHECK(out == 90u); // 9.0 (un digito entero)
}

/// Armado diferido: sin reloj (host, `cycle_now == 0`) queda inactivo y el primer
/// reloj valido solo arma (no emite fps de la nada).
void test_arm() {
	HudFpsWindow w;
	eng::u32 out = 7u;
	w.reset(0u, 0u);
	CHECK(!w.update(10u, 0u, out));
	CHECK(!w.update(20u, 123u, out)); // arma aqui
	CHECK(out == 7u);                 // sin emision
	CHECK(!w.update(30u, 123u + prof_clock_hz - 1u, out));
	CHECK(w.update(40u, 123u + prof_clock_hz, out));
	CHECK(out == 200u); // 20 frames en 1 s
}

/// Tope de 50 Hz: una ventana "imposible" (mas frames que el refresco PAL) se
/// recorta para que `frames * kClockHz * 10` no desborde u32 (limite: 60).
void test_clamp() {
	eng::u32 out = 0u;
	CHECK(window(60u, prof_clock_hz, out));
	CHECK(out == 500u); // recortado a 50.0, no 600
}

/// Wraparound del contador de 32 bits: la resta u32 da el delta correcto.
void test_wrap() {
	HudFpsWindow w;
	eng::u32 out = 0u;
	const eng::u32 start = 0xFFFF0000u; // a punto de envolver
	w.reset(0u, start);
	CHECK(!w.update(5u, start + prof_clock_hz - 2u, out));
	CHECK(w.update(20u, start + prof_clock_hz, out)); // cruza 0xFFFFFFFF
	CHECK(out == 200u);
}

} // namespace

int main() {
	test_close();
	test_decimals();
	test_arm();
	test_clamp();
	test_wrap();
	if (g_fail == 0) {
		std::printf("HOST-465 hud_fps_window: OK\n");
		return 0;
	}
	std::printf("HOST-465 hud_fps_window: %d fallos\n", g_fail);
	return 1;
}
