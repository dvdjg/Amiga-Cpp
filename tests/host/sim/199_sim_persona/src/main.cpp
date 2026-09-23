// ============================================================================
// Test HOST-199: persona, rasgos de psique y arquetipos (eng::sim)
// ============================================================================
//
// Valida `eng/sim/psyche_traits.hpp`, `eng/sim/archetypes.hpp` y `eng/sim/persona.hpp`:
// rasgos de psique y aptitudes, defectos como bits, el catalogo de arquetipos y la
// materializacion de una persona con jitter determinista.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/199_sim_persona

#include <cstdio>

#include <eng/core/math/random.hpp>
#include <eng/sim/archetypes.hpp>
#include <eng/sim/persona.hpp>
#include <eng/sim/psyche_traits.hpp>

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

void test_flaws() {
	Flaws f;
	check(f.count() == 0u, "flaws: vacio");
	f.set(Flaw::Tilt);
	f.set(Flaw::Vengeance);
	check(f.has(Flaw::Tilt) && f.has(Flaw::Vengeance), "flaws: set/has");
	check(!f.has(Flaw::Greed), "flaws: no tiene Greed");
	check(f.count() == 2u, "flaws: cuenta 2");
	f.clear(Flaw::Tilt);
	check(!f.has(Flaw::Tilt) && f.count() == 1u, "flaws: clear");
	check(flaw_name(Flaw::Overconfidence)[0] == 'o', "flaws: nombre");
}

void test_archetype_catalog() {
	check(archetype_count >= 27u, "catalogo: al menos 27 arquetipos");

	// Nombres unicos y arquetipo->nombre->arquetipo consistente.
	bool unique = true;
	bool round_trip = true;
	for (eng::usize i = 0u; i < archetype_count; ++i) {
		const Archetype a = static_cast<Archetype>(i);
		if (archetype_def(a).id != a) {
			round_trip = false;
		}
		if (archetype_of(archetype_name(a)) != a) {
			round_trip = false;
		}
		for (eng::usize j = static_cast<eng::usize>(i + 1u); j < archetype_count; ++j) {
			const char* x = archetype_name(a);
			const char* y = archetype_name(static_cast<Archetype>(j));
			bool same = true;
			while (*x != '\0' || *y != '\0') {
				if (*x != *y) {
					same = false;
					break;
				}
				++x;
				++y;
			}
			if (same) {
				unique = false;
			}
		}
	}
	check(unique, "catalogo: nombres unicos");
	check(round_trip, "catalogo: id/nombre consistentes");

	// El engreido arrastra la constelacion descrita (vanidad, dominancia, poca empatia).
	const ArchetypeDef& e = archetype_def(Archetype::Engreido);
	check(e.psyche.vanity > 75u, "engreido: vanidad alta");
	check(e.base.dominance > 70u, "engreido: dominancia alta");
	check(e.base.empathy < 30u, "engreido: poca empatia");
	check(e.psyche.composure > 70u && e.psyche.temper < 40u, "engreido: flematico");
	check(e.flaws.has(Flaw::Overconfidence), "engreido: overconfidence");

	// El pardillo: credulo, sin compostura, mal farol.
	const ArchetypeDef& p = archetype_def(Archetype::Pardillo);
	check(p.psyche.gullibility > 75u && p.psyche.composure < 30u, "pardillo: credulo y transparente");
	check(p.skills.bluffing < 20u, "pardillo: farol malo");

	// El sabio: sereno y dificil de leer.
	const ArchetypeDef& s = archetype_def(Archetype::Sabio);
	check(s.psyche.composure > 80u && s.skills.tell_control > 80u, "sabio: ilegible");
}

void test_persona_materialize() {
	eng::Xoroshiro64pp rng {42u, 7u};
	const Persona p = make_persona(Archetype::Embustero, rng, 0u);
	check(p.archetype == Archetype::Embustero, "persona: arquetipo");
	check(p.psyche.deceit == archetype_def(Archetype::Embustero).psyche.deceit,
	      "persona: sin jitter copia el rasgo");
	check(p.skills.bluffing == archetype_def(Archetype::Embustero).skills.bluffing,
	      "persona: aptitud copiada");
	check(p.flaws.has(Flaw::Overconfidence), "persona: defecto copiado");

	// Jitter determinista: misma semilla => misma persona; distinta => distinta.
	eng::Xoroshiro64pp rng_a {99u, 1u};
	eng::Xoroshiro64pp rng_b {99u, 1u};
	const Persona pa = make_persona(Archetype::Flematico, rng_a, 20u);
	const Persona pb = make_persona(Archetype::Flematico, rng_b, 20u);
	check(pa.psyche.composure == pb.psyche.composure &&
	          pa.base.aggression == pb.base.aggression,
	      "persona: jitter determinista");

	// El jitter no se sale de rango.
	bool in_range = true;
	eng::Xoroshiro64pp rng_c {5u, 5u};
	for (eng::u8 i = 0u; i < 50u; ++i) {
		const Persona q = make_persona(Archetype::Irascible, rng_c, 60u);
		if (q.psyche.temper > 100u || q.base.aggression > 100u) {
			in_range = false;
		}
	}
	check(in_range, "persona: jitter acotado a [0,100]");
}

void test_modifiers() {
	PsycheTraits t {};
	check(composure_mod(t) == 0, "mods: neutro = 0");
	t.composure = 100u;
	check(composure_mod(t) == 100, "mods: maximo = +100");
	t.composure = 0u;
	check(composure_mod(t) == -100, "mods: minimo = -100");
}

} // namespace

int main() {
	std::printf("eng::sim persona:\n");
	test_flaws();
	test_archetype_catalog();
	test_persona_materialize();
	test_modifiers();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim persona (rasgos de psique, aptitudes, defectos y arquetipos)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
