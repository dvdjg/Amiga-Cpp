// ============================================================================
// Test HOST-056: escalar de coma flotante de 16 bits `eng::math::MiniFloat16`.
// ============================================================================
//
// Respalda la API de `eng/core/minifloat.hpp`: formato (1|5|10, sesgo 15), conversión
// explícita a/desde `float`, aritmética (+ - * / con signo) y los rasgos que lo hacen
// usable como escalar de la librería genérica (`scalar_traits`, `eng/core/linalg.hpp`).
//
// La validación no es "compila y da algo": se compara la MISMA operación hecha en
// `MiniFloat16` contra la hecha en `float`, sobre entradas idénticas (las ya
// redondeadas a 16 bits). Cubre:
//
//   1. Fronteras del formato (cero, uno, 2^-14, máximo finito, overflow, underflow).
//   2. Enteros exactos (0..2048).
//   3. Barrido escalar de + - * / con distintas magnitudes y signos.
//   4. Operaciones de matrices (2x2/3x3/4x4): suma, resta, producto, traspuesta,
//      Mat*Vec, determinante, inversa analítica 2x2/3x3 y afines (transform/compose).
//
// Los errores de las operaciones con acumulación (producto de matrices, Mat*Vec) se
// miden **normalizados por la suma de |términos|**, no por el resultado: así un
// resultado cercano a cero por cancelación no dispara un error relativo enorme que no
// informa de nada. El error relativo por operación elemental es ~2^-11 ≈ 4.9e-4.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/056_minifloat16

#include <cmath>
#include <cstdio>

#include <eng/core/linalg.hpp>
#include <eng/core/minifloat.hpp>

using eng::math::MiniFloat16;
namespace m = eng::math;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr float kTiny = 1.0e-30f;

float rel_err(float got, float want) {
	const float d = std::fabs(want);
	return d < 1.0e-20f ? std::fabs(got - want) : std::fabs(got - want) / d;
}

template <int N>
float max_rel_mat(const m::Mat<N, float>& got, const m::Mat<N, float>& want) {
	float mx = 0.0f;
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) mx = std::fmax(mx, rel_err(got.m[i][j], want.m[i][j]));
	return mx;
}

template <int N>
float max_rel_vec(const m::Vec<N, float>& got, const m::Vec<N, float>& want) {
	float mx = 0.0f;
	for (int i = 0; i < N; ++i) mx = std::fmax(mx, rel_err(got.v[i], want.v[i]));
	return mx;
}

// ---------------------------------------------------------------------------
//  Utilidades genéricas sobre el escalar (las MISMAS para float y MiniFloat16)
// ---------------------------------------------------------------------------

template <typename S>
S sv(float x) {
	return S(x);
}

template <typename S, int N>
m::Mat<N, float> mat_to_float(const m::Mat<N, S>& a) {
	m::Mat<N, float> r {};
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) r.m[i][j] = static_cast<float>(a.m[i][j]);
	return r;
}

template <typename S, int N>
m::Vec<N, float> vec_to_float(const m::Vec<N, S>& a) {
	m::Vec<N, float> r {};
	for (int i = 0; i < N; ++i) r.v[i] = static_cast<float>(a.v[i]);
	return r;
}

template <int N>
bool mat_equal_raw(const m::Mat<N, MiniFloat16>& a, const m::Mat<N, MiniFloat16>& b) {
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j)
			if (!(a.m[i][j] == b.m[i][j])) return false;
	return true;
}

/// Error de `a*b` normalizado por `sum_k |a_ik * b_kj|` (estable ante cancelación).
template <int N>
float mat_mul_err(const m::Mat<N, float>& got, const m::Mat<N, float>& a, const m::Mat<N, float>& b) {
	float mx = 0.0f;
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) {
			float want = 0.0f, scale = kTiny;
			for (int k = 0; k < N; ++k) {
				const float p = a.m[i][k] * b.m[k][j];
				want += p;
				scale += std::fabs(p);
			}
			mx = std::fmax(mx, std::fabs(got.m[i][j] - want) / scale);
		}
	return mx;
}

