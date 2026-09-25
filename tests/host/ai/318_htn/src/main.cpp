// ============================================================================
// Test HOST-318: planificacion jerarquica (HTN) y su consumidor de construccion
// ============================================================================
//
// Valida `eng/ai/planning/htn.hpp`: en vez de A* (GOAP), **descompone** una tarea compuesta
// en subtareas por metodos (precondicion -> lista de subtareas), comprobando que cada accion
// primitiva sea aplicable en el estado que va resultando. Se prueba:
//
//   1) el mecanismo (un dominio de pastel: comprar/batir/hornear) y el orden de los pasos;
//   2) los **metodos con precondicion** (con la mezcla hecha, solo hornear);
//   3) el **fallo controlado** (0) cuando ningun metodo descompone;
//   4) el **consumidor real** `domain.hpp::build_shelter_htn` y su **equivalencia** con el
//      plan GOAP del mismo dominio.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/318_htn

#include <cstdio>

#include <eng/ai/planning/htn.hpp>
#include <eng/core/util/array.hpp>
#include <eng/sim/domain.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_basic_htn() {
	using Ai = eng::ai::Goap<32u>;
	using H = eng::ai::Htn<32u, 3u, 2u, 3u, 8u>;

	// Hechos: 0=harina, 1=huevos, 2=mezcla, 3=pastel.
	const eng::util::Array<Ai::Action, 3> acts {{
	    Ai::Builder {}.named("buy").produce(0u, 1u).build(),
	    Ai::Builder {}.named("mix").require(0u, 1u).produce(2u).build(),
	    Ai::Builder {}.named("bake").require(2u).produce(3u).build(),
	}};

	H h {};
	h.add_subtask(0u).add_subtask(1u).add_subtask(2u); // subtareas 0,1,2 = buy,mix,bake
	h.add_subtask(2u);                                 // subtarea 3 = bake
	const H::Facts none {};
	H::Facts has_mix {};
	has_mix.set(2u);
	// Se declara PRIMERO el respaldo (prioridad 0) y despues el metodo con precondicion
	// (prioridad 1): el orden de prueba lo fija la **prioridad**, no la declaracion.
	h.add_method(none, none, 0u, 3u, 0u);    // metodo 0 (respaldo): comprar, batir, hornear
	h.add_method(has_mix, none, 2u, 1u, 1u); // metodo 1: con mezcla -> solo hornear
	h.add_method(none, none, 3u, 1u, 0u);    // metodo 2: hornear (para el fallo)
	h.add_compound(0u, 2u);              // compuesta 0 = pastel (metodos 0 y 1)
	h.add_compound(2u, 1u);              // compuesta 1 = hornear solo (metodo 2)

	Ai::State s {};
	eng::u16 out[8] {};
	const eng::usize n =
	    h.plan(s, h.compound_at(0u), acts.span(), eng::Span<eng::u16> {out, 8u});
	check(n == 3u, "htn: descompone en 3 acciones");
	check(out[0] == 0u && out[1] == 1u && out[2] == 2u, "htn: orden comprar, batir, hornear");
	check(h.plan_cost() == 3u, "htn: coste del respaldo (comprar+batir+hornear)");

	// Con la mezcla ya hecha, el primer metodo basta.
	Ai::State s2 {};
	s2.facts.set(2u);
	const eng::usize n2 =
	    h.plan(s2, h.compound_at(0u), acts.span(), eng::Span<eng::u16> {out, 8u});
	check(n2 == 1u && out[0] == 2u,
	      "htn: coste minimo elige el metodo mejor (precondicion), no el declarado antes");
	check(h.plan_cost() == 1u, "htn: coste minimo del metodo corto");

	// La compuesta 1 solo sabe hornear: sin mezcla, no hay descomposicion.
	const eng::usize n3 =
	    h.plan(s, h.compound_at(1u), acts.span(), eng::Span<eng::u16> {out, 8u});
	check(n3 == 0u, "htn: sin descomposicion aplicable devuelve 0");
}

