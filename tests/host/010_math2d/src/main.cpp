// Test host de eng::core::math2d (port de lib2d, fixed-point 4.12).
// Valida identidad, traslación, escala, rotación (90/180°) y la tabla de seno.
#include <eng/core/math2d.hpp>

#include <cstdio>

using namespace eng::math2d;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}
static void near(int got, int want, int tol, const char* msg) {
	if (got < want - tol || got > want + tol) {
		std::printf("  [FAIL] %s (got %d, want ~%d +-%d)\n", msg, got, want, tol);
		++failures;
	}
}

int main() {
	Mat2x2 m;
	Vec2 out[2];

	// Identidad.
	load_identity(m);
	check(m.m00 == 4096 && m.m01 == 0 && m.x == 0, "identidad (m00)");
	check(m.m10 == 0 && m.m11 == 4096 && m.y == 0, "identidad (m11)");

	// Transform con identidad no cambia los puntos.
	const Vec2 in[2] = { {100, 200}, {-50, 25} };
	transform(m, out, in, 2);
	check(out[0].x == 100 && out[0].y == 200, "transform identidad p0");
	check(out[1].x == -50 && out[1].y == 25, "transform identidad p1");

	// Traslación.
	translate(m, 10, -20);
	transform(m, out, in, 1);
	check(out[0].x == 110 && out[0].y == 180, "translate");

	// Escala 0.5 (2048 en 4.12).
	load_identity(m);
	scale(m, 2048, 2048);
	transform(m, out, in, 1);
	check(out[0].x == 50 && out[0].y == 100, "scale 0.5");

	// Rotación 90° (índice 1024): en coordenadas de pantalla (x,y) -> (y,-x).
	load_identity(m);
	rotate(m, 1024);
	{
		const Vec2 p = {100, 0};
		transform(m, out, &p, 1);
		near(out[0].x, 0, 1, "rot90 (100,0).x");
		near(out[0].y, -100, 1, "rot90 (100,0).y");
	}
	{
		const Vec2 p = {0, 100};
		transform(m, out, &p, 1);
		near(out[0].x, 100, 1, "rot90 (0,100).x");
		near(out[0].y, 0, 1, "rot90 (0,100).y");
	}

	// Rotación 180° (índice 2048): niega.
	load_identity(m);
	rotate(m, 2048);
	{
		const Vec2 p = {100, 50};
		transform(m, out, &p, 1);
		near(out[0].x, -100, 1, "rot180 x");
		near(out[0].y, -50, 1, "rot180 y");
	}

	// Tabla de seno (4.12).
	check(sin_q12(0) == 0, "sin 0");
	near(sin_q12(1024), 4096, 1, "sin pi/2");
	near(sin_q12(2048), 0, 1, "sin pi");
	near(cos_q12(0), 4096, 1, "cos 0");

	// Flags de punto respecto a la ventana.
	const Rect win { 0, 0, 320, 256 };
	check(point_flags({10, 10}, win) == 0, "punto dentro");
	check(point_flags({-1, 10}, win) == 1, "punto izquierda");
	check(point_flags({320, 10}, win) == 2, "punto derecha");
	check(point_flags({10, -1}, win) == 4, "punto arriba");
	check(point_flags({10, 256}, win) == 8, "punto abajo");

	// Recorte de línea (Liang-Barsky).
	{
		Vec2 a {-100, 100}, b {400, 100};
		check(clip_line(win, a, b), "clip_line cruza -> true");
		check(a.x >= 0 && a.x < 320 && b.x > 0 && b.x <= 320, "clip_line extremos dentro");
	}
	{
		Vec2 a {-100, -100}, b {-50, -50};
		check(!clip_line(win, a, b), "clip_line fuera -> false");
	}
	{
		Vec2 a {50, 50}, b {100, 100};
		check(clip_line(win, a, b) && a.x == 50 && a.y == 50 && b.x == 100 && b.y == 100,
			"clip_line dentro sin cambios");
	}

	// Recorte de polígono (Sutherland-Hodgman): un cuadrado que envuelve la ventana
	// debe quedar dentro de ella.
	{
		Vec2 poly[8] = { {-10, -10}, {330, -10}, {330, 266}, {-10, 266} };
		Vec2 tmpb[8];
		const eng::u32 n = clip_polygon(win, poly, tmpb, 4, PF_LEFT | PF_TOP | PF_RIGHT | PF_BOTTOM);
		check(n >= 3, "clip_polygon devuelve >= 3 vertices");
		bool all_in = true;
		for (eng::u32 i = 0; i < n; ++i) {
			if (poly[i].x < 0 || poly[i].x > 320 || poly[i].y < 0 || poly[i].y > 256) all_in = false;
		}
		check(all_in, "clip_polygon vertices dentro");
	}

	if (failures == 0) {
		std::printf("OK: math2d (lib2d 4.12) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