/// Error de `a*v` normalizado por `sum_k |a_ik * v_k|`.
template <int N>
float mat_vec_err(const m::Vec<N, float>& got, const m::Mat<N, float>& a, const m::Vec<N, float>& v) {
	float mx = 0.0f;
	for (int i = 0; i < N; ++i) {
		float want = 0.0f, scale = kTiny;
		for (int k = 0; k < N; ++k) {
			const float p = a.m[i][k] * v.v[k];
			want += p;
			scale += std::fabs(p);
		}
		mx = std::fmax(mx, std::fabs(got.v[i] - want) / scale);
	}
	return mx;
}

/// Error de `M*v + t` normalizado por `|t| + sum_k |M_ik * v_k|`.
template <int N>
float affine_err(const m::Vec<N, float>& got, const m::Mat<N, float>& m, const m::Vec<N, float>& t,
		 const m::Vec<N, float>& v) {
	float mx = 0.0f;
	for (int i = 0; i < N; ++i) {
		float want = t.v[i], scale = kTiny + std::fabs(t.v[i]);
		for (int k = 0; k < N; ++k) {
			const float p = m.m[i][k] * v.v[k];
			want += p;
			scale += std::fabs(p);
		}
		mx = std::fmax(mx, std::fabs(got.v[i] - want) / scale);
	}
	return mx;
}

/// ¿El resultado cae dentro del rango finito del formato? Fuera de él (overflow a ∞ o
/// underflow a 0) el error relativo no informa de la precisión de la operación.
bool representable(float x) {
	const float ax = std::fabs(x);
	return std::isfinite(x) && ax >= std::ldexp(1.0f, -14) && ax <= 65504.0f;
}

/// Error de `a+b` normalizado por `max(|a|,|b|)`.
template <int N>
float mat_add_err(const m::Mat<N, float>& got, const m::Mat<N, float>& a, const m::Mat<N, float>& b) {
	float mx = 0.0f;
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) {
			const float scale = std::fmax(std::fabs(a.m[i][j]), std::fabs(b.m[i][j])) + kTiny;
			mx = std::fmax(mx, std::fabs(got.m[i][j] - (a.m[i][j] + b.m[i][j])) / scale);
		}
	return mx;
}

// ---------------------------------------------------------------------------
//  Generador determinista (LCG) para el barrido aleatorio
// ---------------------------------------------------------------------------

eng::u32 g_seed = 0x12345678u;

float rnd_unit() {
	g_seed = g_seed * 1664525u + 1013904223u;
	return static_cast<float>((g_seed >> 8) & 0xFFFFu) / 65536.0f;
}

float rnd_span(float lo, float hi) {
	return lo + (hi - lo) * rnd_unit();
}

template <typename S, int N>
m::Mat<N, S> rnd_mat(float lo, float hi) {
	m::Mat<N, S> r {};
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) r.m[i][j] = sv<S>(rnd_span(lo, hi));
	return r;
}

template <typename S, int N>
m::Vec<N, S> rnd_vec(float lo, float hi) {
	m::Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = sv<S>(rnd_span(lo, hi));
	return r;
}

// ---------------------------------------------------------------------------
//  Determinante e inversa analíticos (no los da el engine; se ejercitan aquí)
// ---------------------------------------------------------------------------

template <typename S>
S det2(const m::Mat<2, S>& a) {
	return a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0];
}

template <typename S>
m::Mat<2, S> inverse2(const m::Mat<2, S>& a) {
	const S inv_d = sv<S>(1.0f) / det2(a);
	m::Mat<2, S> r {};
	r.m[0][0] = a.m[1][1] * inv_d;
	r.m[0][1] = -a.m[0][1] * inv_d;
	r.m[1][0] = -a.m[1][0] * inv_d;
	r.m[1][1] = a.m[0][0] * inv_d;
	return r;
}

template <typename S>
S det3(const m::Mat<3, S>& a) {
	return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) -
	       a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) +
	       a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
}

