// ============================================================================
// Test HOST-205: el humano como personaje (eng::sim)
// ============================================================================
//
// Valida `eng/sim/expression.hpp::expression_from_input`: los gestos explicitos y de
// timing del humano producen fugas, y la mesa las lee con el mismo `ReadModel` (simetria
// de lectura). Base para la interfaz realista en ajedrez/Go y poker.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/205_sim_input_expression

#include <cstdio>

#include <eng/sim/expression.hpp>
#include <eng/sim/read.hpp>

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

bool contains(const LeakList& l, GestureKind g) {
	for (eng::usize i = 0u; i < l.size(); ++i) {
		if (l[i].kind == g) {
			return true;
		}
	}
	return false;
}

void test_explicit_gestures() {
	InputExpression in {};
	in.gesture_a = true;
	in.gesture_b = true;
	const LeakList l = expression_from_input(in);
	check(contains(l, GestureKind::EarScratch), "entrada: gesto A produce fuga");
	check(contains(l, GestureKind::NoseFlare), "entrada: gesto B produce fuga");
	check(!contains(l, GestureKind::InstantCall), "entrada: sin timing no hay tell de tiempo");
}

void test_timing_tells() {
	InputExpression quick {};
	quick.quick_response = true;
	const LeakList q = expression_from_input(quick);
	check(contains(q, GestureKind::InstantCall), "timing: respuesta rapida = instant call");

	InputExpression slow {};
	slow.long_tank = true;
	const LeakList s = expression_from_input(slow);
	check(contains(s, GestureKind::Tank), "timing: tardanza = tank");

	InputExpression nervous {};
	nervous.fidget = true;
	check(contains(expression_from_input(nervous), GestureKind::Fidget),
	      "timing: entrada erratica = fidget");

	// Sin nada, no hay fugas.
	InputExpression none {};
	check(expression_from_input(none).size() == 0u, "entrada: sin gestos no hay fugas");
}

void test_symmetric_reading() {
	// La mesa lee al humano igual que a un NPC: aprende su tell de timing en showdown.
	InputExpression quick {};
	quick.quick_response = true;
	const LeakList leaked = expression_from_input(quick);

	// Gestos rastreados de la mesa (incluye InstantCall).
	constexpr GestureKind kTracked[3] {GestureKind::InstantCall, GestureKind::Tank,
	                                   GestureKind::Fidget};
	ReadModel<4, 3> reads;
	reads.reset();
	for (u8 i = 0u; i < 12u; ++i) {
		// El humano siempre responde rapido con mano fuerte (tell real).
		label_showdown(reads, 0u, true, eng::Span<const GestureKind> {kTracked, 3u},
		               leaked.span());
	}
	const u8 slot = ReadModel<4, 3>::slot_of(kTracked, 3u, GestureKind::InstantCall);
	check(slot < 3u, "lectura: InstantCall rastreado");
	const eng::u16 ps = p_strong(reads, 0u, slot);
	check(ps > 750u, "lectura: la mesa aprende el tell de timing del humano");
	check(classify_tells(reads, 0u) == TellStyle::Readable,
	      "lectura: clasifica al humano como legible");
}

} // namespace

int main() {
	std::printf("eng::sim input_expression:\n");
	test_explicit_gestures();
	test_timing_tells();
	test_symmetric_reading();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim input_expression (gestos explicitos, timing y lectura simetrica)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
