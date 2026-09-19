#define ENG_SCALAR_RETRO16  // host: instancia retro (eng::real=q12, coord=q0)
// HOST-055 — inversa de una transformación rígida (`math3d::inverse_rigid`).
//
// La parte lineal ortonormal se invierte trasponiendo y la traslación con `-mT·t`, sin
// divisiones. El test comprueba `compose(a, inverse_rigid(a)) ~ identidad` y que la doble
// inversa devuelve la original.
#include <eng/platform/amiga/gfx3d.hpp>

#include <cstdio>

using eng::s16;
using eng::u16;
using eng::math::compose;
using eng::retro::q0;
using namespace eng::math3d;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int main() {
	const u16 rot[][3] = {
		{0, 0, 0}, {1024, 0, 0}, {0, 2048, 0}, {300, 700, 1100}, {4095, 1, 2047}, {2048, 1024, 512},
	};

	bool all_identity = true;
	bool all_twice = true;
	const s16 txs[] = {-1000, 1000};
	for (const auto& r : rot) {
		for (const s16 tx : txs) {
			Affine3<> a {};
			load_rotate(a.m, eng::retro::turns(r[0]), eng::retro::turns(r[1]),
				    eng::retro::turns(r[2]));
			a.t = P3<> {{q0 {tx}, q0 {static_cast<s16>(-200)}, q0 {300}}};

			const Affine3<> ai = inverse_rigid(a);
			const Affine3<> c = compose(a, ai);
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					const s16 want = (i == j) ? 4096 : 0;
					if (c.m.m[i][j].v < want - 8 || c.m.m[i][j].v > want + 8) {
						all_identity = false;
					}
				}
			}
			if (c.t.x().v < -4 || c.t.x().v > 4 || c.t.y().v < -4 || c.t.y().v > 4 ||
			    c.t.z().v < -4 || c.t.z().v > 4) {
				all_identity = false;
			}

			const Affine3<> back = inverse_rigid(ai);
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					if (back.m.m[i][j].v != a.m.m[i][j].v) all_twice = false;
				}
			}
			const s16 bt[3] = {back.t.x().v, back.t.y().v, back.t.z().v};
			const s16 at[3] = {a.t.x().v, a.t.y().v, a.t.z().v};
			for (int k = 0; k < 3; ++k) {
				if (bt[k] < at[k] - 4 || bt[k] > at[k] + 4) all_twice = false;
			}
		}
	}
	check(all_identity, "compose(a, inverse_rigid(a)) ~ identidad");
	check(all_twice, "inverse_rigid(inverse_rigid(a)) ~ a");

	if (failures == 0) {
		std::printf("OK: inverse_rigid (inv. de una transformacion rigida) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