template <typename S>
m::Mat<3, S> inverse3(const m::Mat<3, S>& a) {
	const S id = sv<S>(1.0f) / det3(a);
	m::Mat<3, S> r {};
	r.m[0][0] = (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) * id;
	r.m[0][1] = -(a.m[0][1] * a.m[2][2] - a.m[0][2] * a.m[2][1]) * id;
	r.m[0][2] = (a.m[0][1] * a.m[1][2] - a.m[0][2] * a.m[1][1]) * id;
	r.m[1][0] = -(a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) * id;
	r.m[1][1] = (a.m[0][0] * a.m[2][2] - a.m[0][2] * a.m[2][0]) * id;
	r.m[1][2] = -(a.m[0][0] * a.m[1][2] - a.m[0][2] * a.m[1][0]) * id;
	r.m[2][0] = (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]) * id;
	r.m[2][1] = -(a.m[0][0] * a.m[2][1] - a.m[0][1] * a.m[2][0]) * id;
	r.m[2][2] = (a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0]) * id;
	return r;
}

// ---------------------------------------------------------------------------
//  1. Fronteras del formato
// ---------------------------------------------------------------------------

void test_format() {
	check(MiniFloat16(0.0f).raw == 0x0000u, "0.0 -> raw 0");
	check(MiniFloat16(-0.0f).raw == 0x0000u, "-0.0 -> raw 0");
	check(MiniFloat16(1.0f).raw == 0x3C00u, "1.0 -> 0x3C00");
	check(MiniFloat16(2.0f).raw == 0x4000u, "2.0 -> 0x4000");
	check(MiniFloat16(0.5f).raw == 0x3800u, "0.5 -> 0x3800");
	check(MiniFloat16(1.5f).raw == 0x3E00u, "1.5 -> 0x3E00");
	check(MiniFloat16(-1.0f).raw == 0xBC00u, "-1.0 -> 0xBC00");

	// Máximo finito: (1 + 1023/1024) * 2^15 = 65504.
	check(MiniFloat16(65504.0f).raw == 0x7BFFu, "65504 -> 0x7BFF (máximo finito)");
	check(static_cast<float>(MiniFloat16(65504.0f)) == 65504.0f, "65504 vuelta exacta");

	// Mínimo normal: 2^-14 (campo exponente 1).
	check(MiniFloat16::from_raw(0x0400u).raw == 0x0400u, "2^-14 -> 0x0400");
	check(static_cast<float>(MiniFloat16::from_raw(0x0400u)) == std::ldexp(1.0f, -14),
	      "2^-14 vuelta exacta");

	// Underflow (sin denormales) y overflow (infinito).
	check(MiniFloat16(1.0e-5f).is_zero(), "1e-5 -> cero (underflow)");
	check(!MiniFloat16(1.0e-4f).is_zero(), "1e-4 distinto de cero");
	check(MiniFloat16(70000.0f).is_inf(), "70000 -> infinito (overflow)");
	check(MiniFloat16(-70000.0f).is_inf(), "-70000 -> infinito");
	check(MiniFloat16::from_raw(0x7C00u).is_inf(), "0x7C00 es infinito");

	// Orden total: los negativos no se pueden comparar como enteros sin signo.
	check(sv<MiniFloat16>(-2.0f) < sv<MiniFloat16>(-1.0f), "-2 < -1");
	check(sv<MiniFloat16>(-1.0f) < sv<MiniFloat16>(0.0f), "-1 < 0");
	check(sv<MiniFloat16>(0.0f) < sv<MiniFloat16>(1.0f), "0 < 1");
	check(sv<MiniFloat16>(1.0f) < sv<MiniFloat16>(100.0f), "1 < 100");
	check(sv<MiniFloat16>(65504.0f) < MiniFloat16::from_raw(0x7C00u), "máximo finito < inf");

	// Redondeo al más cercano: media ulp no cambia 1.0; una ulp sí.
	check(static_cast<float>(MiniFloat16(1.0f + 1.0f / 4096.0f)) == 1.0f, "media ulp redondea a 1.0");
	check(MiniFloat16(1.0f + 1.0f / 1024.0f).raw == 0x3C01u, "una ulp -> 0x3C01");

	// Borde inferior: [2^-15, 2^-14) redondea al mínimo normal; por debajo, a cero.
	check(MiniFloat16(1.5f * std::ldexp(1.0f, -15)).raw == 0x0400u,
	      "1.5·2^-15 (float) -> 2^-14");
	check(MiniFloat16(std::ldexp(1.0f, -16)).raw == 0u, "2^-16 (float) -> 0");
	check((MiniFloat16::from_raw(0x0400u) * MiniFloat16(0.75f)).raw == 0x0400u,
	      "2^-14 · 0.75 -> 2^-14 (redondeo en la aritmética)");
	check((MiniFloat16::from_raw(0x0400u) * MiniFloat16(0.5f)).raw == 0x0400u,
	      "2^-15 exacto -> 2^-14 (empate hacia arriba)");
	check((MiniFloat16::from_raw(0x0400u) * MiniFloat16(0.25f)).is_zero(),
	      "2^-16 -> 0 (por debajo de la mitad)");
}

