#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
// HOST-050 — Red de seguridad de F3: las versiones NUEVAS (librería genérica) deben dar
// EXACTAMENTE los mismos 12 valores que las viejas (math3d), para todos los ángulos.
// Cualquier diferencia es un fallo, no una mejora.
#include <eng/retro/fixed_trig.hpp>
#include <eng/core/linalg.hpp>
#include <eng/platform/amiga/gfx3d.hpp>

#include <cstdio>
#include <eng/retro/fixed_q.hpp>

using namespace eng::math;
using namespace eng::retro;
using eng::u16;

// --- Implementaciones NUEVAS, espejo de math3d::load_rotate / load_reverse_rotate ---
static Mat<3, q12> new_load_rotate(u16 ax, u16 ay, u16 az) {
	const q12 sinX {eng::retro::sin(turns(ax)).v}, cosX {eng::retro::cos(turns(ax)).v};
	const q12 sinY {eng::retro::sin(turns(ay)).v}, cosY {eng::retro::cos(turns(ay)).v};
	const q12 sinZ {eng::retro::sin(turns(az)).v}, cosZ {eng::retro::cos(turns(az)).v};

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
	const q12 sinX {eng::retro::sin(turns(ax)).v}, cosX {eng::retro::cos(turns(ax)).v};
	const q12 sinY {eng::retro::sin(turns(ay)).v}, cosY {eng::retro::cos(turns(ay)).v};
	const q12 sinZ {eng::retro::sin(turns(az)).v}, cosZ {eng::retro::cos(turns(az)).v};

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
		eng::math3d::Mat3<> o {};
		eng::math3d::load_rotate(o, eng::retro::angle_to_radians(a), eng::retro::angle_to_radians(a),
					 eng::retro::angle_to_radians(a));
		const Mat<3, q12> n = new_load_rotate(a, a, a);
		cmp("load_rotate", o.m[0][0].v, n.m[0][0].v, a, "m00");
		cmp("load_rotate", o.m[0][1].v, n.m[0][1].v, a, "m01");
		cmp("load_rotate", o.m[0][2].v, n.m[0][2].v, a, "m02");
		cmp("load_rotate", o.m[1][0].v, n.m[1][0].v, a, "m10");
		cmp("load_rotate", o.m[1][1].v, n.m[1][1].v, a, "m11");
		cmp("load_rotate", o.m[1][2].v, n.m[1][2].v, a, "m12");
		cmp("load_rotate", o.m[2][0].v, n.m[2][0].v, a, "m20");
		cmp("load_rotate", o.m[2][1].v, n.m[2][1].v, a, "m21");
		cmp("load_rotate", o.m[2][2].v, n.m[2][2].v, a, "m22");

		const u16 ax = static_cast<u16>(a * 3u);
		const u16 ay = static_cast<u16>(a * 7u);
		const u16 az = static_cast<u16>(a * 11u);
		eng::math3d::Mat3<> o2 {};
		eng::math3d::load_reverse_rotate(o2, eng::retro::angle_to_radians(ax),
						 eng::retro::angle_to_radians(ay),
						 eng::retro::angle_to_radians(az));
		const Mat<3, q12> n2 = new_load_reverse_rotate(ax, ay, az);
		cmp("reverse", o2.m[0][0].v, n2.m[0][0].v, a, "m00");
		cmp("reverse", o2.m[0][1].v, n2.m[0][1].v, a, "m01");
		cmp("reverse", o2.m[0][2].v, n2.m[0][2].v, a, "m02");
		cmp("reverse", o2.m[1][0].v, n2.m[1][0].v, a, "m10");
		cmp("reverse", o2.m[1][1].v, n2.m[1][1].v, a, "m11");
		cmp("reverse", o2.m[1][2].v, n2.m[1][2].v, a, "m12");
		cmp("reverse", o2.m[2][0].v, n2.m[2][0].v, a, "m20");
		cmp("reverse", o2.m[2][1].v, n2.m[2][1].v, a, "m21");
		cmp("reverse", o2.m[2][2].v, n2.m[2][2].v, a, "m22");
	}

	if (mismatches == 0) {
		std::printf("OK: bit-exactitud math3d (4096 angulos, load_rotate + reverse) confirmada.\n");
		return 0;
	}
	std::printf("FAIL: %d discrepancias\n", mismatches);
	return 1;
}
