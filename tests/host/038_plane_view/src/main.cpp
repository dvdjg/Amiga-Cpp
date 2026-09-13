// ============================================================================
// Test HOST-038: PlaneView (soft DPF, doble buffer de planos)
// ============================================================================
//
// Valida `eng::field::PlaneView` con punteros crudos (sin MemorySystem): el
// buffer delantero que publica el display, el trasero que escribe el Blit y la
// conmutación `flip()`. Es la pieza extraída del playfield de scroll.

#include <cstdio>

#include <eng/field/plane_view.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::field::PlaneView;
} // namespace

int main() {
	eng::u8 main_real[8] {}, main_front[8] {}, extra_real[8] {}, extra_front[8] {};

	// Sin doble buffer: display y escritura apuntan al mismo bloque.
	PlaneView pv {};
	pv.bind_single(main_real, main_front);
	check(!pv.double_buffered(), "single: sin doble buffer");
	check(pv.display_base() == main_real && pv.write_base() == main_front, "single: bases");
	pv.flip();
	check(pv.display_base() == main_real && pv.write_base() == main_front, "single: flip no cambia");

	// Con doble buffer: front = principal, back = extra (active=0).
	pv.bind_raw(main_real, main_front, extra_real, extra_front);
	check(pv.double_buffered(), "db: activo");
	check(pv.display_base() == main_real && pv.write_base() == extra_front,
	      "db active=0: front=main, back=extra");

	// flip: front = extra, back = principal.
	pv.flip();
	check(pv.display_base() == extra_real && pv.write_base() == main_front,
	      "db active=1: front=extra, back=main");
	// flip vuelve al estado inicial (toggle).
	pv.flip();
	check(pv.display_base() == main_real && pv.write_base() == extra_front, "db: toggle de vuelta");

	// bind_raw con extra nulo = sin doble buffer.
	PlaneView ps {};
	ps.bind_raw(main_real, main_front, nullptr, nullptr);
	check(!ps.double_buffered(), "bind_raw sin extra: single");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: PlaneView (front/back/flip del soft DPF) validado.\n");
	return 0;
}
