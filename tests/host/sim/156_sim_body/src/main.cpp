// ============================================================================
// Test HOST-156: cuerpo procedural (cadena + IK + postura expresiva)
// ============================================================================
//
// Valida `engine/include/eng/sim/body.hpp`:
//
//   1) `ChainBody` genérico sobre el escalar: se endereza con `reset`, resuelve IK con
//      `solve` (la punta alcanza el objetivo conservando la longitud de los segmentos) y
//      se recta hacia el objetivo cuando está fuera de alcance.
//   2) `pose_from_behavior`: cada conducta tiene una postura base.
//   3) `pose_from_state`: el afecto modula la postura (miedo agacha/retrocede, ira
//      inclina, alegría menea la cola, vínculo negativo tensa).
//   4) `apply_pose`: la postura deforma la cadena (agacharse baja la Y).
//   5) Genérico sobre `double` y `q12` (fixed), sin heap.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/156_sim_body

#include <cstdio>

#include <eng/core/math/fixed_math.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/sim/body.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using BodyD = ChainBody<double, 4>;

[[nodiscard]] bool near(double a, double b, double eps = 1e-6) {
	const double d = a - b;
	return (d < 0 ? -d : d) < eps;
}

void test_reset_solve() {
	BodyD body;
	body.reset({0.0, 0.0}, 1.0);
	check(near(body.joint(3).v[0], 3.0) && near(body.joint(3).v[1], 0.0),
	      "body: reset endereza la cadena");

	// Objetivo alcanzable: la punta llega y los segmentos conservan su longitud.
	body.solve({0.0, 0.0}, {2.0, 1.0}, 16);
	check(near(body.tail().v[0], 2.0, 1e-3) && near(body.tail().v[1], 1.0, 1e-3),
	      "body: la punta alcanza el objetivo");
	bool lengths_ok = true;
	for (eng::u8 i = 1; i < 4u; ++i) {
		const double dx = body.joint(i).v[0] - body.joint(i - 1u).v[0];
		const double dy = body.joint(i).v[1] - body.joint(i - 1u).v[1];
		const double len = dx * dx + dy * dy;
		if (!near(len, 1.0, 1e-3)) {
			lengths_ok = false;
		}
	}
	check(lengths_ok, "body: los segmentos conservan su longitud (IK)");
	check(near(body.head().v[0], 0.0) && near(body.head().v[1], 0.0),
	      "body: la raiz queda fija");

	// Objetivo fuera de alcance: cadena recta hacia el objetivo.
	body.solve({0.0, 0.0}, {100.0, 0.0}, 16);
	check(near(body.tail().v[0], 3.0, 1e-3), "body: fuera de alcance se estira");
}

void test_pose() {
	const BodyPose idle = pose_from_behavior(Behavior::Idle);
	const BodyPose hunt = pose_from_behavior(Behavior::Hunt);
	const BodyPose court = pose_from_behavior(Behavior::Court);
	const BodyPose submit = pose_from_behavior(Behavior::Submit);
	check(hunt.lean > idle.lean && hunt.reach > 0, "pose: cazar se inclina y estira");
	check(court.wag > 0, "pose: cortejar menea la cola");
	check(submit.crouch > 0 && submit.head_pitch > 0, "pose: someterse se agacha");

	Mind fearful;
	fearful.emotions.fear = 255u;
	const BodyPose scared = pose_from_state(Behavior::Idle, fearful, 0);
	check(scared.crouch > idle.crouch && scared.recoil > 0,
	      "pose: el miedo agacha y retrocede");

	Mind angry;
	angry.emotions.anger = 255u;
	const BodyPose furious = pose_from_state(Behavior::Idle, angry, 0);
	check(furious.lean > idle.lean, "pose: la ira inclina hacia delante");

	// Vinculo negativo (enemigo) tensa la postura (mente neutra para aislar el vinculo).
	Mind neutral;
	neutral.emotions.fear = 128u;
	neutral.emotions.anger = 128u;
	neutral.emotions.joy = 128u;
	neutral.emotions.compassion = 128u;
	const BodyPose neutral_pose = pose_from_state(Behavior::Idle, neutral, 0);
	const BodyPose hostile = pose_from_state(Behavior::Idle, neutral, -90);
	check(hostile.lean > neutral_pose.lean && hostile.head_pitch < neutral_pose.head_pitch,
	      "pose: un enemigo cercano tensa la postura");
}

void test_apply() {
	BodyD body;
	body.reset({0.0, 0.0}, 1.0);
	const double before = body.joint(0).v[1];
	BodyPose crouch {};
	crouch.crouch = 100;
	body.apply_pose(crouch, 1.0);
	check(body.joint(0).v[1] < before, "body: agacharse baja la Y");
}

void test_q12() {
	using Q = eng::retro::q12;
	using VQ = eng::math::Vec<2, Q>;
	ChainBody<Q, 3> body;
	body.reset(VQ {Q {0}, Q {0}}, Q {1024}); // segmento 0.25
	const VQ root {Q {0}, Q {0}};
	const VQ target {Q {2048}, Q {0}}; // 0.5 en X
	body.solve(root, target, 12);
	const Q dist = eng::math::length(body.tail() - target);
	check(dist.v < 256, "body q12: la punta alcanza el objetivo");
}

} // namespace

int main() {
	std::printf("Sim body:\n");
	test_reset_solve();
	test_pose();
	test_apply();
	test_q12();

	if (g_fail == 0u) {
		std::printf("OK: Sim body (cadena, IK, postura, generico double/q12)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