void test_method_cost() {
	using Ai = eng::ai::Goap<32u>;
	using H = eng::ai::Htn<32u, 4u, 1u, 2u, 8u>;

	// 3 = comprar mezcla ya hecha (caro): dos caminos al pastel con costes distintos.
	const eng::util::Array<Ai::Action, 4> acts {{
	    Ai::Builder {}.named("buy").produce(0u, 1u).build(),
	    Ai::Builder {}.named("mix").require(0u, 1u).produce(2u).build(),
	    Ai::Builder {}.named("bake").require(2u).produce(3u).build(),
	    Ai::Builder {}.named("buy_mix").cost(9u).produce(2u).build(),
	}};

	H h {};
	h.add_subtask(0u).add_subtask(1u).add_subtask(2u); // camino barato: buy, mix, bake
	h.add_subtask(3u).add_subtask(2u);                 // camino caro: buy_mix, bake
	const H::Facts none {};
	// El metodo caro tiene MAS prioridad: aun asi debe ganar el de menor **coste**.
	h.add_method(none, none, 0u, 3u, 0u, 0u); // prioridad 0 -> coste 1+1+1 = 3
	h.add_method(none, none, 3u, 2u, 9u, 0u); // prioridad 9 -> coste 9+1 = 10
	h.add_compound(0u, 2u);

	Ai::State s {};
	eng::u16 out[8] {};
	const eng::usize n =
	    h.plan(s, h.compound_at(0u), acts.span(), eng::Span<eng::u16> {out, 8u});
	check(n == 3u && out[0] == 0u && out[1] == 1u && out[2] == 2u,
	      "htn coste: elige el camino barato aunque el caro tenga mas prioridad");
	check(h.plan_cost() == 3u, "htn coste: 3 (barato), no 10 (prioritario)");
}

void test_nested_compound() {
	using Ai = eng::ai::Goap<32u>;
	using H = eng::ai::Htn<32u, 3u, 2u, 4u, 8u>;

	const eng::util::Array<Ai::Action, 3> acts {{
	    Ai::Builder {}.named("buy").produce(0u, 1u).build(),
	    Ai::Builder {}.named("mix").require(0u, 1u).produce(2u).build(),
	    Ai::Builder {}.named("bake").require(2u).produce(3u).build(),
	}};

	H h {};
	// «preparar» (compuesta 0): comprar y batir.
	h.add_subtask(0u).add_subtask(1u);
	// «hacer pastel» (compuesta 1): preparar (compuesta 0) y hornear.
	h.add_subtask(H::compound(0u)).add_subtask(2u);
	const H::Facts none {};
	h.add_method(none, none, 0u, 2u, 0u); // metodo 0 -> preparar
	h.add_method(none, none, 2u, 2u, 0u); // metodo 1 -> hacer pastel
	h.add_compound(0u, 1u);               // compuesta 0 = preparar
	h.add_compound(1u, 1u);               // compuesta 1 = hacer pastel

	Ai::State s {};
	eng::u16 out[8] {};
	const eng::usize n =
	    h.plan(s, h.compound_at(1u), acts.span(), eng::Span<eng::u16> {out, 8u});
	check(n == 3u, "htn anidado: 3 acciones");
	check(out[0] == 0u && out[1] == 1u && out[2] == 2u,
	      "htn anidado: compuesta dentro de compuesta (comprar, batir, hornear)");
}

void test_construction_consumer() {
	const SimHtn htn = build_shelter_htn();
	const auto acts = ConstructionDomain::actions();
	const SimHtn::State start = start_state(SimInventory {});

	eng::u16 out[8] {};
	const eng::usize n =
	    htn.plan(start, htn.compound_at(0u), acts.span(), eng::Span<eng::u16> {out, 8u});
	check(n == 4u, "htn construccion: 4 pasos");
	check(out[0] == static_cast<eng::u16>(SimActionKind::Gather) &&
		  out[1] == static_cast<eng::u16>(SimActionKind::CraftTool) &&
		  out[2] == static_cast<eng::u16>(SimActionKind::Gather) &&
		  out[3] == static_cast<eng::u16>(SimActionKind::Build),
	      "htn construccion: gather, craft, gather, build");

	// Equivalencia con el GOAP del mismo dominio (mismo plan, no solo misma longitud).
	SimGoap::Goal goal = ConstructionDomain::goal(false, true);
	SimGoap::Planner<64> planner;
	eng::u16 gplan[8] {};
	const eng::usize gn =
	    planner.plan(start, goal, acts.span(), eng::Span<eng::u16> {gplan, 8u});
	check(gn == n, "htn vs goap: mismo numero de pasos");
	bool same = gn == n;
	for (eng::usize i = 0u; same && i < n; ++i) {
		same = gplan[i] == out[i];
	}
	check(same, "htn vs goap: mismo plan");

	// Con materiales y herramienta previos, un solo paso.
	SimInventory ready {};
	ready.has_materials = true;
	ready.has_tool = true;
	const eng::usize n1 = htn.plan(start_state(ready), htn.compound_at(0u), acts.span(),
				      eng::Span<eng::u16> {out, 8u});
	check(n1 == 1u && out[0] == static_cast<eng::u16>(SimActionKind::Build),
	      "htn construccion: con herramientas solo construir");
}

} // namespace

int main() {
	std::printf("HTN:\n");
	test_basic_htn();
	test_method_cost();
	test_nested_compound();
	test_construction_consumer();

	if (g_fail == 0u) {
		std::printf("OK: HTN (descomposicion, metodos con precondicion, fallo y equivalencia "
			    "con GOAP)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
