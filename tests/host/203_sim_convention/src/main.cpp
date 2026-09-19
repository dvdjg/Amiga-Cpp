// ============================================================================
// Test HOST-203: convenciones secretas (base del Mus) (eng::sim)
// ============================================================================
//
// Valida `eng/sim/convention.hpp`: emision/decodificacion de gestos pactados, disimulo y
// exposicion, e inferencia de la convencion por un observador externo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/203_sim_convention

#include <cstdio>

#include <eng/sim/convention.hpp>

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

void test_pact() {
	Convention c {};
	check(!c.active(), "pacto: vacio no activo");
	c.id = 7u;
	check(c.add(GestureKind::EarScratch, SignalKind::Alarm), "pacto: anade gesto");
	check(c.add(GestureKind::NoseFlare, SignalKind::Food), "pacto: anade segundo gesto");
	check(c.active(), "pacto: activo");
	check(c.lookup(GestureKind::EarScratch) == SignalKind::Alarm, "pacto: lookup correcto");
	check(c.lookup(GestureKind::Smile) == SignalKind::Count, "pacto: gesto no pactado");
}

void test_emit_decode() {
	Convention c {};
	c.id = 1u;
	c.add(GestureKind::EarScratch, SignalKind::Alarm);
	c.concealment = 60u;

	const Signal s = emit_convention(c, GestureKind::EarScratch, 5u, 0u, 0u, 10, 10, 120u);
	check(s.kind == SignalKind::Alarm, "emit: la senal es la pactada");
	check(c.exposure > 0u, "emit: sube la exposicion");

	// Quien comparte la convencion la decodifica.
	SignalKind out = SignalKind::Count;
	check(decode_convention(c, GestureKind::EarScratch, true, out) == ConventionDecode::Decoded,
	      "decode: el companero decodifica");
	check(out == SignalKind::Alarm, "decode: senal correcta");

	// Quien no la comparte solo ve un gesto.
	SignalKind out2 = SignalKind::Count;
	check(decode_convention(c, GestureKind::EarScratch, false, out2) ==
	          ConventionDecode::NotShared,
	      "decode: el rival no la comparte");
	// El companero ve un gesto no pactado.
	check(decode_convention(c, GestureKind::Smile, true, out2) ==
	          ConventionDecode::UnknownGesture,
	      "decode: gesto no pactado");
}

void test_disimulo() {
	// Mas disimulo => menos exposicion por uso.
	Convention low {};
	low.id = 1u;
	low.add(GestureKind::EarScratch, SignalKind::Alarm);
	low.concealment = 10u;
	Convention high {};
	high.id = 1u;
	high.add(GestureKind::EarScratch, SignalKind::Alarm);
	high.concealment = 90u;
	for (u8 i = 0u; i < 5u; ++i) {
		(void)emit_convention(low, GestureKind::EarScratch, 1u, 0u, 0u, 0, 0, 120u);
		(void)emit_convention(high, GestureKind::EarScratch, 1u, 0u, 0u, 0, 0, 120u);
	}
	check(high.exposure < low.exposure, "disimulo: mas disimulo, menos exposicion");

	// El disimulo tambien acelera el desvanecimiento de la exposicion.
	const u8 before = high.exposure;
	convention_decay(high, 20u);
	check(high.exposure < before, "disimulo: decay baja la exposicion");
}

void test_inference() {
	ConventionObserver obs {};
	check(!convention_inferred(obs), "inferencia: al principio no");
	// El mismo par repite el mismo gesto en momentos de decision: sube la sospecha.
	for (u8 i = 0u; i < 8u; ++i) {
		observe_for_convention(obs, 1u, 2u, GestureKind::EarScratch, true);
	}
	check(obs.pair_repeats > 0u, "inferencia: cuenta repeticiones");
	check(convention_inferred(obs), "inferencia: se deduce la convencion");
	check(obs.suspicion > 0u, "inferencia: sube la sospecha");

	// Fuera de un momento de decision no se observa.
	ConventionObserver obs2 {};
	observe_for_convention(obs2, 1u, 2u, GestureKind::EarScratch, false);
	check(obs2.suspicion == 0u, "inferencia: sin decision no se observa");
}

} // namespace

int main() {
	std::printf("eng::sim convention:\n");
	test_pact();
	test_emit_decode();
	test_disimulo();
	test_inference();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim convention (pacto, disimulo, exposicion e inferencia)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
