// ============================================================================
// Test HOST-044: BigBufferScroll (estrategia de scroll trivial "escena ya dibujada")
// ============================================================================
//
// Valida `eng::field::BigBufferScroll` (scroll_engine.hpp): la estrategia base que
// solo lleva el offset de cámara acotado (sin anillo ni banda de staging que
// rellenar). Contraste con `ScrollEngine` (corkscrew/XYLimited, que emite blits).

#include <cstdio>

#include <eng/field/scroll_engine.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

} // namespace

int main() {
	using eng::field::BigBufferScroll;

	// Rango acotado: clampa en los extremos.
	BigBufferScroll s {};
	s.min_pos = 0;
	s.max_pos = 80;
	check(s.position == 0, "posicion inicial");
	check(s.step(30) == 30, "avanza 30");
	check(s.step(80) == 80, "clampa en max");
	check(s.step(-200) == 0, "clampa en min");
	check(s.step(50) == 50, "vuelve a avanzar");

	// Sin clamp (anillo): no recorta.
	BigBufferScroll ring {};
	ring.min_pos = 0; ring.max_pos = 80; ring.clamp = false;
	check(ring.step(-10) == -10, "sin clamp: negativo");
	check(ring.step(200) == 190, "sin clamp: por encima del max");

	// reset.
	ring.reset(42);
	check(ring.position == 42, "reset");

	// Sin sobrecoste: es un POD trivial.
	static_assert(sizeof(BigBufferScroll) <= 4u * sizeof(eng::s32) + 4u, "tamano minimo");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: BigBufferScroll (offset acotado/ring) validado.\n");
	return 0;
}
