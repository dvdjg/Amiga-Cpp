// Test host de eng::math3d (especialización 4.12 sobre la librería genérica).
// Valida identidad, escala, rotación sobre Z, composición, transform y el afín.
#include <eng/platform/amiga/gfx3d.hpp>

#include <cstdio>
#include <eng/retro/fixed_q.hpp>

using namespace eng::math3d;
using namespace eng::retro;
using eng::math::Affine;
using eng::retro::q0;
using eng::retro::q12;
using eng::math::Vec;
using eng::s16;

static int failures = 0;
static void check(bool ok, const char *msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}
static void near(int got, int want, int tol, const char *msg) {
	if (got < want - tol || got > want + tol) {
		std::printf("  [FAIL] %s (got %d, want ~%d +-%d)\n", msg, got, want, tol);
		++failures;
	}
}
static s16 mv(const Mat3 &m, int r, int c) { return m.m[r][c].v; }

int main() {
	Mat3 m = Mat3::identity();
	Vec3 out[2];

	// Identidad.
	check(mv(m, 0, 0) == 4096 && mv(m, 1, 1) == 4096 && mv(m, 2, 2) == 4096, "identidad diagonal");
	check(mv(m, 0, 1) == 0 && mv(m, 1, 0) == 0 && mv(m, 2, 0) == 0, "identidad fuera");

	// transform con identidad no cambia.
	const Vec3 in[2] = {vec3(100, 200, 300), vec3(-50, 25, -75)};
	transform(m, out, in, 2);
	check(out[0].x().v == 100 && out[0].y().v == 200 && out[0].z().v == 300, "transform identidad p0");
	check(out[1].x().v == -50 && out[1].y().v == 25 && out[1].z().v == -75, "transform identidad p1");

	// Escala 0.5.
	m = Mat3::identity();
	scale(m, q12 {2048}, q12 {2048}, q12 {2048});
	transform(m, out, in, 2);
	check(out[0].x().v == 50 && out[0].y().v == 100 && out[0].z().v == 150, "scale 0.5 p0");
	check(out[1].x().v == -25 && out[1].y().v == 12 && out[1].z().v == -38, "scale 0.5 p1 (truncado)");

	// Rotación sobre Z 90° (az=1024): (x,y,z) -> (y,-x,z).
	m = Mat3::identity();
	load_rotate(m, 0, 0, 1024);
	{
		const Vec3 p = vec3(100, 0, 0);
		transform(m, out, &p, 1);
		near(out[0].x().v, 0, 1, "rotZ (100,0,0).x");
		near(out[0].y().v, 100, 1, "rotZ (100,0,0).y");
	}
	{
		const Vec3 p = vec3(0, 100, 0);
		transform(m, out, &p, 1);
		near(out[0].x().v, -100, 1, "rotZ (0,100,0).x");
		near(out[0].y().v, 0, 1, "rotZ (0,100,0).y");
	}

	// compose(I, R) == R  (la composición ahora es el `operator*` de la librería).
	const Mat3 id = Mat3::identity();
	Mat3 r = Mat3::identity();
	load_rotate(r, 0, 0, 1024);
	const Mat3 c = id * r;
	check(mv(c, 0, 0) == mv(r, 0, 0) && mv(c, 0, 1) == mv(r, 0, 1) && mv(c, 1, 0) == mv(r, 1, 0) &&
		      mv(c, 1, 1) == mv(r, 1, 1),
	      "compose(I,R) == R");

	// R*R con R = Rz(90°) da Rz(180°) = diag(-1,-1).
	Mat3 r2 = Mat3::identity();
	load_rotate(r2, 0, 0, 1024);
	const Mat3 r180 = r * r2;
	near(mv(r180, 0, 0), -4096, 2, "Rz90*Rz90 (0,0)");
	near(mv(r180, 0, 1), 0, 2, "Rz90*Rz90 (0,1)");
	near(mv(r180, 1, 0), 0, 2, "Rz90*Rz90 (1,0)");
	near(mv(r180, 1, 1), -4096, 2, "Rz90*Rz90 (1,1)");

	// Transformación AFÍN: M*p + t, con la traslación en LONGITUD (q0).
	{
		Affine<3, q12, q0> a {Mat3::identity(), Vec<3, q0> {{q0 {10}, q0 {-20}, q0 {30}}}};
		const Vec<3, q0> p {{q0 {100}, q0 {200}, q0 {300}}};
		const Vec<3, q0> q = eng::math::transform(a, p);
		check(q.x().v == 110 && q.y().v == 180 && q.z().v == 330, "afin: M*p + t");
	}

	// Visibilidad de cara (back-face culling) y claves de orden Z.
	{
		const Vec3 a = vec3(0, 0, 0), b = vec3(100, 0, 0), cc = vec3(0, 100, 0);
		check(face_visible(a, b, cc, vec3(50, 50, 200)), "cara visible desde +z");
		check(!face_visible(a, b, cc, vec3(50, 50, -200)), "cara no visible desde -z");
	}
	{
		const Vec3 a = vec3(0, 0, 10), b = vec3(0, 0, 20), cc = vec3(0, 0, 30);
		check(face_z_sum(a, b, cc) == 60, "face_z_sum");
		check(face_z_min(a, b, cc) == 10, "face_z_min");
	}

	if (failures == 0) {
		std::printf("OK: math3d (especializacion 4.12 sobre la libreria generica) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
