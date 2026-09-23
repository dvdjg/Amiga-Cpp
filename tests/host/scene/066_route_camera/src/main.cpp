// ============================================================================
// Test HOST-066: route_camera (cámara de ruta por fases) sobre tipos tipados.
// ============================================================================
//
// Respalda `eng/scene/route_camera.hpp`: la posición es `Vec<2, Fixed<s16,0>>` y los
// offsets de la circunferencia se generan con `eng::SineTable` (no con tablas a mano).
// Se comprueban las INVARIANTES del contrato, sin float:
//
//   - fases: posiciones exactas en los inicios de fase (horizontal/vertical/diagonal/
//     circular/senoidal), derivadas de la configuración por defecto;
//   - círculo: los puntos caen sobre la circunferencia de radio `radius_scale`
//     (|d² − r²| acotado por el redondeo entero del offset);
//   - lineal: avance de 1 px/frame en x en las fases suaves;
//   - espejo: `mirror_x` refleja x como `max_x + min_x − x`;
//   - salto: tras `jump_start_frames` la cámara se mantiene dentro de los límites.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/066_route_camera

#include <cstdio>

#include <eng/scene/route_camera.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void near(int got, int want, int tol, const char* what) {
	const int d = got - want;
	if (d < -tol || d > tol) {
		std::printf("[FAIL] %s: got %d, want %d +-%d\n", what, got, want, tol);
		++g_fail;
	}
}

/// Cámara nueva en la posición inicial de la ruta.
eng::scene::RouteCamera fresh() {
	eng::scene::RouteCamera cam {};
	cam.set(1, cam.center_y);
	return cam;
}

/// Posiciones exactas al inicio de cada fase (configuración por defecto).
void check_phase_starts() {
	struct Case {
		eng::u32 frame;
		eng::s16 x;
		eng::s16 y;
		const char* what;
	};
	const Case cases[] = {
		{0u, 1, 128, "inicio fase horizontal"},
		{192u, 160, 0, "inicio fase vertical"},
		{384u, 1, 0, "inicio fase diagonal"},
		{576u, 256, 128, "inicio fase circular"},   // 0.5 vuelta de tabla = +radio en x
		{1088u, 1, 128, "inicio fase senoidal"},
	};
	for (const Case& c : cases) {
		eng::scene::RouteCamera cam = fresh();
		for (eng::u32 f = 0; f <= c.frame; ++f) cam.advance(f);
		near(cam.x(), c.x, 0, c.what);
		near(cam.y(), c.y, 0, c.what);
	}
}

/// Las fases lineales avanzan 1 px/frame (movimiento suave).
void check_linear_speed() {
	eng::scene::RouteCamera cam = fresh();
	eng::u16 px = cam.x();
	eng::u16 py = cam.y();
	for (eng::u32 f = 1; f < 1600u; ++f) {
		cam.advance(f);
		const int dx = static_cast<int>(cam.x()) - static_cast<int>(px);
		const int dy = static_cast<int>(cam.y()) - static_cast<int>(py);
		if (f < 192u) {
			check(dx == 1, "horizontal: +1 px/frame en x");
		} else if (f > 192u && f < 384u) {
			check(dy == 1, "vertical: +1 px/frame en y");
		} else if (f > 384u && f < 576u) {
			check(dx == 1 && dy == 1, "diagonal: +1 px/frame en ambos ejes");
		} else if (f > 1088u) {
			check(dx == 1, "senoidal: +1 px/frame en x");
		}
		px = cam.x();
		py = cam.y();
	}
}

/// Círculo: los puntos están sobre la circunferencia de radio `radius_scale`.
void check_circle_radius() {
	eng::scene::RouteCamera cam = fresh();
	const int r = cam.radius_scale;
	const int r2 = r * r;
	int worst = 0;
	for (eng::u32 f = 0; f < 1088u; ++f) {
		cam.advance(f);
		if (f < 576u) continue;
		const int dx = static_cast<int>(cam.x()) - cam.center_x;
		const int dy = static_cast<int>(cam.y()) - cam.center_y;
		const int d2 = dx * dx + dy * dy;
		const int e = d2 - r2;
		const int ae = e < 0 ? -e : e;
		if (ae > worst) worst = ae;
	}
	// Cota del redondeo: cada offset entero se desvía <= 0.5 px del ideal, así que la
	// desviación de d² es ~2·r·0.5·2 = 2r (con margen).
	near(worst, 0, 4 * r, "círculo: puntos sobre el radio");
}

/// `mirror_x` refleja la posición horizontal (`max_x + min_x - route_x`, en `u16`).
void check_mirror() {
	eng::scene::RouteCamera plain = fresh();
	eng::scene::RouteCamera mirror = fresh();
	mirror.mirror_x = true;
	for (eng::u32 f = 0; f < 1600u; ++f) {
		plain.advance(f);
		mirror.advance(f);
		const eng::u16 expected = static_cast<eng::u16>(mirror.max_x + mirror.min_x - plain.x());
		near(mirror.x(), expected, 0, "espejo x");
		near(mirror.y(), plain.y(), 0, "espejo no cambia y");
	}
}

/// Tras `jump_start_frames`, el modo de saltos se mantiene dentro de los límites.
void check_jump_bounds() {
	eng::scene::RouteCamera cam = fresh();
	for (eng::u32 f = 0; f < 4000u; ++f) {
		cam.advance(f);
		if (f < 1690u) continue; // deja pasar el primer salto (viene de la fase senoidal)
		check(cam.x() >= cam.min_x && cam.x() <= cam.max_x, "salto: x en límites");
		check(cam.y() >= cam.min_y && cam.y() <= cam.max_y, "salto: y en límites");
	}
}

/// `set` + `x()`/`y()` son coherentes.
void check_set() {
	eng::scene::RouteCamera cam = fresh();
	cam.set(123, 45);
	near(cam.x(), 123, 0, "set x");
	near(cam.y(), 45, 0, "set y");
}

} // namespace

int main() {
	std::printf("== HOST-066 route_camera ==\n");
	check_phase_starts();
	check_linear_speed();
	check_circle_radius();
	check_mirror();
	check_jump_bounds();
	check_set();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: route_camera (fases, radio, espejo y saltos) validado.\n");
	return 0;
}
