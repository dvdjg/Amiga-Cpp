// ============================================================================
// Test HOST-177: trig escalar GENERICA (el mismo algoritmo con float y con Fixed).
// ============================================================================
//
// Demuestra el patron del motor: el algoritmo es una plantilla sobre el escalar `S` y
// las operaciones concretas (seno, coseno, producto normalizado) las aporta el propio
// escalar. Aqui la MISMA funcion `rotate` se instancia con `float` y con `q12`
// (`Fixed<s16,12>`), se normalizan ambos resultados a double y se mide la desviacion de
// la version fixed. El angulo va en radianes en los dos casos.
//
//   bash tools/run-host-tests.sh tests/host/core/177_scalar_trig_generic

#include <eng/core/math/fixed.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/retro/fixed_trig.hpp>

#include <cstdio>

namespace {

template <typename S>
struct Vec2 {
	S x;
	S y;
};

/// Algoritmo unico: no nombra ningun tipo concreto, solo el escalar `S`.
template <typename S>
[[nodiscard]] constexpr Vec2<S> rotate(const Vec2<S>& v, S angle) {
	const S c = eng::math::scalar_cos<S>::op(angle);
	const S s = eng::math::scalar_sin<S>::op(angle);
	return {eng::math::mul_norm(c, v.x) - eng::math::mul_norm(s, v.y),
		eng::math::mul_norm(s, v.x) + eng::math::mul_norm(c, v.y)};
}

/// Normaliza cualquier escalar a double (para comparar float y fixed en un marco comun).
template <typename S>
[[nodiscard]] double as_double(S v) {
	return static_cast<double>(v);
}
template <typename R, int E, typename P>
[[nodiscard]] double as_double(eng::math::Fixed<R, E, P> v) {
	return static_cast<double>(v.v) / static_cast<double>(1 << E);
}

using eng::retro::q12;

[[nodiscard]] double err_at(double angle) {
	const Vec2<float> vf {1.0f, 0.5f};
	const Vec2<q12> vq {eng::math::scalar_const<q12>::from(1.0),
			    eng::math::scalar_const<q12>::from(0.5)};
	const Vec2<float> rf = rotate(vf, eng::math::scalar_const<float>::from(angle));
	const Vec2<q12> rq = rotate(vq, eng::math::scalar_const<q12>::from(angle));
	const double dx = as_double(rq.x) - as_double(rf.x);
	const double dy = as_double(rq.y) - as_double(rf.y);
	return dx * dx + dy * dy;
}

} // namespace

