// HOST-050 — Red de seguridad de F3: las versiones NUEVAS (librería genérica) deben dar
// EXACTAMENTE los mismos 12 valores que las viejas (math3d), para todos los ángulos.
// Cualquier diferencia es un fallo, no una mejora.
#include <eng/core/linalg.hpp>
#include <eng/core/math3d.hpp>

#include <cstdio>

using namespace eng::math;
using eng::u16;

// --- Implementaciones NUEVAS, espejo de math3d::load_rotate / load_reverse_rotate ---
static Mat<3, q12> new_load_rotate(u16 ax, u16 ay, u16 az) {
	const q12 sinX {eng::math2d::sin_q12(ax)}, cosX {eng::math2d::cos_q12(ax)};
	const q12 sinY {eng::math2d::sin_q12(ay)}, cosY {eng::math2d::cos_q12(ay)};
	const q12 sinZ {eng::math2d::sin_q12(az)}, cosZ {eng::math2d::cos_q12(az)};

	const q12 tmp0 = dot(sinY, cosZ);
	const q12 tmp1 = dot(sinY, sinZ);

	Mat<3, q12> m {};
	m.m[0][0] = dot(cosY, cosZ);
	m.m[0][1] = -dot(cosY, sinZ);
	m.m[0][2] = sinY;
	m.m[1][0] = dot(cosX, sinZ, sinX, tmp0);
	m.m[1][1] = dot(cosX, cosZ, -sinX, tmp1);
	m.m[1][2] = -dot(sinX, cosY);
	m.m[2][0] = dot(sinX, sinZ, -cosX, tmp0);
	m.m[2][1] = dot(sinX, cosZ, cosX, tmp1);
	m.m[2][2] = dot(cosX, cosY);
	return m;
}

static Mat<3, q12> new_load_reverse_rotate(u16 ax, u16 ay, u16 az) {
	const q12 sinX {eng::math2d::sin_q12(ax)}, cosX {eng::math2d::cos_q12(ax)};
	const q12 sinY {eng::math2d::sin_q12(ay)}, cosY {eng::math2d::cos_q12(ay)};
	const q12 sinZ {eng::math2d::sin_q12(az)}, cosZ {eng::math2d::cos_q12(az)};

	const q12 tmp0 = dot(sinX, sinY);
	const q12 tmp1 = dot(cosX, sinY);

	Mat<3, q12> m {};
	m.m[0][0] = dot(cosY, cosZ);
	m.m[0][1] = dot(tmp0, cosZ, -cosX, sinZ);
	m.m[0][2] = dot(tmp1, cosZ, sinX, sinZ);
	m.m[1][0] = dot(cosY, sinZ);
	m.m[1][1] = dot(tmp0, sinZ, cosX, cosZ);
	m.m[1][2] = dot(tmp1, sinZ, -sinX, cosZ);
	m.m[2][0] = -sinY;
	m.m[2][1] = dot(sinX, cosY);
	m.m[2][2] = dot(cosX, cosY);
	return m;
}

static int mismatches = 0;
static void cmp(const char *what, eng::s16 old_v, eng::s16 new_v, u16 a, const char *field) {
	if (old_v != new_v) {
		if (mismatches < 12) {
			std::printf("  [MISMATCH] %s ang=%u %s: viejo=%d nuevo=%d\n", what, a, field, old_v, new_v);
		}
		++mismatches;
	}
}

int main() {
	// Los 4096 ángulos, con los tres ejes iguales (el caso de la demo) y con ejes
	// distintos, para load_rotate y load_reverse_rotate.
	for (u16 a = 0; a < 4096; ++a) {
		eng::math3d::Mat3x3 o {};
		eng::math3d::load_rotate(o, a, a, a);
		const Mat<3, q12> n = new_load_rotate(a, a, a);
		cmp("load_rotate", o.m00, n.m[0][0].v, a, "m00");
		cmp("load_rotate", o.m01, n.m[0][1].v, a, "m01");
		cmp("load_rotate", o.m02, n.m[0][2].v, a, "m02");
		cmp("load_rotate", o.m10, n.m[1][0].v, a, "m10");
		cmp("load_rotate", o.m11, n.m[1][1].v, a, "m11");
		cmp("load_rotate", o.m12, n.m[1][2].v, a, "m12");
		cmp("load_rotate", o.m20, n.m[2][0].v, a, "m20");
		cmp("load_rotate", o.m21, n.m[2][1].v, a, "m21");
		cmp("load_rotate", o.m22, n.m[2][2].v, a, "m22");

		const u16 ax = static_cast<u16>(a * 3u);
		const u16 ay = static_cast<u16>(a * 7u);
		const u16 az = static_cast<u16>(a * 11u);
		eng::math3d::Mat3x3 o2 {};
		eng::math3d::load_reverse_rotate(o2, ax, ay, az);
		const Mat<3, q12> n2 = new_load_reverse_rotate(ax, ay, az);
		cmp("reverse", o2.m00, n2.m[0][0].v, a, "m00");
		cmp("reverse", o2.m01, n2.m[0][1].v, a, "m01");
		cmp("reverse", o2.m02, n2.m[0][2].v, a, "m02");
		cmp("reverse", o2.m10, n2.m[1][0].v, a, "m10");
		cmp("reverse", o2.m11, n2.m[1][1].v, a, "m11");
		cmp("reverse", o2.m12, n2.m[1][2].v, a, "m12");
		cmp("reverse", o2.m20, n2.m[2][0].v, a, "m20");
		cmp("reverse", o2.m21, n2.m[2][1].v, a, "m21");
		cmp("reverse", o2.m22, n2.m[2][2].v, a, "m22");
	}

	if (mismatches == 0) {
		std::printf("OK: bit-exactitud math3d (4096 angulos, load_rotate + reverse) confirmada.\n");
		return 0;
	}
	std::printf("FAIL: %d discrepancias\n", mismatches);
	return 1;
}
