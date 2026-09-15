// Test host de eng::retro (lib2d 4.12): geometría 2D tipada sobre los tipos genéricos
// (Vec2 = Vec<2,q0>, Mat2x2 = Affine<2,q12,q0>), rotación con la tabla y recorte 2D.
#include <eng/retro/angles.hpp>
#include <eng/retro/lib2d.hpp>

#include <cstdio>

using eng::s16;
using eng::u32;
using namespace eng::retro;

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
	// Identidad: parte lineal 4.12 + traslación 0 (el `Matrix2D` del original, tipado).
	Mat2x2 m = Mat2x2::identity();
	check(m.m.m[0][0].v == 4096 && m.m.m[0][1].v == 0 && m.t.v[0].v == 0, "identidad (fila 0)");
	check(m.m.m[1][0].v == 0 && m.m.m[1][1].v == 4096 && m.t.v[1].v == 0, "identidad (fila 1)");

	// Transform con identidad no cambia los puntos (transform genérico del núcleo).
	const Vec2 in[2] = {v2(100, 200), v2(-50, 25)};
	{
		const Vec2 o0 = eng::math::transform(m, in[0]);
		const Vec2 o1 = eng::math::transform(m, in[1]);
		check(o0.v[0].v == 100 && o0.v[1].v == 200, "transform identidad p0");
		check(o1.v[0].v == -50 && o1.v[1].v == 25, "transform identidad p1");
	}

	// Traslación.
	translate(m, 10, -20);
	{
		const Vec2 o = eng::math::transform(m, in[0]);
		check(o.v[0].v == 110 && o.v[1].v == 180, "translate");
	}

	// Escala 0.5 (2048 en 4.12).
	m = Mat2x2::identity();
	scale(m, 2048, 2048);
	{
		const Vec2 o = eng::math::transform(m, in[0]);
		check(o.v[0].v == 50 && o.v[1].v == 100, "scale 0.5");
	}

	// Rotación 90° (índice 1024): en coordenadas de pantalla (x,y) -> (y,-x).
	m = Mat2x2::identity();
	rotate(m, 1024);
	{
		const Vec2 o = eng::math::transform(m, v2(100, 0));
		near(o.v[0].v, 0, 2, "rot90 (100,0).x");
		near(o.v[1].v, -100, 2, "rot90 (100,0).y");
	}
	{
		const Vec2 o = eng::math::transform(m, v2(0, 100));
		near(o.v[0].v, 100, 2, "rot90 (0,100).x");
		near(o.v[1].v, 0, 2, "rot90 (0,100).y");
	}

	// Rotación 180° (índice 2048): niega.
	m = Mat2x2::identity();
	rotate(m, 2048);
	{
		const Vec2 o = eng::math::transform(m, v2(100, 50));
		near(o.v[0].v, -100, 2, "rot180 x");
		near(o.v[1].v, -50, 2, "rot180 y");
	}

	// Tabla de seno (4.12).
	check(sin_q12(0) == 0, "sin 0");
	near(sin_q12(1024), 4096, 1, "sin pi/2");
	near(sin_q12(2048), 0, 1, "sin pi");
	near(cos_q12(0), 4096, 1, "cos 0");

	// Flags de punto respecto a la ventana.
	const Rect win = rect(0, 0, 320, 256);
	check(point_flags(v2(10, 10), win) == 0, "punto dentro");
	check(point_flags(v2(-1, 10), win) == 1, "punto izquierda");
	check(point_flags(v2(320, 10), win) == 2, "punto derecha");
	check(point_flags(v2(10, -1), win) == 4, "punto arriba");
	check(point_flags(v2(10, 256), win) == 8, "punto abajo");

	// Recorte de línea (Liang-Barsky).
	{
		Vec2 a = v2(-100, 100), b = v2(400, 100);
		check(clip_line(win, a, b), "clip_line cruza -> true");
		check(a.v[0].v >= 0 && a.v[0].v < 320 && b.v[0].v > 0 && b.v[0].v <= 320,
		      "clip_line extremos dentro");
	}
	{
		Vec2 a = v2(-100, -100), b = v2(-50, -50);
		check(!clip_line(win, a, b), "clip_line fuera -> false");
	}
	{
		Vec2 a = v2(50, 50), b = v2(100, 100);
		check(clip_line(win, a, b) && a.v[0].v == 50 && a.v[1].v == 50 && b.v[0].v == 100 &&
			      b.v[1].v == 100,
		      "clip_line dentro sin cambios");
	}

	// Recorte de polígono (Sutherland-Hodgman): un cuadrado que envuelve la ventana
	// debe quedar dentro de ella.
	{
		Vec2 poly[8] = {v2(-10, -10), v2(330, -10), v2(330, 266), v2(-10, 266)};
		Vec2 tmpb[8];
		const u32 n = clip_polygon(win, poly, tmpb, 4, PF_LEFT | PF_TOP | PF_RIGHT | PF_BOTTOM);
		check(n >= 3, "clip_polygon devuelve >= 3 vertices");
		bool all_in = true;
		for (u32 i = 0; i < n; ++i) {
			if (poly[i].v[0].v < 0 || poly[i].v[0].v > 320 || poly[i].v[1].v < 0 ||
			    poly[i].v[1].v > 256) {
				all_in = false;
			}
		}
		check(all_in, "clip_polygon vertices dentro");
	}

	if (failures == 0) {
		std::printf("OK: lib2d retro (geometria 2D tipada + recorte) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
