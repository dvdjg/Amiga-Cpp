// HOST-052 — la librería es AGNÓSTICA del escalar.
// El MISMO Vec/Mat/Affine y las MISMAS operaciones funcionan con: 4.12, 8.8, float
// (FPU/emulación) y un escalar de usuario (complejo de dos fix, definido aquí mismo).
#include <eng/core/linalg.hpp>
#include <eng/core/math2d.hpp>

#include <cstdio>
#include <eng/retro/fixed_q.hpp>

// (1) Escalar de USUARIO: complejo de dos fix. La librería no sabe nada de él.
struct Cpx {
	eng::s16 re;
	eng::s16 im;
};
constexpr Cpx operator+(Cpx a, Cpx b) {
	return Cpx {static_cast<eng::s16>(a.re + b.re), static_cast<eng::s16>(a.im + b.im)};
}
constexpr Cpx operator-(Cpx a) { return Cpx {static_cast<eng::s16>(-a.re), static_cast<eng::s16>(-a.im)}; }
constexpr Cpx operator*(Cpx a, Cpx b) {
	const eng::s32 p0 = eng::math2d::mul16(a.re, b.re);
	const eng::s32 p1 = eng::math2d::mul16(a.im, b.im);
	const eng::s32 p2 = eng::math2d::mul16(a.re, b.im);
	const eng::s32 p3 = eng::math2d::mul16(a.im, b.re);
	return Cpx {static_cast<eng::s16>((p0 - p1) >> 12), static_cast<eng::s16>((p2 + p3) >> 12)};
}
constexpr bool operator==(Cpx a, Cpx b) { return a.re == b.re && a.im == b.im; }

namespace eng::math {
// Lo único que hay que decirle a la librería: cómo normalizar un producto y cuál es el
// producto interno (para un complejo, con el conjugado). El resto lo pone el primario.
template <>
struct scalar_traits<Cpx> {
	using scalar = Cpx;
	static constexpr Cpx inner(Cpx a, Cpx b) { return a * Cpx {b.re, static_cast<eng::s16>(-b.im)}; }
	static constexpr Cpx zero() { return Cpx {0, 0}; }
	static constexpr Cpx one() { return Cpx {4096, 0}; }
	template <typename Prod>
	static constexpr Cpx norm_from(Prod p) {
		return static_cast<Cpx>(p);
	}
	static constexpr bool needs_normalize = false;
};
} // namespace eng::math

using namespace eng::math;
using namespace eng::retro;
using eng::s16;

int main() {
	// El MISMO Mat<2,S>, con cuatro escalares distintos (incluido uno de usuario).
	const Mat<2, Fixed<s16, 12>> m412 = Mat<2, Fixed<s16, 12>>::identity();
	const Mat<2, Fixed<s16, 8>> m88 = Mat<2, Fixed<s16, 8>>::identity();
	const Mat<2, float> mflt = Mat<2, float>::identity();
	const Mat<2, Cpx> mcpx = Mat<2, Cpx>::identity();

	const bool ok412 = (m412 * m412).m[0][0] == q12 {4096};
	const bool ok88 = (m88 * m88).m[1][1] == Fixed<s16, 8> {256};
	const bool okflt = (mflt * mflt).m[0][0] == 1.0f;
	const bool okcpx = (mcpx * mcpx).m[0][0] == Cpx {4096, 0};

	// Vector + producto interno con complejo: usa el conjugado (i * conj(i) = 1).
	const Vec<2, Cpx> v {{Cpx {4096, 0}, Cpx {0, 4096}}}; // (1, i)
	const Cpx ip = dot(v, v);                             // 1 + 1 = 2
	const bool okdot = ip == Cpx {8192, 0};

	if (ok412 && ok88 && okflt && okcpx && okdot) {
		std::printf("OK: la libreria funciona con 4.12, 8.8, float y un complejo de usuario.\n");
		return 0;
	}
	std::printf("FAIL: 412=%d 88=%d float=%d cpx=%d dot=%d\n", ok412, ok88, okflt, okcpx, okdot);
	return 1;
}
