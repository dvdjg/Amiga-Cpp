// Test host de eng::core::math3d (port de lib3d, fixed-point 4.12).
// Valida identidad, traslación, escala, rotación sobre Z, composición y transform.
#include <eng/core/math3d.hpp>

#include <cstdio>

using namespace eng::math3d;

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
	Mat3x3 m;
	Vec3 out[2];

	// Identidad.
	load_identity(m);
	check(m.m00 == 4096 && m.m11 == 4096 && m.m22 == 4096, "identidad diagonal");
	check(m.m01 == 0 && m.m02 == 0 && m.m10 == 0 && m.m12 == 0 && m.m20 == 0 && m.m21 == 0, "identidad fuera");
	check(m.x == 0 && m.y == 0 && m.z == 0, "identidad traslacion");

	// transform con identidad no cambia.
	const Vec3 in[2] = { {100, 200, 300}, {-50, 25, -75} };
	transform(m, out, in, 2);
	check(out[0].x == 100 && out[0].y == 200 && out[0].z == 300, "transform identidad p0");
	check(out[1].x == -50 && out[1].y == 25 && out[1].z == -75, "transform identidad p1");

	// Traslación.
	translate(m, 10, -20, 30);
	transform(m, out, in, 1);
	check(out[0].x == 110 && out[0].y == 180 && out[0].z == 330, "translate");

	// Escala 0.5.
	load_identity(m);
	scale(m, 2048, 2048, 2048);
	transform(m, out, in, 2);
	check(out[0].x == 50 && out[0].y == 100 && out[0].z == 150, "scale 0.5 p0");
	check(out[1].x == -25 && out[1].y == 12 && out[1].z == -38, "scale 0.5 p1 (truncado)");

	// Rotación sobre Z 90° (ax=ay=0, az=1024): (x,y,z) -> (y,-x,z).
	load_identity(m);
	load_rotate(m, 0, 0, 1024);
	{
		const Vec3 p = {100, 0, 0};
		transform(m, out, &p, 1);
		near(out[0].x, 0, 1, "rotZ (100,0,0).x");
		near(out[0].y, 100, 1, "rotZ (100,0,0).y");
		near(out[0].z, 0, 1, "rotZ (100,0,0).z");
	}
	{
		const Vec3 p = {0, 100, 0};
		transform(m, out, &p, 1);
		near(out[0].x, -100, 1, "rotZ (0,100,0).x");
		near(out[0].y, 0, 1, "rotZ (0,100,0).y");
	}

	// compose(I, R) == R.
	Mat3x3 id;
	load_identity(id);
	Mat3x3 r;
	load_rotate(r, 0, 0, 1024);
	const Mat3x3 c = compose(id, r);
	check(c.m00 == r.m00 && c.m01 == r.m01 && c.m10 == r.m10 && c.m11 == r.m11,
		"compose(I,R) == R");

	// compose(R, R) con R = Rz(90°) da Rz(180°) = diag(-1,-1).
	Mat3x3 r2;
	load_rotate(r2, 0, 0, 1024);
	const Mat3x3 r180 = compose(r, r2);
	near(r180.m00, -4096, 2, "Rz90*Rz90 (0,0)");
	near(r180.m01, 0, 2, "Rz90*Rz90 (0,1)");
	near(r180.m10, 0, 2, "Rz90*Rz90 (1,0)");
	near(r180.m11, -4096, 2, "Rz90*Rz90 (1,1)");

	if (failures == 0) {
		std::printf("OK: math3d (lib3d 4.12) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
