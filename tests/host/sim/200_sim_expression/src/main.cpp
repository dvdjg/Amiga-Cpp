// ============================================================================
// Test HOST-200: expresion no verbal y fuga (eng::sim)
// ============================================================================
//
// Valida `eng/sim/expression.hpp`: canales, propiedades por gesto (control/deteccion),
// compostura efectiva y el calculo de la fuga, incluidos los canales autonomicos que
// delatan siempre y las microexpresiones.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/200_sim_expression

#include <cstdio>

#include <eng/sim/expression.hpp>
#include <eng/sim/mind.hpp>

namespace {

using namespace eng::sim;
using eng::u8;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

Mind make_mind(u8 fear, u8 anger, u8 joy) {
	Mind m;
	m.emotions.fear = fear;
	m.emotions.anger = anger;
	m.emotions.joy = joy;
	return m;
}

void test_gesture_table() {
	// Los canales autonomicos tienen control bajo; los volitivos, mas alto.
	check(gesture_def(GestureKind::PupilDilate).control < 15u, "gesto: pupilas poco controlables");
	check(gesture_def(GestureKind::NoseFlare).control < 25u, "gesto: fosas nasales poco controlables");
	check(gesture_def(GestureKind::HandTremor).control < 20u, "gesto: temblor poco controlable");
	check(gesture_def(GestureKind::StareDown).control > 60u, "gesto: mirada desafiante controlable");
	check(gesture_def(GestureKind::Smile).channel == ExpressionChannel::FaceMouth,
	      "gesto: sonrisa en la boca");
	check(gesture_def(GestureKind::Yawn).channel == ExpressionChannel::FaceMouth,
	      "gesto: bostezo en la boca");
	check(gesture_def(GestureKind::Tank).channel == ExpressionChannel::Timing,
	      "gesto: pensar mucho es timing");
	check(gesture_count > 60u, "gesto: catalogo amplio");
}

void test_composure() {
	LeakContext ctx {};
	ctx.composure_base = 50u;
	check(effective_composure(ctx, ExpressionParams {}) == 50u, "compostura: base sola");

	ctx.tell_control = 80u;
	check(effective_composure(ctx, ExpressionParams {}) > 50u, "compostura: la pericia sube");

	ctx.fatigue = 255u;
	ctx.tilt = 255u;
	check(effective_composure(ctx, ExpressionParams {}) < 50u, "compostura: fatiga/tilt bajan");

	ctx.composure_base = 0u;
	ctx.tell_control = 0u;
	ctx.fatigue = 255u;
	ctx.tilt = 255u;
	check(effective_composure(ctx, ExpressionParams {}) == 0u, "compostura: nunca negativa");
}

void test_leak() {
	// Emocion por debajo del umbral: sin fuga.
	{
		Mind calm = make_mind(10u, 10u, 128u);
		LeakContext ctx {};
		ctx.composure_base = 20u; // poca compostura
		check(leak(calm, ctx, GestureKind::BlinkFast) == 0u, "fuga: sin emocion no hay fuga");
	}

	// Misma emocion, mas compostura => menos fuga (monotonia).
	{
		Mind scared = make_mind(220u, 10u, 128u);
		LeakContext low {};
		low.composure_base = 10u;
		LeakContext high {};
		high.composure_base = 90u;
		const u8 leak_low = leak(scared, low, GestureKind::BlinkFast);
		const u8 leak_high = leak(scared, high, GestureKind::BlinkFast);
		check(leak_low > leak_high, "fuga: mas compostura, menos fuga");
		check(leak_low > 0u, "fuga: se le escapa algo");
	}

	// Canal autonomico vs volitivo con la misma compostura: el autonomico delata mas.
	{
		Mind scared = make_mind(220u, 10u, 128u);
		LeakContext ctx {};
		ctx.composure_base = 60u;
		const u8 auto_leak = leak(scared, ctx, GestureKind::HandTremor);
		const u8 voli_leak = leak(scared, ctx, GestureKind::StareDown);
		check(auto_leak > voli_leak, "fuga: el canal autonomico delata mas");
	}

	// Compostura perfecta: no se filtra nada.
	{
		Mind scared = make_mind(255u, 255u, 0u);
		LeakContext ctx {};
		ctx.composure_base = 100u;
		ctx.tell_control = 100u;
		check(leak(scared, ctx, GestureKind::BlinkFast) == 0u, "fuga: compostura perfecta no filtra");
	}

	// La ansiedad sube la fuga.
	{
		Mind mild = make_mind(60u, 10u, 128u);
		LeakContext calm {};
		calm.composure_base = 40u;
		LeakContext anxious = calm;
		anxious.anxiety = 120u;
		check(leak(mild, anxious, GestureKind::BlinkFast) >
		          leak(mild, calm, GestureKind::BlinkFast),
		      "fuga: la ansiedad sube la fuga");
	}
}

void test_leak_list() {
	const GestureKind candidates[4] {GestureKind::BlinkFast, GestureKind::HandTremor,
	                                 GestureKind::StareDown, GestureKind::Smile};
	Mind scared = make_mind(230u, 20u, 100u);
	LeakContext ctx {};
	ctx.composure_base = 20u;
	LeakList out;
	compute_leaks(scared, ctx, candidates, out);
	check(out.size() > 0u, "lista: hay fugas");
	// El temblor (autonomico) debe estar entre las fugas visibles.
	bool has_tremor = false;
	for (eng::usize i = 0u; i < out.size(); ++i) {
		if (out[i].kind == GestureKind::HandTremor) {
			has_tremor = true;
		}
	}
	check(has_tremor, "lista: incluye el temblor");

	// Con compostura perfecta, la lista queda vacia.
	LeakContext stoic {};
	stoic.composure_base = 100u;
	stoic.tell_control = 100u;
	LeakList none;
	compute_leaks(scared, stoic, candidates, none);
	check(none.size() == 0u, "lista: compostura perfecta = sin fugas");
}

} // namespace

int main() {
	std::printf("eng::sim expression:\n");
	test_gesture_table();
	test_composure();
	test_leak();
	test_leak_list();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim expression (canales, control, compostura y fugas)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
