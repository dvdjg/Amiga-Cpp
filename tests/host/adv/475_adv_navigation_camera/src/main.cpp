// ============================================================================
// Test HOST-475: A0 de aventura -- navegacion por walkboxes y camara de mundo
// ============================================================================
//
// Valida `engine/include/eng/adv/{navigation,camera}.hpp`:
//
//   1) `walkbox_route`: adyacencia derivada de la geometria (bordes que se tocan o quedan
//      dentro de tolerancia), ruta entre cajas, destino fuera de caja rechazado, cajas
//      desconectadas sin ruta y caso de una sola caja. Con `s16` y `float`.
//   2) `Camera<S>`: dead-zone (no mueve si el objetivo esta dentro), desplazamiento justo al
//      salir, clamp a los limites del mundo, paneo temporizado que termina exacto y es
//      monotono. Con `eng::math::Fixed<s16,12>` y `float`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/adv/475_adv_navigation_camera

#include <cstdio>

#include <eng/adv/camera.hpp>
#include <eng/adv/navigation.hpp>
#include <eng/core/math/fixed.hpp>

namespace {

using namespace eng::adv;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

template <class S>
eng::math::Vec<2, S> V(int x, int y) {
	return {{eng::math::scalar_traits<S>::from_int(x), eng::math::scalar_traits<S>::from_int(y)}};
}

template <class S>
Walkbox<S> box(int x0, int y0, int x1, int y1) {
	return Walkbox<S> {V<S>(x0, y0), V<S>(x1, y0), V<S>(x1, y1), V<S>(x0, y1), 0x0f, 0x00, 255};
}

template <class S>
void nav_checks(const char* label) {
	using D = walkbox_dist_t<S>;
	const Walkbox<S> boxes[3] {box<S>(0, 0, 64, 64), box<S>(64, 0, 128, 64), box<S>(128, 0, 192, 64)};
	const eng::Span<const Walkbox<S>> span {boxes};
	D best[3] {};
	eng::u16 prev[3] {};
	eng::u8 state[3] {};
	const RouteScratch<S> scratch {{best}, {prev}, {state}};
	eng::math::Vec<2, S> out[8] {};

	const eng::usize n = walkbox_route(span, V<S>(10, 10), V<S>(180, 50), static_cast<S>(0), scratch,
					   eng::Span<eng::math::Vec<2, S>> {out});
	check(n == 2, label);
	check(out[0].v[0] == static_cast<S>(64) && out[0].v[1] == static_cast<S>(10), "route: punto de paso en el borde");
	check(out[1].v[0] == static_cast<S>(180) && out[1].v[1] == static_cast<S>(50), "route: llega al destino");

	// Misma caja: un solo punto (el destino).
	D best1[3] {};
	eng::u16 prev1[3] {};
	eng::u8 state1[3] {};
	const RouteScratch<S> scratch1 {{best1}, {prev1}, {state1}};
	const eng::usize n1 = walkbox_route(span, V<S>(5, 5), V<S>(20, 20), static_cast<S>(0), scratch1,
					    eng::Span<eng::math::Vec<2, S>> {out});
	check(n1 == 1 && out[0].v[0] == static_cast<S>(20) && out[0].v[1] == static_cast<S>(20), "route: misma caja");

	// Destino fuera de toda caja: sin ruta (el juego decide el suelo mas cercano).
	D best2[3] {};
	eng::u16 prev2[3] {};
	eng::u8 state2[3] {};
	const RouteScratch<S> scratch2 {{best2}, {prev2}, {state2}};
	const eng::usize n2 = walkbox_route(span, V<S>(5, 5), V<S>(400, 400), static_cast<S>(0), scratch2,
					    eng::Span<eng::math::Vec<2, S>> {out});
	check(n2 == 0, "route: destino fuera de caja -> 0");

	// Tolerancia: cajas con hueco de 1 px se conectan con tol 2 y no con tol 0.
	const Walkbox<S> gap_boxes[2] {box<S>(0, 0, 63, 63), box<S>(65, 0, 128, 63)};
	const eng::Span<const Walkbox<S>> gap_span {gap_boxes};
	D best3[2] {};
	eng::u16 prev3[2] {};
	eng::u8 state3[2] {};
	const RouteScratch<S> scratch3 {{best3}, {prev3}, {state3}};
	check(walkbox_route(gap_span, V<S>(5, 5), V<S>(100, 50), static_cast<S>(0), scratch3,
			    eng::Span<eng::math::Vec<2, S>> {out}) == 0,
	      "route: hueco de 1 px con tolerancia 0 no conecta");
	D best4[2] {};
	eng::u16 prev4[2] {};
	eng::u8 state4[2] {};
	const RouteScratch<S> scratch4 {{best4}, {prev4}, {state4}};
	check(walkbox_route(gap_span, V<S>(5, 5), V<S>(100, 50), static_cast<S>(2), scratch4,
			    eng::Span<eng::math::Vec<2, S>> {out}) > 0,
	      "route: tolerancia 2 conecta el hueco de 1 px");
}

template <class S>
int to_i(S v) {
	return eng::math::scalar_traits<S>::to_int(v);
}

template <class S>
S fi(int v) {
	return eng::math::scalar_traits<S>::from_int(v);
}

template <class S>
void camera_checks(const char* label) {
	Camera<S> cam {};
	cam.place(V<S>(0, 0));
	cam.set_bounds(V<S>(0, 0), V<S>(1000, 500));
	cam.set_soft_bounds(V<S>(100, 100), V<S>(300, 200));
	cam.place(V<S>(50, 50));

	// Fuera de la zona muerta -> la camara se desplaza lo justo.
	cam.follow(V<S>(400, 150));
	cam.update();
	check(to_i(cam.pos.v[0]) == 150 && to_i(cam.pos.v[1]) == 50, label);

	// Dentro de la zona muerta (ya desplazada) -> no se mueve.
	cam.follow(V<S>(250, 150));
	cam.update();
	check(to_i(cam.pos.v[0]) == 150 && to_i(cam.pos.v[1]) == 50, "camera: dead-zone no mueve");

	// Clamp a los limites del mundo.
	cam.follow(V<S>(-500, -500));
	cam.update();
	check(to_i(cam.pos.v[0]) == 0 && to_i(cam.pos.v[1]) == 0, "camera: clamp al mundo");

	// Paneo: termina exacto y avanza de forma monotona.
	cam.stop_follow();
	cam.place(V<S>(50, 50));
	cam.pan_to(V<S>(250, 150), 8);
	check(cam.panning(), "camera: paneo activo");
	int last_x = 50;
	bool monotonic = true;
	for (int i = 0; i < 8; i++) {
		cam.update();
		const int x = to_i(cam.pos.v[0]);
		if (x < last_x) monotonic = false;
		last_x = x;
	}
	check(monotonic, "camera: paneo monotono");
	check(to_i(cam.pos.v[0]) == 250 && to_i(cam.pos.v[1]) == 150, "camera: paneo termina en destino");
	check(!cam.panning(), "camera: paneo finalizado");
}

} // namespace

int main() {
	std::printf("Adv A0 (navegacion + camara):\n");
	nav_checks<eng::s16>("navegacion s16");
	nav_checks<float>("navegacion float");
	using Fx = eng::math::Fixed<eng::s32, 16>;
	camera_checks<Fx>("camara Fixed");
	camera_checks<float>("camara float");
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: adv A0 (walkbox_route + Camera, s16/float/Fixed)\n");
	return 0;
}
