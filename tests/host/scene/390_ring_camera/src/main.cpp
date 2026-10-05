// ============================================================================
// Test HOST-390: cámara toroidal (`Camera2D::reset_ring`) — R6? §7 (cámara).
// ============================================================================
//
// Valida la cámara de un mundo que **envuelve**: `reset_ring(period_x, period_y, viewport)` mantiene
// la posición en `[0, period)` por **envoltura** (no recorte), en ambos ejes; el contraste es
// `reset(...)` (mundo acotado), que **recorta**. Es representación pura (sin hardware).
//
//   bash tools/run-host-tests.sh tests/host/scene/390_ring_camera

#include <cstdio>

#include <eng/scene/virtual_scene.hpp>

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
	std::printf("== HOST-390 ring_camera ==\n");

	// Mundo toroidal 320x256.
	eng::scene::Camera2D cam {};
	cam.reset_ring(320u, 256u, eng::Size2u {320u, 256u});
	check(cam.toroidal(), "reset_ring → toroidal");
	check(cam.x() == 0u && cam.y() == 0u, "posición inicial 0,0");

	// Envoltura X: 0 - 1 → 319; 319 + 1 → 0.
	cam.move_by(-1, 0);
	check(cam.x() == 319u, "wrap X negativo (0-1 → 319)");
	cam.move_by(1, 0);
	check(cam.x() == 0u, "wrap X positivo (319+1 → 0)");

	// Envoltura Y.
	cam.move_by(0, -1);
	check(cam.y() == 255u, "wrap Y negativo (0-1 → 255)");
	cam.move_by(0, 1);
	check(cam.y() == 0u, "wrap Y positivo (255+1 → 0)");

	// Pasos grandes también envuelven (módulo).
	cam.move_by(400, 0);
	check(cam.x() == 80u, "wrap X con paso > período (400 % 320)");
	cam.move_by(-100, 0);
	check(cam.x() == 300u, "wrap X negativo grande (80-100 → 300)");

	// Contraste: la cámara **acotada** recorta (no envuelve).
	eng::scene::Camera2D bounded {};
	bounded.reset(eng::scene::WorldRect {0u, 0u, 320u, 256u}, eng::Size2u {320u, 256u});
	check(!bounded.toroidal(), "reset → acotada");
	bounded.move_by(-1, 0);
	check(bounded.x() == 0u, "acotada: recorta en 0 (no envuelve)");
	bounded.move_by(1000, 0);
	check(bounded.x() == 0u, "acotada: sin margen de scroll, x queda en 0");

	// Una cámara con solo envoltura X deja Y acotada.
	eng::scene::Camera2D xonly {};
	xonly.reset_ring(320u, 0u, eng::Size2u {320u, 256u});
	check(xonly.toroidal(), "toroidal X-only");
	xonly.move_by(-1, 0);
	check(xonly.x() == 319u, "X envuelve");
	check(xonly.y() == 0u, "Y queda en 0 (recorte)");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: camara toroidal (reset_ring: envoltura X/Y) validada.\n");
	return 0;
}
