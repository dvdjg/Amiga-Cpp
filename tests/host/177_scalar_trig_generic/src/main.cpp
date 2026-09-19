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
//   bash tools/run-host-tests.sh tests/host/177_scalar_trig_generic

#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/scalar_math.hpp>
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

	if (fails) {
		std::printf("[FAIL] HOST-177: %d comprobacion(es)\n", fails);
		return 1;
	}
	std::printf("OK: trig escalar generica: misma funcion con float y q12 (desviacion^2 max %g).\n",
		    worst);
	return 0;
}