int main() {
	int fails = 0;

	// 1) El mismo algoritmo compila e instancia con float y con q12 (intercambiables).
	const double kAngles[] = {0.0, 0.3, 0.7853981634, 1.5707963268, 2.5, 3.0,
				  4.712389, 6.0, -1.0, -3.5};
	double worst = 0.0;
	for (double a : kAngles) {
		const double e = err_at(a);
		if (e > worst) worst = e;
	}
	// La tabla 4.12 tiene 4096 pasos (cuantizacion ~2.4e-4 por unidad) mas el redondeo de
	// `mul_norm`; el umbral deja margen sobre el radio del vector (|v| ~ 1.12).
	if (worst > 0.01) {
		std::printf("[FAIL] desviacion q12 vs float: %g (umbral 0.01)\n", worst);
		++fails;
	}

	// 2) La especializacion retro usa la tabla exacta del original.
	{
		const q12 half_pi = eng::math::scalar_const<q12>::from(1.5707963268);
		if (eng::math::scalar_sin<q12>::op(q12 {0}).v != 0 ||
		    eng::math::scalar_sin<q12>::op(half_pi).v != eng::retro::kSinQ12[1024] ||
		    eng::math::scalar_cos<q12>::op(q12 {0}).v != eng::retro::kSinQ12[1024]) {
			std::printf("[FAIL] la especializacion retro no usa kSinTab\n");
			++fails;
		}
	}

	// 3) `sincos` en una pasada coincide con `sin`/`cos` por separado.
	{
		const q12 half_pi = eng::math::scalar_const<q12>::from(1.5707963268);
		q12 s {};
		q12 c {};
		eng::math::scalar_sincos<q12>::op(half_pi, s, c);
		if (s.v != eng::math::scalar_sin<q12>::op(half_pi).v ||
		    c.v != eng::math::scalar_cos<q12>::op(half_pi).v) {
			std::printf("[FAIL] sincos != sin/cos\n");
			++fails;
		}
	}

	// 4) El angulo lleva la unidad en el TIPO (`Angle<S, Unit>`): un mismo `sin` sirve
	//    para grados, vueltas y radianes sin funciones con el formato en el nombre.
	{
		using eng::math::Angle;
		using namespace eng::math::angle;
		const float s90 = eng::math::sin(Angle<float, degrees> {90.0f});
		const float c_turn = eng::math::cos(Angle<float, turns> {0.25f}); // 1/4 vuelta
		const float r = eng::math::sin(Angle<float, radians> {1.5707963268f});
		if (s90 < 0.999f || s90 > 1.001f || c_turn > 0.001f || c_turn < -0.001f || r < 0.999f ||
		    r > 1.001f) {
			std::printf("[FAIL] Angle<degrees/turns/radians> (%.4f %.4f %.4f)\n", s90, c_turn, r);
			++fails;
		}
		// `turns` sobre q12 va a la tabla exacta (índice == crudo del 4.12).
		if (eng::retro::sin(eng::retro::turns(1024)).v != eng::retro::kSinQ12[1024] ||
		    eng::retro::cos(eng::retro::turns(0)).v != eng::retro::kSinQ12[1024]) {
			std::printf("[FAIL] turns/q12 no usa kSinTab\n");
			++fails;
		}
	}

	// 5) Aritmetica de `Angle` (misma unidad), `wrap` a (-pi, pi] y `sincos3` compartido.
	{
		using eng::math::Angle;
		using namespace eng::math::angle;
		const Angle<float, radians> a1 {0.5f};
		const Angle<float, radians> a2 {0.25f};
		const float sum = (a1 + a2).value;
		const float dif = (a1 - a2).value;
		const float neg = (-a1).value;
		if (sum < 0.749f || sum > 0.751f || dif < 0.249f || dif > 0.251f || neg != -0.5f) {
			std::printf("[FAIL] Angle +,-,neg (%.4f %.4f %.4f)\n", sum, dif, neg);
			++fails;
		}
		const float w = eng::math::wrap(Angle<float, radians> {7.0f}).value; // 7 - 2pi ~ 0.7168
		if (w < 0.70f || w > 0.73f) {
			std::printf("[FAIL] wrap(7) = %g\n", static_cast<double>(w));
			++fails;
		}
		// `sincos3` == sin/cos por eje, y `load_rotate_from_sincos` == `load_rotate`.
		const q12 x = eng::retro::angle_to_radians(300).value;
		const q12 y = eng::retro::angle_to_radians(700).value;
		const q12 z = eng::retro::angle_to_radians(1100).value;
		const auto sc = eng::math3d::sincos3(eng::retro::radians(x), eng::retro::radians(y),
						     eng::retro::radians(z));
		if (sc.sinX.v != eng::math::scalar_sin<q12>::op(x).v ||
		    sc.cosZ.v != eng::math::scalar_cos<q12>::op(z).v) {
			std::printf("[FAIL] sincos3 != sin/cos\n");
			++fails;
		}
		eng::math::Mat<3, q12> m1 {};
		eng::math::Mat<3, q12> m2 {};
		eng::math3d::load_rotate_from_sincos(m1, sc);
		eng::math3d::load_rotate(m2, eng::retro::radians(x), eng::retro::radians(y),
					 eng::retro::radians(z));
		bool eq = true;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (m1.m[i][j].v != m2.m[i][j].v) eq = false;
		if (!eq) {
			std::printf("[FAIL] load_rotate_from_sincos != load_rotate\n");
			++fails;
		}
		// La inversa con `sincos(-a) = (-sin, cos)` == `load_reverse_rotate(-a)`.
		eng::math::Mat<3, q12> m3 {};
		eng::math::Mat<3, q12> m4 {};
		const decltype(sc) scn {-sc.sinX, sc.cosX, -sc.sinY, sc.cosY, -sc.sinZ, sc.cosZ};
		eng::math3d::load_reverse_rotate_from_sincos(m3, scn);
		eng::math3d::load_reverse_rotate(m4, eng::retro::radians(-x), eng::retro::radians(-y),
						 eng::retro::radians(-z));
		bool eqr = true;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (m3.m[i][j].v != m4.m[i][j].v) eqr = false;
		if (!eqr) {
			std::printf("[FAIL] reverse con sincos(-a) != load_reverse_rotate(-a)\n");
			++fails;
		}
		// `angle_diff` da el delta mas corto (6 rad -> 6 - 2pi ~ -0.283).
		const float dd = eng::math::angle_diff(Angle<float, radians> {3.0f},
						       Angle<float, radians> {-3.0f})
					 .value;
		if (dd < -0.29f || dd > -0.27f) {
			std::printf("[FAIL] angle_diff(3,-3) = %g\n", static_cast<double>(dd));
			++fails;
		}
	}

	if (fails) {
		std::printf("[FAIL] HOST-177: %d comprobacion(es)\n", fails);
		return 1;
	}
	std::printf("OK: trig escalar generica: misma funcion con float y q12 (desviacion^2 max %g).\n",
		    worst);
	return 0;
}