// ---------------------------------------------------------------------------
//  2. Enteros exactos y barrido escalar vs float
// ---------------------------------------------------------------------------

void test_exact_integers() {
	bool ok = true;
	for (int i = 0; i <= 2048; ++i) {
		if (static_cast<float>(MiniFloat16(static_cast<float>(i))) != static_cast<float>(i)) {
			ok = false;
			std::printf("[FAIL] entero %d no exacto\n", i);
			break;
		}
	}
	check(ok, "enteros 0..2048 exactos (ulp = 1 en [1024,2048))");
}

void test_scalar_sweep() {
	float e_add = 0.0f, e_sub = 0.0f, e_mul = 0.0f, e_div = 0.0f;
	const float mags[] = {1.0f, 1.1f, 1.3f, 1.5f, 1.7f, 1.9f};

	for (int e1 = -13; e1 <= 15; ++e1) {
		const float base1 = std::ldexp(1.0f, e1);
		for (float ma : mags)
			for (int s1 = 0; s1 < 2; ++s1) {
				const float av = base1 * ma * (s1 ? -1.0f : 1.0f);
				for (int e2 = -13; e2 <= 15; ++e2) {
					const float base2 = std::ldexp(1.0f, e2);
					for (float mb : mags)
						for (int s2 = 0; s2 < 2; ++s2) {
							const float bv = base2 * mb * (s2 ? -1.0f : 1.0f);
							const MiniFloat16 a(av), b(bv);
							const float fa = static_cast<float>(a);
							const float fb = static_cast<float>(b);
							const float big = std::fmax(std::fabs(fa), std::fabs(fb));

							if (representable(fa * fb))
								e_mul = std::fmax(e_mul, rel_err(static_cast<float>(a * b), fa * fb));
							if (representable(fa / fb))
								e_div = std::fmax(e_div, rel_err(static_cast<float>(a / b), fa / fb));

							// Se ignoran los resultados con cancelación catastrófica
							// (el error relativo no acota nada) y los que salen del rango
							// finito, donde entra en juego la saturación a 0/∞.
							const float sa = fa + fb, ss = fa - fb;
							if (representable(sa) && std::fabs(sa) >= 0.25f * big)
								e_add = std::fmax(e_add, rel_err(static_cast<float>(a + b), sa));
							if (representable(ss) && std::fabs(ss) >= 0.25f * big)
								e_sub = std::fmax(e_sub, rel_err(static_cast<float>(a - b), ss));
						}
				}
			}
	}

	std::printf("  escalar (rel): + %.2e  - %.2e  * %.2e  / %.2e\n", e_add, e_sub, e_mul, e_div);
	check(e_add <= 5.0e-3f, "suma: error relativo <= 5e-3");
	check(e_sub <= 5.0e-3f, "resta: error relativo <= 5e-3");
	check(e_mul <= 1.0e-3f, "producto: error relativo <= 1e-3");
	check(e_div <= 2.0e-3f, "división: error relativo <= 2e-3");

	// Identidades exactas del formato.
	const MiniFloat16 a(3.5f);
	check((a + MiniFloat16::zero()).raw == a.raw, "a + 0 == a exacto");
	check((a - a).raw == 0u, "a - a == 0 exacto");
	check((a * MiniFloat16::one()).raw == a.raw, "a * 1 == a exacto");
	check((-a).raw == (a.raw ^ MiniFloat16::sign_mask), "-a invierte el signo");
	check((MiniFloat16(-3.5f) * MiniFloat16(-2.0f)).raw == MiniFloat16(7.0f).raw, "signos en producto");
}

