// ============================================================================
// Test HOST-489: colisión 3D mínima (esfera/AABB/rayo) con float y Fixed.
// ============================================================================
#include <eng/core/math/fixed.hpp>
#include <eng/core/math/fixed_math.hpp>
#include <eng/core/util/collision3d.hpp>

#include <cstdio>

using namespace eng;
using namespace eng::collision3d;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

template <class S>
[[nodiscard]] eng::math::Vec<3, S> v3(int x, int y, int z) {
	using T = eng::math::scalar_traits<S>;
	eng::math::Vec<3, S> r {}; r.v[0] = T::from_int(x); r.v[1] = T::from_int(y); r.v[2] = T::from_int(z); return r;
}

template <class S>
void test_scalar(const char* label) {
	using T = eng::math::scalar_traits<S>;
	const S zero = T::zero();
	const S one = T::one();
	const S two = T::from_int(2);
	const S four = T::from_int(4);
	const S ten = T::from_int(10);

	const Sphere3<S> a {v3<S>(0, 0, 0), two};
	const Sphere3<S> b {v3<S>(1, 0, 0), two};
	const Sphere3<S> c {v3<S>(5, 0, 0), two};
	check(sphere_overlap(a, b), label);
	check(!sphere_overlap(a, c), label);

	const Aabb3<S> box {v3<S>(1, -1, -1), v3<S>(2, 1, 1)};
	const Sphere3<S> s0 {v3<S>(0, 0, 0), one};
	const Sphere3<S> s1 {v3<S>(-1, 0, 0), one};
	check(sphere_aabb(s0, box), label);
	check(!sphere_aabb(s1, box), label);
	check(aabb_overlap(box, Aabb3<S> {v3<S>(1, 0, 0), v3<S>(3, 2, 2)}), label);
	check(!aabb_overlap(box, Aabb3<S> {v3<S>(3, 0, 0), v3<S>(4, 2, 2)}), label);

	Ray3<S> ray {v3<S>(-2, 0, 0), v3<S>(1, 0, 0), ten};
	RayHit3<S> hit {};
	check(ray_aabb(ray, Aabb3<S> {v3<S>(-1, -1, -1), v3<S>(1, 1, 1)}, hit), label);
	check(hit.hit && T::to_int(hit.t) == 1, label);
	check(T::to_int(hit.point.v[0]) == -1, label);
	check(T::to_int(hit.normal.v[0]) == -1, label);

	Ray3<S> miss {v3<S>(-2, 3, 0), v3<S>(1, 0, 0), ten};
	RayHit3<S> no_hit {};
	check(!ray_aabb(miss, Aabb3<S> {v3<S>(-1, -1, -1), v3<S>(1, 1, 1)}, no_hit), label);

	Ray3<S> rs {v3<S>(-5, 0, 0), v3<S>(1, 0, 0), ten};
	RayHit3<S> sh {};
	check(ray_sphere(rs, Sphere3<S> {v3<S>(0, 0, 0), one}, sh), label);
	check(sh.hit && T::to_int(sh.t) == 4, label);
	check(T::to_int(sh.point.v[0]) == -1, label);
	check(T::to_int(sh.normal.v[0]) == -1, label);
	check(!ray_sphere(Ray3<S> {v3<S>(-5, 3, 0), v3<S>(1, 0, 0), ten},
			  Sphere3<S> {v3<S>(0, 0, 0), one}, no_hit),
	      label);
	(void)four;
	(void)zero;
}

} // namespace

int main() {
	test_scalar<float>("float");
	using Q4 = eng::math::Fixed<eng::s16, 4>;
	test_scalar<Q4>("Fixed<s16,4>");
	if (g_fail == 0) {
		std::printf("OK: HOST-489 colision 3D (esfera/AABB/rayo) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones.\n", g_fail);
	return 1;
}
