// ============================================================================
// Test HOST-201: lectura de tells (eng::sim)
// ============================================================================
//
// Valida `eng/sim/read.hpp`: el modelo de lectura de un observador (aprende en showdown,
// Bayes-lite, prior por arquetipo y descuento por suspicacia) y la distincion entre un
// rival legible (pardillo) y uno ruidoso (listillo).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/201_sim_read

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

const GestureKind kTracked[1] {GestureKind::BlinkFast};
constexpr eng::usize kGestures = 1u;

LeakedGesture leak_blink() {
	return LeakedGesture {GestureKind::BlinkFast, 80u, false};
}

void test_learns_from_showdown() {
	ReadModel<4, kGestures> m;
	m.reset();
	check(m.hands_seen[0] == 0u, "read: empieza sin muestras");

	// Un pardillo: el gesto aparece SIEMPRE que tiene mano fuerte y NUNCA con debil.
	const LeakedGesture leaked[1] {leak_blink()};
	for (u8 i = 0u; i < 12u; ++i) {
		label_showdown(m, 0u, true, eng::Span<const GestureKind> {kTracked, kGestures},
		               eng::Span<const LeakedGesture> {leaked, 1u});
	}
	const u8 slot = 0u;
	const eng::u16 ps = p_strong(m, 0u, slot);
	check(ps > 800u, "read: aprende que el gesto indica mano fuerte");
	check(tell_indicio(m, 0u, slot) > 40, "read: indicio positivo fuerte");
	check(classify_tells(m, 0u) == TellStyle::Readable, "read: rival legible");

	// Un listillo: el gesto aparece con fuerte y con debil a partes iguales.
	ReadModel<4, kGestures> n;
	n.reset();
	for (u8 i = 0u; i < 10u; ++i) {
		label_showdown(n, 1u, true, eng::Span<const GestureKind> {kTracked, kGestures},
		               eng::Span<const LeakedGesture> {leaked, 1u});
		label_showdown(n, 1u, false, eng::Span<const GestureKind> {kTracked, kGestures},
		               eng::Span<const LeakedGesture> {leaked, 1u});
	}
	check(p_strong(n, 1u, slot) > 400u && p_strong(n, 1u, slot) < 600u,
	      "read: gesto ruidoso queda cerca del 50%");
	check(classify_tells(n, 1u) == TellStyle::Noisy, "read: rival ruidoso");
}

void test_needs_showdown() {
	ReadModel<4, kGestures> m;
	m.reset();
	// Sin showdown no hay etiqueta: el modelo no cambia y no se fia.
	check(tell_indicio(m, 0u, 0u) == 0, "read: sin showdown no hay indicio");
	check(classify_tells(m, 0u) == TellStyle::Unknown, "read: sin showdown es desconocido");
	// Pocas muestras: por debajo de min_samples sigue sin fiarse.
	const LeakedGesture leaked[1] {leak_blink()};
	label_showdown(m, 0u, true, eng::Span<const GestureKind> {kTracked, kGestures},
	               eng::Span<const LeakedGesture> {leaked, 1u});
	check(tell_indicio(m, 0u, 0u) == 0, "read: una muestra no basta");
}

void test_suspicion_discount() {
	ReadModel<4, kGestures> m;
	m.reset();
	const LeakedGesture leaked[1] {leak_blink()};
	for (u8 i = 0u; i < 12u; ++i) {
		label_showdown(m, 0u, true, eng::Span<const GestureKind> {kTracked, kGestures},
		               eng::Span<const LeakedGesture> {leaked, 1u});
	}
	ReadParams low {};
	low.suspicion = 0u;
	ReadParams high {};
	high.suspicion = 100u;
	const eng::s16 ind_low = tell_indicio(m, 0u, 0u, low);
	const eng::s16 ind_high = tell_indicio(m, 0u, 0u, high);
	check(ind_low > ind_high, "read: la suspicacia descuenta el tell");
}

void test_archetype_prior() {
	// Un arquetipo embustero baja el prior (sus gestos mienten).
	ReadModel<4, kGestures> m;
	m.reset();
	m.archetype_guess[0] = Archetype::Embustero;
	m.archetype_guess[1] = Archetype::Pardillo;
	const eng::u16 p_embustero = p_strong(m, 0u, 0u);
	const eng::u16 p_pardillo = p_strong(m, 1u, 0u);
	check(p_embustero < p_pardillo, "read: el embustero tiene prior mas bajo");
}

} // namespace

int main() {
	std::printf("eng::sim read:\n");
	test_learns_from_showdown();
	test_needs_showdown();
	test_suspicion_discount();
	test_archetype_prior();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim read (aprendizaje de tells, Bayes-lite, prior y suspicacia)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