// ---------------------------------------------------------------------------
//  3. Matrices: misma operación en MiniFloat16 y en float sobre entradas iguales
// ---------------------------------------------------------------------------

void test_matrices() {
	using MF = MiniFloat16;

	const m::Mat<3, MF> a3 = {{{sv<MF>(1.3f), sv<MF>(-2.7f), sv<MF>(0.9f)},
				   {sv<MF>(3.1f), sv<MF>(0.6f), sv<MF>(-1.4f)},
				   {sv<MF>(-0.35f), sv<MF>(4.2f), sv<MF>(2.3f)}}};
	const m::Mat<3, MF> b3 = {{{sv<MF>(0.7f), sv<MF>(1.1f), sv<MF>(-3.3f)},
				   {sv<MF>(2.2f), sv<MF>(-1.6f), sv<MF>(0.45f)},
				   {sv<MF>(1.15f), sv<MF>(2.7f), sv<MF>(0.85f)}}};
	const m::Vec<3, MF> v3 = {sv<MF>(1.5f), sv<MF>(-2.0f), sv<MF>(0.75f)};

	const auto af = mat_to_float(a3);
	const auto bf = mat_to_float(b3);
	const auto vf = vec_to_float(v3);

	const m::Mat<3, float> neg_bf = {{{-bf.m[0][0], -bf.m[0][1], -bf.m[0][2]},
					  {-bf.m[1][0], -bf.m[1][1], -bf.m[1][2]},
					  {-bf.m[2][0], -bf.m[2][1], -bf.m[2][2]}}};
	const float e_add = mat_add_err(mat_to_float(a3 + b3), af, bf);
	const float e_sub = mat_add_err(mat_to_float(a3 - b3), af, neg_bf);
	const float e_mul = mat_mul_err(mat_to_float(a3 * b3), af, bf);
	const float e_vec = mat_vec_err(vec_to_float(a3 * v3), af, vf);

	// Traspuesta: copia de elementos, sin redondeo alguno.
	check(mat_equal_raw(m::transpose(m::transpose(a3)), a3), "transpose(transpose(A)) == A exacto");

	// Identidad: `I * A` no debe redondear (0*A da cero y sumar cero es exacto).
	check(mat_equal_raw(m::Mat<3, MF>::identity() * a3, a3), "I * A == A exacto (sin redondeo)");

	std::printf("  3x3 (norm. por escala): + %.2e  - %.2e  * %.2e  M*v %.2e\n", e_add, e_sub, e_mul,
		    e_vec);
	check(e_add <= 2.0e-3f, "3x3 suma: error <= 2e-3");
	check(e_sub <= 2.0e-3f, "3x3 resta: error <= 2e-3");
	check(e_mul <= 5.0e-3f, "3x3 producto: error <= 5e-3");
	check(e_vec <= 5.0e-3f, "3x3 Mat*Vec: error <= 5e-3");

	// Producto 4x4 (acumulación más larga, con traslación).
	const m::Mat<4, MF> a4 = {{{sv<MF>(1.3f), sv<MF>(0.25f), sv<MF>(-0.6f), sv<MF>(2.1f)},
				   {sv<MF>(0.0f), sv<MF>(1.7f), sv<MF>(0.9f), sv<MF>(-1.1f)},
				   {sv<MF>(-0.35f), sv<MF>(0.0f), sv<MF>(2.3f), sv<MF>(3.1f)},
				   {sv<MF>(0.0f), sv<MF>(0.0f), sv<MF>(0.0f), sv<MF>(1.0f)}}};
	const m::Mat<4, MF> b4 = {{{sv<MF>(1.1f), sv<MF>(-0.6f), sv<MF>(0.35f), sv<MF>(-2.2f)},
				   {sv<MF>(0.65f), sv<MF>(1.2f), sv<MF>(0.0f), sv<MF>(1.7f)},
				   {sv<MF>(0.0f), sv<MF>(0.35f), sv<MF>(1.4f), sv<MF>(0.55f)},
				   {sv<MF>(0.0f), sv<MF>(0.0f), sv<MF>(0.0f), sv<MF>(1.0f)}}};
	const float e_mul4 =
		mat_mul_err(mat_to_float(a4 * b4), mat_to_float(a4), mat_to_float(b4));
	std::printf("  4x4 *: %.2e\n", e_mul4);
	check(e_mul4 <= 5.0e-3f, "4x4 producto: error <= 5e-3");

	// Determinante e inversa 3x3 (matriz bien condicionada, det = 18).
	const m::Mat<3, MF> s3 = {{{sv<MF>(4.0f), sv<MF>(1.0f), sv<MF>(0.0f)},
				   {sv<MF>(1.0f), sv<MF>(3.0f), sv<MF>(1.0f)},
				   {sv<MF>(0.0f), sv<MF>(1.0f), sv<MF>(2.0f)}}};
	const auto s3f = mat_to_float(s3);
	const float det_mf = static_cast<float>(det3(s3));
	const float det_fl = det3(s3f);
	std::printf("  det3 = %.4f (float %.4f, rel %.2e)\n", det_mf, det_fl,
		    rel_err(det_mf, det_fl));
	check(rel_err(det_mf, det_fl) <= 5.0e-3f, "det3: error relativo <= 5e-3");

	const auto inv3 = inverse3(s3);
	const float e_inv3 = max_rel_mat(mat_to_float(inv3), inverse3(s3f));
	// La inversa entrada a entrada puede tener entradas pequeñas: además se comprueba la
	// propiedad útil A*A^-1 ~ I.
	const auto prod3 = s3 * inv3;
	const m::Mat<3, float> id3 = m::Mat<3, float>::identity();
	float id_err = 0.0f;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			id_err = std::fmax(id_err, std::fabs(static_cast<float>(prod3.m[i][j]) - id3.m[i][j]));
	std::printf("  inversa 3x3: vs float %.2e ; |A*A^-1 - I|max %.4f\n", e_inv3, id_err);
	check(e_inv3 <= 2.0e-2f, "inversa 3x3 vs float <= 2e-2");
	check(id_err <= 0.05f, "A*A^-1 ~ I (abs <= 5e-2)");

	// Resolver A*x = b con x = A^-1 * b.
	const m::Vec<3, MF> bv = {sv<MF>(1.0f), sv<MF>(2.0f), sv<MF>(3.0f)};
	const float e_solve = max_rel_vec(vec_to_float(inv3 * bv), inverse3(s3f) * vec_to_float(bv));
	std::printf("  solve 3x3: vs float %.2e\n", e_solve);
	check(e_solve <= 2.0e-2f, "solve 3x3 vs float <= 2e-2");

	// Inversa 2x2.
	const m::Mat<2, MF> m2 = {{{sv<MF>(3.0f), sv<MF>(1.0f)}, {sv<MF>(2.0f), sv<MF>(4.0f)}}};
	const auto m2f = mat_to_float(m2);
	const float e_det2 = rel_err(static_cast<float>(det2(m2)), det2(m2f));
	const float e_inv2 = max_rel_mat(mat_to_float(inverse2(m2)), inverse2(m2f));
	const auto p2 = m2 * inverse2(m2);
	const m::Mat<2, float> i2 = m::Mat<2, float>::identity();
	float id2 = 0.0f;
	for (int i = 0; i < 2; ++i)
		for (int j = 0; j < 2; ++j)
			id2 = std::fmax(id2, std::fabs(static_cast<float>(p2.m[i][j]) - i2.m[i][j]));
	std::printf("  det2 rel %.2e ; inversa 2x2 vs float %.2e ; |A*A^-1 - I|max %.4f\n", e_det2,
		    e_inv2, id2);
	check(e_det2 <= 5.0e-3f, "det2: error relativo <= 5e-3");
	check(e_inv2 <= 1.0e-2f, "inversa 2x2 vs float <= 1e-2");
	check(id2 <= 0.01f, "A*A^-1 ~ I 2x2 (abs <= 1e-2)");

	// Afín: transformar un punto y componer dos transformaciones.
	m::Affine<3, MF, MF> aff = m::Affine<3, MF, MF>::identity();
	aff.m.m[0][0] = sv<MF>(1.3f);
	aff.m.m[0][1] = sv<MF>(-0.15f);
	aff.m.m[1][1] = sv<MF>(0.6f);
	aff.m.m[2][1] = sv<MF>(0.45f);
	aff.m.m[2][2] = sv<MF>(2.1f);
	aff.t = {sv<MF>(1.1f), sv<MF>(-1.9f), sv<MF>(0.4f)};
	m::Affine<3, MF, MF> aff2 = m::Affine<3, MF, MF>::identity();
	aff2.m.m[0][1] = sv<MF>(-0.55f);
	aff2.m.m[1][2] = sv<MF>(0.35f);
	aff2.t = {sv<MF>(-1.3f), sv<MF>(1.7f), sv<MF>(2.2f)};

	m::Affine<3, float, float> aff_f {}, aff2_f {};
	aff_f.m = mat_to_float(aff.m);
	for (int i = 0; i < 3; ++i) aff_f.t.v[i] = static_cast<float>(aff.t.v[i]);
	aff2_f.m = mat_to_float(aff2.m);
	for (int i = 0; i < 3; ++i) aff2_f.t.v[i] = static_cast<float>(aff2.t.v[i]);

	const float e_aff1 = affine_err(vec_to_float(m::transform(aff, v3)), aff_f.m, aff_f.t, vf);
	const auto comp_f = m::compose(aff_f, aff2_f);
	const float e_aff2 = affine_err(vec_to_float(m::transform(m::compose(aff, aff2), v3)), comp_f.m,
					comp_f.t, vf);
	std::printf("  afin transform %.2e ; compose %.2e\n", e_aff1, e_aff2);
	check(e_aff1 <= 5.0e-3f, "afin transform: error <= 5e-3");
	check(e_aff2 <= 5.0e-3f, "afin compose: error <= 5e-3");
}

