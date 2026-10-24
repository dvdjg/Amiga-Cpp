// ============================================================================
// Test HOST-490: física 3D arcade mínima (esferas, suelo, impulso, sleep, time scale).
// ============================================================================
#include <eng/core/math/fixed.hpp>
#include <eng/core/math/fixed_math.hpp>
#include <eng/sim/physics3d.hpp>

#include <cmath>
#include <cstdio>

using namespace eng;
using namespace eng::sim3d;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

template <class S>
[[nodiscard]] eng::math::Vec<3, S> p3(int x, int y, int z) {
	using T = eng::math::scalar_traits<S>;
	eng::math::Vec<3, S> r {}; r.v[0] = T::from_int(x); r.v[1] = T::from_int(y); r.v[2] = T::from_int(z); return r;
}

void test_float() {
	using W = PhysicsWorld3D<float, 4>;
	W w;
	w.set_ground(0.0f);
	w.set_gravity(-10.0f);
	w.set_restitution(0.0f);
	w.set_sleep_threshold(0.01f);

	Body3D<float> falling {.position = p3<float>(0, 5, 0), .radius = 1.0f,
			       .inv_mass = 1.0f};
	const u16 fi = w.add(falling);
	check(fi == 0u, "add esfera que cae");
	for (int i = 0; i < 30; ++i) w.step(0.05f);
	check(w.body(fi).position.v[1] >= 0.99f, "esfera no atraviesa el suelo");
	check(w.body(fi).position.v[1] <= 1.05f, "esfera descansa sobre el suelo");
	check(w.body(fi).sleeping, "esfera se duerme");

	Body3D<float> static_body {.position = p3<float>(10, 5, 0), .radius = 1.0f,
				   .inv_mass = 0.0f};
	const u16 si = w.add(static_body);
	for (int i = 0; i < 10; ++i) w.step(0.05f);
	check(w.body(si).position.v[1] == 5.0f, "cuerpo estatico no se mueve");

	W pair;
	pair.set_ground(-10.0f);
	pair.set_gravity(0.0f);
	const u16 a = pair.add({.position = p3<float>(0, 0, 0),
				.velocity = p3<float>(1, 0, 0), .radius = 1.0f,
				.inv_mass = 1.0f});
	const u16 b = pair.add({.position = p3<float>(1, 0, 0),
				.velocity = p3<float>(-1, 0, 0), .radius = 1.0f,
				.inv_mass = 1.0f});
	pair.step(0.05f);
	const float dx = pair.body(b).position.v[0] - pair.body(a).position.v[0];
	check(dx > 1.9f, "esferas se separan tras el contacto");
	check(pair.body(a).velocity.v[0] < 0.0f, "esfera A rebota");
	check(pair.body(b).velocity.v[0] > 0.0f, "esfera B rebota");

	W frozen;
	frozen.set_ground(0.0f);
	frozen.set_time_scale(0.0f);
	const u16 zi = frozen.add({.position = p3<float>(0, 5, 0), .radius = 1.0f,
				   .inv_mass = 1.0f});
	frozen.step(0.5f);
	check(frozen.body(zi).position.v[1] == 5.0f, "time_scale 0 congela");
}

void test_fixed() {
	using Q4 = eng::math::Fixed<eng::s16, 8>;
	using T = eng::math::scalar_traits<Q4>;
	using W = PhysicsWorld3D<Q4, 2>;
	W w;
	w.set_ground(T::zero());
	w.set_gravity(T::from_int(-10));
	w.set_restitution(T::zero());
	w.set_sleep_threshold(eng::math::div_norm(T::one(), T::from_int(16)));
	const u16 i = w.add({.position = p3<Q4>(0, 5, 0), .radius = T::one(),
			     .inv_mass = T::one()});
	for (int k = 0; k < 40; ++k) {
		w.step(eng::math::div_norm(T::one(), T::from_int(20)));
	}
	const int y = T::to_int(w.body(i).position.v[1]);
	check(y == 1, "Fixed: esfera descansa en y=1");
	check(w.body(i).sleeping, "Fixed: esfera se duerme");
}

} // namespace

int main() {
	test_float();
	test_fixed();
	if (g_fail == 0) {
		std::printf("OK: HOST-490 fisica 3D arcade validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones.\n", g_fail);
	return 1;
}
