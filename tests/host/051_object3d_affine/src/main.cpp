// HOST-051 — Red de seguridad para migrar `object3d` a Affine: la transformación del
// objeto construida con la librería genérica debe dar EXACTAMENTE los mismos 12 valores
// y la misma cámara que `object3d::update_object_transformation` actual.
#include <eng/core/linalg.hpp>
#include <eng/core/object3d.hpp>

#include <cstdio>

using namespace eng::math;
using eng::s16;
using eng::u16;

// --- version nueva, espejo de la vieja, sobre Affine<3,q12,q0> ---
struct NewTransform {
	Affine<3, q12, q0> o2w {};
	Affine<3, q12, q0> w2o {};
	Vec<3, q0> camera {}; // la camara es una LONGITUD (exp 0)
};

static Mat<3, q12> new_load_rotate(u16 ax, u16 ay, u16 az) {
	const q12 sinX {eng::math2d::sin_q12(ax)}, cosX {eng::math2d::cos_q12(ax)};
	const q12 sinY {eng::math2d::sin_q12(ay)}, cosY {eng::math2d::cos_q12(ay)};
	const q12 sinZ {eng::math2d::sin_q12(az)}, cosZ {eng::math2d::cos_q12(az)};
	const q12 tmp0 = dot(sinY, cosZ);
	const q12 tmp1 = dot(sinY, sinZ);
	Mat<3, q12> m {};
	m.m[0][0] = dot(cosY, cosZ); m.m[0][1] = -dot(cosY, sinZ); m.m[0][2] = sinY;
	m.m[1][0] = dot(cosX, sinZ, sinX, tmp0); m.m[1][1] = dot(cosX, cosZ, -sinX, tmp1); m.m[1][2] = -dot(sinX, cosY);
	m.m[2][0] = dot(sinX, sinZ, -cosX, tmp0); m.m[2][1] = dot(sinX, cosZ, cosX, tmp1); m.m[2][2] = dot(cosX, cosY);
	return m;
}

static Mat<3, q12> new_load_reverse_rotate(u16 ax, u16 ay, u16 az) {
	const q12 sinX {eng::math2d::sin_q12(ax)}, cosX {eng::math2d::cos_q12(ax)};
	const q12 sinY {eng::math2d::sin_q12(ay)}, cosY {eng::math2d::cos_q12(ay)};
	const q12 sinZ {eng::math2d::sin_q12(az)}, cosZ {eng::math2d::cos_q12(az)};
	const q12 tmp0 = dot(sinX, sinY);
	const q12 tmp1 = dot(cosX, sinY);
	Mat<3, q12> m {};
	m.m[0][0] = dot(cosY, cosZ); m.m[0][1] = dot(tmp0, cosZ, -cosX, sinZ); m.m[0][2] = dot(tmp1, cosZ, sinX, sinZ);
	m.m[1][0] = dot(cosY, sinZ); m.m[1][1] = dot(tmp0, sinZ, cosX, cosZ); m.m[1][2] = dot(tmp1, sinZ, -sinX, cosZ);
	m.m[2][0] = -sinY; m.m[2][1] = dot(sinX, cosY); m.m[2][2] = dot(cosX, cosY);
	return m;
}

// El espejo: R (rotate) -> S (columnas escaladas) -> T (traslacion cruda, q0).
static NewTransform new_update(s16 rx, s16 ry, s16 rz, s16 sx, s16 sy, s16 sz, s16 tx, s16 ty, s16 tz) {
	NewTransform r {};
	Mat<3, q12> m = new_load_rotate(static_cast<u16>(rx), static_cast<u16>(ry), static_cast<u16>(rz));
	const q12 sxs {sx}, sys {sy}, szs {sz};
	for (int i = 0; i < 3; ++i) {
		m.m[i][0] = (m.m[i][0] * sxs).norm<12>().narrow<s16>();
		m.m[i][1] = (m.m[i][1] * sys).norm<12>().narrow<s16>();
		m.m[i][2] = (m.m[i][2] * szs).norm<12>().narrow<s16>();
	}
	r.o2w.m = m;
	r.o2w.t = Vec<3, q0> {{from_int<s16>(tx), from_int<s16>(ty), from_int<s16>(tz)}};
	// w2o = compose(scale_inverso, reverse_rotate)
	Mat<3, q12> ms = Mat<3, q12>::identity();
	ms.m[0][0] = q12 {eng::math2d::div16(eng::math2d::kOne8_24, sx)};
	ms.m[1][1] = q12 {eng::math2d::div16(eng::math2d::kOne8_24, sy)};
	ms.m[2][2] = q12 {eng::math2d::div16(eng::math2d::kOne8_24, sz)};
	Mat<3, q12> mr = new_load_reverse_rotate(static_cast<u16>(-rx), static_cast<u16>(-ry), static_cast<u16>(-rz));
	r.w2o.m = ms * mr;
	r.w2o.t = Vec<3, q0> {{from_int<s16>(static_cast<s16>(-tx)), from_int<s16>(static_cast<s16>(-ty)),
			       from_int<s16>(static_cast<s16>(-tz))}};
	// camara = M * (M.t), normalizada a q12
	for (int i = 0; i < 3; ++i) r.camera.v[i] = dot(r.w2o.m.m[i][0], r.w2o.t.v[0], r.w2o.m.m[i][1], r.w2o.t.v[1],
							  r.w2o.m.m[i][2], r.w2o.t.v[2]);
	return r;
}

static int bad = 0;
static void cmp(const char *f, s16 a, s16 b) {
	if (a != b) {
		if (bad < 10) std::printf("  [MISMATCH] %s: viejo=%d nuevo=%d\n", f, a, b);
		++bad;
	}
}

int main() {
	// Barrido de rotaciones (las 4096) con una escala y traslacion fijas, mas algunos
	// casos con escalas/traslaciones distintas (como la demo: scale 1.0, t=(0,0,-4000)).
	for (u16 a = 0; a < 4096; a += 7) {
		eng::object3d::Object3D o {};
		o.rotate = {static_cast<s16>(a), static_cast<s16>(a), static_cast<s16>(a)};
		o.scale = {4096, 4096, 4096};
		o.translate = {0, 0, -4000};
		eng::object3d::update_object_transformation(o);
		const NewTransform n = new_update(static_cast<s16>(a), static_cast<s16>(a), static_cast<s16>(a), 4096, 4096,
						  4096, 0, 0, -4000);
		cmp("o2w.m00", o.objectToWorld.m00, n.o2w.m.m[0][0].v);
		cmp("o2w.m11", o.objectToWorld.m11, n.o2w.m.m[1][1].v);
		cmp("o2w.m22", o.objectToWorld.m22, n.o2w.m.m[2][2].v);
		cmp("o2w.x", o.objectToWorld.x, n.o2w.t.v[0].v);
		cmp("o2w.z", o.objectToWorld.z, n.o2w.t.v[2].v);
		cmp("w2o.m00", o.worldToObject.m00, n.w2o.m.m[0][0].v);
		cmp("w2o.m12", o.worldToObject.m12, n.w2o.m.m[1][2].v);
		cmp("w2o.y", o.worldToObject.y, n.w2o.t.v[1].v);
		cmp("cam.x", o.camera.x, n.camera.v[0].v);
		cmp("cam.y", o.camera.y, n.camera.v[1].v);
		cmp("cam.z", o.camera.z, n.camera.v[2].v);
	}

	if (bad == 0) {
		std::printf("OK: bit-exactitud de update_object_transformation (barrido de angulos) confirmada.\n");
		return 0;
	}
	std::printf("FAIL: %d discrepancias\n", bad);
	return 1;
}