// ---------------------------------------------------------------------------
//  4. Barrido aleatorio de 3x3 (muchos casos)
// ---------------------------------------------------------------------------

void test_random_matrices() {
	using MF = MiniFloat16;
	float worst_mul = 0.0f, worst_vec = 0.0f, worst_add = 0.0f;
	for (int trial = 0; trial < 500; ++trial) {
		const m::Mat<3, MF> a = rnd_mat<MF, 3>(-8.0f, 8.0f);
		const m::Mat<3, MF> b = rnd_mat<MF, 3>(-8.0f, 8.0f);
		const m::Vec<3, MF> v = rnd_vec<MF, 3>(-8.0f, 8.0f);
		const auto af = mat_to_float(a);
		const auto bf = mat_to_float(b);
		const auto vf = vec_to_float(v);
		worst_mul = std::fmax(worst_mul, mat_mul_err(mat_to_float(a * b), af, bf));
		worst_add = std::fmax(worst_add, mat_add_err(mat_to_float(a + b), af, bf));
		worst_vec = std::fmax(worst_vec, mat_vec_err(vec_to_float(a * v), af, vf));
	}
	std::printf("  500 3x3 aleatorias: * %.2e  + %.2e  M*v %.2e\n", worst_mul, worst_add, worst_vec);
	check(worst_mul <= 5.0e-3f, "3x3 aleatorio * <= 5e-3");
	check(worst_add <= 2.0e-3f, "3x3 aleatorio + <= 2e-3");
	check(worst_vec <= 5.0e-3f, "3x3 aleatorio M*v <= 5e-3");
}

// ---------------------------------------------------------------------------
//  5. Rasgos de escalar de la librería genérica
// ---------------------------------------------------------------------------

void test_scalar_traits() {
	using T = m::scalar_traits<MiniFloat16>;
	check(T::zero().raw == 0u, "scalar_traits::zero");
	check(T::one().raw == 0x3C00u, "scalar_traits::one == 1.0");
	check(T::from_int(3).raw == MiniFloat16(3.0f).raw, "from_int(3)");
	check(T::to_int(MiniFloat16(7.0f)) == 7, "to_int(7)");
	check(T::needs_normalize == false, "no necesita normalizar");
}

} // namespace

int main() {
	std::printf("== HOST-056 MiniFloat16 ==\n");
	test_format();
	test_exact_integers();
	test_scalar_sweep();
	test_matrices();
	test_random_matrices();
	test_scalar_traits();

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: MiniFloat16 y sus operaciones de matrices vs float dentro de tolerancia.\n");
	return 0;
}
