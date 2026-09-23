// Test host del álgebra lineal genérica (eng::math::Vec/Mat/Affine).
// El MISMO código sirve para fixed-point y para float (F2 del roadmap).
#include <eng/core/math/linalg.hpp>

#include <cstdio>
#include <eng/retro/fixed_q.hpp>

using namespace eng::math;
using namespace eng::retro;
using eng::s16;

static int failures = 0;
static void check(bool ok, const char *msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// --- invariantes de compilación ---
// `ratio * ratio = ratio` y `ratio * longitud = longitud`: tipos distintos.
static_assert(sizeof(decltype(Mat<2, q12>::identity() * Mat<2, q12>::identity())) == sizeof(Mat<2, q12>),
	      "mat*mat conserva tipo");
static_assert(sizeof(decltype(Mat<2, q12>::identity() * Vec<2, q0>::zero())) == sizeof(Vec<2, q0>),
	      "mat(ratio)*vec(longitud) -> vec(longitud)");
// El mismo template con float.
static_assert(sizeof(decltype(Mat<3, float>::identity() * Vec<3, float>::zero())) == sizeof(Vec<3, float>),
	      "las mismas plantillas con float");

int main() {
	// --- Vector: suma, resta y producto escalar fusionado ---
	const Vec<3, q12> a {{q12 {4096}, q12 {2048}, q12 {0}}}; // (1, 0.5, 0)
	const Vec<3, q12> b {{q12 {2048}, q12 {2048}, q12 {0}}}; // (0.5, 0.5, 0)
	check(dot(a, b) == q12 {3072}, "1*0.5 + 0.5*0.5 = 0.75 (fusionado)");
	check((a + b).x() == q12 {6144}, "suma de vectores");
	check((a - b).x() == q12 {2048}, "resta de vectores");

	// --- Matriz: R(90 grados) en 4.12, R*R = -I, R*(1,0) = (0,1) ---
	Mat<2, q12> r90 {};
	r90.m[0][0] = q12 {0}; r90.m[0][1] = q12 {-4096};
	r90.m[1][0] = q12 {4096}; r90.m[1][1] = q12 {0};
	const Mat<2, q12> r180 = r90 * r90;
	check(r180.m[0][0] == q12 {-4096} && r180.m[1][1] == q12 {-4096}, "R90*R90 = -I (diagonal)");
	check(r180.m[0][1] == q12 {0} && r180.m[1][0] == q12 {0}, "R90*R90 = -I (fuera)");

	const Vec<2, q0> p {{q0 {1}, q0 {0}}}; // punto (1, 0) en LONGITUD
	const Vec<2, q0> q = r90 * p;          // ratio * longitud -> longitud
	check(q.x() == q0 {0} && q.y() == q0 {1}, "R90 * (1,0) = (0,1)");

	check(transpose(r90).m[0][1] == q12 {4096}, "transpose intercambia");
	check(determinant(r90) == q12 {4096}, "det(R90) = 1");

	// --- Determinante 3x3 por cofactores (sin productos encadenados) ---
	Mat<3, q12> r90z {};
	r90z.m[0][0] = q12 {0};    r90z.m[0][1] = q12 {-4096}; r90z.m[0][2] = q12 {0};
	r90z.m[1][0] = q12 {4096}; r90z.m[1][1] = q12 {0};     r90z.m[1][2] = q12 {0};
	r90z.m[2][0] = q12 {0};    r90z.m[2][1] = q12 {0};     r90z.m[2][2] = q12 {4096};
	check(determinant(r90z) == q12 {4096}, "det(R90 3x3) = 1");
	check(determinant(Mat<3, q12>::identity()) == q12 {4096}, "det(I 3x3) = 1");

	// --- Inversa analítica (adj/det) 2x2 y 3x3 ---
	const Mat<2, q12> ir = inverse(r90);
	check(ir.m[0][0] == q12 {0} && ir.m[0][1] == q12 {4096} && ir.m[1][0] == q12 {-4096} && ir.m[1][1] == q12 {0},
	      "inverse(R90) = R(-90)");
	const Mat<2, q12> rr = r90 * ir;
	check(rr.m[0][0] == q12 {4096} && rr.m[0][1] == q12 {0} && rr.m[1][0] == q12 {0} && rr.m[1][1] == q12 {4096},
	      "R90 * inverse(R90) = I");

	const Mat<3, q12> rz = r90z * inverse(r90z);
	check(rz.m[0][0] == q12 {4096} && rz.m[1][1] == q12 {4096} && rz.m[2][2] == q12 {4096} &&
	      rz.m[0][1] == q12 {0} && rz.m[1][0] == q12 {0},
	      "R * inverse(R) = I (3x3)");

	// --- Afín: M*p + t, con t en LONGITUD (el caso que el tipado resuelve) ---
	Affine<2, q12, q0> tr {};
	tr.m = r90;
	tr.t = Vec<2, q0> {{q0 {10}, q0 {20}}};
	const Vec<2, q0> q2 = transform(tr, p); // R90*(1,0) + (10,20) = (10, 21)
	check(q2.x() == q0 {10} && q2.y() == q0 {21}, "afin: M*p + t");
	translate(tr, Vec<2, q0> {{q0 {-10}, q0 {-20}}});
	check(tr.t.x() == q0 {0} && tr.t.y() == q0 {0}, "translate tipado (longitud + longitud)");

	// --- El MISMO código con float ---
	Mat<3, float> f {};
	f.m[0][0] = 1.0f; f.m[0][1] = 2.0f; f.m[0][2] = 3.0f;
	f.m[1][0] = 0.0f; f.m[1][1] = 1.0f; f.m[1][2] = 0.0f;
	f.m[2][0] = 0.0f; f.m[2][1] = 0.0f; f.m[2][2] = 1.0f;
	const Mat<3, float> f2 = f * f;
	check(f2.m[0][1] == 4.0f && f2.m[0][2] == 6.0f, "float: f*f acumula igual");
	const Vec<3, float> fv {{1.0f, 1.0f, 1.0f}};
	const Vec<3, float> fo = f * fv;
	check(fo.x() == 6.0f && fo.y() == 1.0f, "float: mat*vec");
	check(determinant(f) == 1.0f, "det(float 3x3) = 1");
	check((f * inverse(f)).m[0][0] == 1.0f && (f * inverse(f)).m[1][1] == 1.0f, "float: f * inverse(f) = I");

	// --- mulu32x16: producto u32*u16 sin libgcc (presupuestos/tamanos) ---
	check(eng::math::mulu32x16(0x12345u, 0x6789u) == 0x12345u * 0x6789u,
	      "mulu32x16 coincide con u32*u16");

	if (failures == 0) {
		std::printf("OK: linalg (Vec/Mat/Affine genericos sobre el escalar) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
