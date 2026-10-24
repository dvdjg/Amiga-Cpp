// ============================================================================
// Test HOST-493: animación rígida 3D (clips, interpolación, loop, eventos).
// ============================================================================
#include <eng/core/math/fixed.hpp>
#include <eng/scene3d/animation3d.hpp>

#include <cmath>
#include <cstdio>

using namespace eng;
using namespace eng::anim3d;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

math3d::Vec3t<eng::coord> p3(int x, int y, int z) {
	math3d::Vec3t<eng::coord> r {};
	r.v[0] = math::scalar_traits<eng::coord>::from_int(x);
	r.v[1] = math::scalar_traits<eng::coord>::from_int(y);
	r.v[2] = math::scalar_traits<eng::coord>::from_int(z);
	return r;
}

math3d::Vec3t<eng::real> r3(float x, float y, float z) {
	return math3d::Vec3t<eng::real> {x, y, z};
}

void test_interpolation_and_events() {
	const Keyframe keys[] = {
		{0, p3(0, 0, 0), r3(0.10f, 0.0f, 0.0f)},
		{10, p3(10, 20, 0), r3(0.30f, 0.0f, 0.0f)},
	};
	const Track tracks[] = {{0, 0, 2}};
	const Event events[] = {{5, 7}};
	const Clip clip {{keys, 2}, {tracks, 1}, {events, 1}, 10, true};

	Player<1, 4> p;
	p.play(clip);
	check(p.playing(), "clip activo");
	check(p.tick() == 0u, "clip empieza en tick 0");

	p.update(5);
	Pose pose;
	check(p.sample(0, pose), "muestrea track 0");
	check(pose.translation.v[0] == 5, "traslación lineal en el punto medio");
	check(pose.translation.v[1] == 10, "traslación Y lineal");
	check(std::fabs(static_cast<float>(pose.rotation.v[0]) - 0.20f) < 0.01f,
	      "rotación lineal en el punto medio");
	check(p.pending_events() == 1u, "evento cruzado");
	u16 id = 0;
	check(p.take_event(id) && id == 7u, "evento correcto");
	check(!p.take_event(id), "evento consumido una sola vez");
}

void test_loop_and_oneshot() {
	const Keyframe keys[] = {
		{0, p3(0, 0, 0), r3(0.0f, 0.0f, 0.0f)},
		{10, p3(10, 0, 0), r3(0.0f, 0.0f, 0.0f)},
	};
	const Track tracks[] = {{0, 0, 2}};
	Clip clip {{keys, 2}, {tracks, 1}, {}, 10, true};

	Player<1> p;
	p.play(clip);
	p.update(14);
	check(p.tick() == 4u, "loop vuelve a 4");

	clip.loop = false;
	p.play(clip);
	p.update(20);
	check(p.tick() == 10u, "one-shot se queda al final");
	Pose pose;
	check(p.sample(0, pose), "one-shot muestrea el último key");
	check(pose.translation.v[0] == 10, "one-shot conserva el valor final");
}

void test_shortest_rotation() {
	const Keyframe keys[] = {
		{0, p3(0, 0, 0), r3(0.90f, 0.0f, 0.0f)},
		{10, p3(0, 0, 0), r3(0.10f, 0.0f, 0.0f)},
	};
	const Track tracks[] = {{0, 0, 2}};
	const Clip clip {{keys, 2}, {tracks, 1}, {}, 10, true};

	Player<1> p;
	p.play(clip);
	p.update(5);
	Pose pose;
	check(p.sample(0, pose), "muestrea giro corto");
	const float r = static_cast<float>(pose.rotation.v[0]);
	check(std::fabs(r) < 0.01f || std::fabs(r - 1.0f) < 0.01f,
	      "interpola 0.9 -> 0.1 por el camino corto");
}

void test_ratio_q12() {
	using Q12 = eng::math::Fixed<s16, 12>;
	check(RatioQ12<Q12>::get(0u).v == 0, "ratio 0");
	check(RatioQ12<Q12>::get(2048u).v == 2048, "ratio 0.5 en Q12");
	check(RatioQ12<Q12>::get(4096u).v == 4096, "ratio 1.0 en Q12");
}
void test_rotation_matrix() {
	const auto m = rotation_from_turns(r3(0.0f, 0.0f, 0.0f));
	check(std::fabs(m.m[0][0] - 1.0f) < 0.001f, "matriz identidad m00");
	check(std::fabs(m.m[1][1] - 1.0f) < 0.001f, "matriz identidad m11");
	check(std::fabs(m.m[2][2] - 1.0f) < 0.001f, "matriz identidad m22");
}

void test_limits() {
	Player<1> empty;
	Pose pose;
	check(!empty.playing(), "player vacío");
	check(!empty.sample(0, pose), "sin clip no muestrea");
	check(!empty.update(1), "sin clip no avanza");

	const Keyframe keys[] = {{0, p3(1, 2, 3), r3(0.0f, 0.0f, 0.0f)}};
	const Track tracks[] = {{0, 0, 1}};
	const Clip one {{keys, 1}, {tracks, 1}, {}, 1, false};
	Player<1> p;
	p.play(one);
	p.update(1);
	check(p.sample(0, pose), "track de un keyframe");
	check(pose.translation.v[2] == 3, "track de un keyframe conserva valor");
	check(!p.sample(1, pose), "track fuera de rango rechazado");
}

} // namespace

int main() {
	test_interpolation_and_events();
	test_loop_and_oneshot();
	test_shortest_rotation();
	test_ratio_q12();
	test_rotation_matrix();
	test_limits();
	if (g_fail == 0) std::printf("[PASS] HOST-493 animation3d\n");
	return g_fail == 0 ? 0 : 1;
}
