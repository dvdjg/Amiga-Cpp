// ============================================================================
// Test HOST-115: steering behaviors (seek/flee/arrive y flocking)
// ============================================================================
//
// Valida `engine/include/eng/ai/steering/steering.hpp`:
//
//   1) seek/flee/arrive sobre vectores 2D.
//   2) separation/cohesion/alignment y la combinacion `flock`.
//   3) Generico sobre el escalar: `double` y `q12` (fixed) con fixed_math.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/115_steering

#include <cstdio>

#include <eng/ai/steering/steering.hpp>
#include <eng/core/fixed_math.hpp>
#include <eng/retro/fixed_q.hpp>

namespace {

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using SteerD = eng::math::Vec<2, double>;

[[nodiscard]] bool approx(double a, double b, double eps = 1e-6) {
	const double d = a - b;
	return (d < 0 ? -d : d) < eps;
}

void test_seek_flee_arrive() {
	const SteerD origin {0.0, 0.0};
	const SteerD target {3.0, 4.0};

	const SteerD s = eng::ai::seek(origin, target, 10.0);
	check(approx(s.v[0], 6.0) && approx(s.v[1], 8.0), "seek: (0.6, 0.8) * 10");

	const SteerD f = eng::ai::flee(origin, target, 10.0);
	check(approx(f.v[0], -6.0) && approx(f.v[1], -8.0), "flee: direccion opuesta");

	// Dentro de slow_radius frena proporcionalmente.
	const SteerD near = eng::ai::arrive(origin, SteerD {10.0, 0.0}, 10.0, 20.0);
	check(approx(near.v[0], 5.0) && approx(near.v[1], 0.0), "arrive: frena a mitad");
	// Fuera de slow_radius = velocidad maxima.
	const SteerD far = eng::ai::arrive(origin, SteerD {100.0, 0.0}, 10.0, 20.0);
	check(approx(far.v[0], 10.0), "arrive: lejos va a maxima");
	// En el objetivo, velocidad cero.
	const SteerD here = eng::ai::arrive(origin, origin, 10.0, 20.0);
	check(approx(here.v[0], 0.0) && approx(here.v[1], 0.0), "arrive: en el objetivo cero");
}

void test_flocking() {
	const SteerD self {0.0, 0.0};
	const SteerD neighbors_a[1] = {{1.0, 0.0}};
	const SteerD sep = eng::ai::separation(self, eng::Span<const SteerD> {neighbors_a, 1}, 5.0,
					       10.0);
	check(approx(sep.v[0], -10.0) && approx(sep.v[1], 0.0), "separation: aleja del vecino");

	const SteerD neighbors_b[2] = {{2.0, 0.0}, {4.0, 0.0}};
	const SteerD coh = eng::ai::cohesion(self, eng::Span<const SteerD> {neighbors_b, 2}, 10.0);
	check(approx(coh.v[0], 10.0), "cohesion: hacia el centroide");

	const SteerD vels[2] = {{10.0, 0.0}, {20.0, 0.0}};
	const SteerD ali = eng::ai::alignment(self, eng::Span<const SteerD> {vels, 2}, 10.0);
	check(approx(ali.v[0], 10.0), "alignment: hacia la velocidad media");

	// flock con pesos 1 combina los tres: (-1,0) + (1,0) + (1,0) = (1,0).
	const SteerD self_vel {0.0, 0.0};
	const eng::ai::FlockWeights<double> w {1.0, 1.0, 1.0};
	const SteerD total = eng::ai::flock(self, self_vel,
					    eng::Span<const SteerD> {neighbors_a, 1},
					    eng::Span<const SteerD> {vels, 1}, 5.0, w);
	check(approx(total.v[0], 1.0) && approx(total.v[1], 0.0), "flock: suma ponderada");
}

void test_q12() {
	using SteerQ = eng::math::Vec<2, eng::retro::q12>;
	const eng::retro::q12 zero {0};
	const eng::retro::q12 one {4096}; // 1.0 en q12
	const SteerQ pos {zero, zero};
	// Vector pequeno: `length_sq` en q12 desborda si las componentes son grandes.
	const SteerQ target {eng::retro::q12 {1536}, eng::retro::q12 {2048}}; // (0.375, 0.5)
	const SteerQ v = eng::ai::seek(pos, target, one);
	const double vx = v.v[0].v / 4096.0;
	const double vy = v.v[1].v / 4096.0;
	const double len = eng::math::length(v).v / 4096.0;
	check(vx > 0.0 && vy > 0.0, "q12: seek apunta al objetivo");
	check(approx(len, 1.0, 0.02), "q12: la magnitud es max_speed");
}

} // namespace

int main() {
	std::printf("Steering:\n");
	test_seek_flee_arrive();
	test_flocking();
	test_q12();

	if (g_fail == 0u) {
		std::printf("OK: Steering (seek/flee/arrive, flocking, generico double/q12)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
