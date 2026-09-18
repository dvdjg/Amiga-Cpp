// ============================================================================
// Test HOST-107: planificacion GOAP (eng/ai/planning/goap.hpp)
// ============================================================================
//
// Valida el planificador GOAP sobre tres dominios clasicos de IA de juego:
//
//   1) Torres de Hanoi (3 discos): plan de 7 movimientos por reglas.
//   2) Receta de un pastel: cadena de ingredientes con coste por accion.
//   3) Mision de un soldado: obstaculos (alambre), utensilio (alicates), llave y
//      puerta, maquina (generador / puerta electrica), arma y municion.
//
// Ademas de que el plan exista, se comprueba su coste (optimo con esta heuristica),
// su longitud, que cada accion sea APLICABLE en orden y que el estado final cumpla
// el objetivo. Tambien se cubren el caso "ya se cumple" y el caso "sin solucion".
//
// El numero de hechos no se indica: `WorldState` es fijo (32 hechos) y el unico
// parametro de plantilla de la API es el presupuesto del `Planner`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/107_goap

#include <cstdio>

#include <eng/ai/planning/goap.hpp>
#include <eng/core/util/array.hpp>

namespace {

using eng::u16;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Ejecuta el planificador, valida coste/longitud y REPRODUCE el plan paso a paso
/// (cada accion aplicable y estado final conforme al objetivo).
template <usize A, usize N>
void run_and_check(const char* label, u16 expected_cost, usize expected_len,
		   const eng::ai::WorldState& start, const eng::ai::Goal& goal,
		   const eng::util::Array<eng::ai::Action, A>& actions) {
	eng::ai::Planner<N> planner;
	u16 plan[32] {};
	const usize n = planner.plan(start, goal, actions.span(), eng::Span<u16> {plan, 32u});
	std::printf("  %-12s plan=%u acciones  coste=%u  nodos=%u\n", label,
		    static_cast<unsigned>(n), static_cast<unsigned>(planner.plan_cost()),
		    static_cast<unsigned>(planner.expansions()));

	check(planner.found(), "el planificador encuentra solucion");
	if (!planner.found()) {
		return;
	}
	check(planner.plan_cost() == expected_cost, "el coste del plan es el minimo esperado");
	check(n == expected_len, "la longitud del plan es la esperada");

	eng::ai::WorldState s = start;
	for (usize i = 0; i < n; ++i) {
		const eng::ai::Action& a = actions[plan[i]];
		check(eng::ai::applicable(s, a), "cada accion del plan es aplicable");
		eng::ai::apply(s, a);
	}
	check(eng::ai::satisfies(s, goal), "el estado final cumple el objetivo");
}

// ---------------------------------------------------------------------------
// 1) Torres de Hanoi: 3 discos, 3 postes.
// ---------------------------------------------------------------------------
// Hecho = "disco d esta en el poste p" -> indice d*3 + p.
// Regla: un disco solo se mueve si no tiene discos menores encima -> al mover el
// disco d de `from` a `to`, ningun disco s<d puede estar en `from` ni en `to`.
constexpr u16 kDiskFact(u16 d, u16 p) { return static_cast<u16>(d * 3u + p); }

constexpr eng::util::Array<eng::ai::Action, 18> make_hanoi_actions() {
	eng::util::Array<eng::ai::Action, 18> acts {};
	usize k = 0;
	for (u16 d = 0; d < 3u; ++d) {
		for (u16 from = 0; from < 3u; ++from) {
			for (u16 to = 0; to < 3u; ++to) {
				if (from == to) {
					continue;
				}
				eng::ai::Action a {};
				a.cost = 1u;
				a.name = "mover";
				a.pre_true.facts.set(kDiskFact(d, from));
				for (u16 s = 0; s < d; ++s) {
					a.pre_false.facts.set(kDiskFact(s, from));
					a.pre_false.facts.set(kDiskFact(s, to));
				}
				a.eff_del.facts.set(kDiskFact(d, from));
				a.eff_add.facts.set(kDiskFact(d, to));
				acts[k++] = a;
			}
		}
	}
	return acts;
}
constexpr auto kHanoiActions = make_hanoi_actions();

void test_hanoi() {
	// Todo en el poste A (0) -> todo en el poste C (2).
	const auto start = eng::ai::make_state(kDiskFact(0, 0), kDiskFact(1, 0),
					       kDiskFact(2, 0));
	eng::ai::Goal goal {};
	goal.want_true.facts.set(kDiskFact(0, 2));
	goal.want_true.facts.set(kDiskFact(1, 2));
	goal.want_true.facts.set(kDiskFact(2, 2));
	// La solucion optima de Hanoi con 3 discos son 2^3 - 1 = 7 movimientos.
	run_and_check<18, 64>("hanoi", 7u, 7u, start, goal, kHanoiActions);
}

// ---------------------------------------------------------------------------
// 2) Receta de un pastel: ingredientes, mezcla, horno y decoracion.
// ---------------------------------------------------------------------------
enum : u16 {
	kHarina = 0,
	kHuevos = 1,
	kAzucar = 2,
	kMantequilla = 3,
	kMezcla = 4,
	kHornoCaliente = 5,
	kHorneado = 6,
	kPastelListo = 7,
};

constexpr eng::util::Array<eng::ai::Action, 8> kCakeActions { {
	eng::ai::ActionBuilder {}.named("comprar_harina").cost(2u).produce(kHarina).build(),
	eng::ai::ActionBuilder {}.named("comprar_huevos").cost(2u).produce(kHuevos).build(),
	eng::ai::ActionBuilder {}.named("comprar_azucar").cost(1u).produce(kAzucar).build(),
	eng::ai::ActionBuilder {}.named("comprar_mantequilla").cost(1u).produce(kMantequilla).build(),
	eng::ai::ActionBuilder {}
		.named("batir")
		.cost(3u)
		.require(kHarina, kHuevos, kAzucar, kMantequilla)
		.produce(kMezcla)
		.build(),
	eng::ai::ActionBuilder {}.named("precalentar").cost(4u).produce(kHornoCaliente).build(),
	eng::ai::ActionBuilder {}
		.named("hornear")
		.cost(5u)
		.require(kMezcla, kHornoCaliente)
		.produce(kHorneado)
		.build(),
	eng::ai::ActionBuilder {}.named("decorar").cost(2u).require(kHorneado).produce(kPastelListo).build(),
} };

void test_cake() {
	const eng::ai::WorldState start {};
	eng::ai::Goal goal {};
	goal.want_true.facts.set(kPastelListo);
	// 2+2+1+1 (compras) + 3 (batir) + 4 (horno) + 5 (hornear) + 2 (decorar) = 20.
	run_and_check<8, 64>("pastel", 20u, 8u, start, goal, kCakeActions);
}

// ---------------------------------------------------------------------------
// 3) Mision del soldado: obstaculo + utensilio + llave/puerta + maquina + arma.
// ---------------------------------------------------------------------------
enum : u16 {
	kAlicates = 0,
	kLlave = 1,
	kAlambreCortado = 2,
	kPuertaAbierta = 3,
	kEnArsenal = 4,
	kRifle = 5,
	kMunicion = 6,
	kRifleCargado = 7,
	kGenerador = 8,
	kPuertaElectrica = 9,
	kEnPuesto = 10,
	kEnemigoAbatido = 11,
	kObjetivo = 12,
};

constexpr eng::util::Array<eng::ai::Action, 13> kSoldierActions { {
	eng::ai::ActionBuilder {}.named("recoger_alicates").cost(1u).produce(kAlicates).build(),
	eng::ai::ActionBuilder {}.named("recoger_llave").cost(1u).produce(kLlave).build(),
	eng::ai::ActionBuilder {}.named("cortar_alambre").cost(3u).require(kAlicates).produce(kAlambreCortado).build(),
	eng::ai::ActionBuilder {}.named("abrir_puerta").cost(1u).require(kLlave).produce(kPuertaAbierta).build(),
	eng::ai::ActionBuilder {}.named("entrar_arsenal").cost(2u).require(kPuertaAbierta).produce(kEnArsenal).build(),
	eng::ai::ActionBuilder {}.named("coger_rifle").cost(2u).require(kEnArsenal).produce(kRifle).build(),
	eng::ai::ActionBuilder {}.named("coger_municion").cost(2u).require(kEnArsenal).produce(kMunicion).build(),
	eng::ai::ActionBuilder {}.named("cargar_rifle").cost(3u).require(kRifle, kMunicion).produce(kRifleCargado).build(),
	eng::ai::ActionBuilder {}.named("arrancar_generador").cost(4u).produce(kGenerador).build(),
	eng::ai::ActionBuilder {}.named("abrir_puerta_electrica").cost(1u).require(kGenerador).produce(kPuertaElectrica).build(),
	eng::ai::ActionBuilder {}.named("avanzar_puesto").cost(3u).require(kAlambreCortado, kPuertaElectrica).produce(kEnPuesto).build(),
	eng::ai::ActionBuilder {}.named("abatir_enemigo").cost(2u).require(kEnPuesto, kRifleCargado).produce(kEnemigoAbatido).build(),
	eng::ai::ActionBuilder {}.named("cumplir_mision").cost(1u).require(kEnemigoAbatido).produce(kObjetivo).build(),
} };

void test_soldier() {
	const eng::ai::WorldState start {};
	eng::ai::Goal goal {};
	goal.want_true.facts.set(kObjetivo);
	// 1+1 +3 +1+2 +2+2 +3 +4+1 +3 +2 +1 = 26; las 13 acciones son necesarias.
	// El presupuesto `MaxNodes` acota el estado de trabajo del planner (256 nodos ~9 KiB
	// inline): el escenario usa 81. En Amiga se instancia en estatica, no en la pila.
	run_and_check<13, 256>("soldado", 26u, 13u, start, goal, kSoldierActions);
}

// ---------------------------------------------------------------------------
// Casos limite del contrato.
// ---------------------------------------------------------------------------
void test_already_satisfied() {
	const auto start = eng::ai::make_state(kPastelListo);
	eng::ai::Goal goal {};
	goal.want_true.facts.set(kPastelListo);
	eng::ai::Planner<32> planner;
	u16 plan[4] {};
	const usize n = planner.plan(start, goal, kCakeActions.span(), eng::Span<u16> {plan, 4u});
	check(planner.found(), "objetivo ya cumplido: found()");
	check(n == 0u, "objetivo ya cumplido: plan vacio");
	check(planner.plan_cost() == 0u, "objetivo ya cumplido: coste 0");
}

void test_no_solution() {
	// El objetivo pide un hecho (el 7) que ninguna accion produce.
	eng::ai::Goal goal {};
	goal.want_true.facts.set(7u);
	eng::ai::Planner<32> planner;
	u16 plan[4] {};
	const usize n = planner.plan(eng::ai::WorldState {}, goal, kCakeActions.span(),
				     eng::Span<u16> {plan, 4u});
	check(!planner.found(), "sin solucion: found() es false");
	check(n == 0u, "sin solucion: no devuelve acciones");
}

void test_forbid() {
	// `forbid` exige que un hecho este a 0: la via "sin azucar" solo vale mientras no
	// se haya comprado azucar; si el azucar ya esta, hay que usar la via cara.
	constexpr eng::util::Array<eng::ai::Action, 3> acts { {
		eng::ai::ActionBuilder {}.named("comprar_azucar").cost(1u).produce(kAzucar).build(),
		eng::ai::ActionBuilder {}
			.named("sin_azucar")
			.cost(2u)
			.forbid(kAzucar)
			.produce(kPastelListo)
			.build(),
		eng::ai::ActionBuilder {}
			.named("con_azucar")
			.cost(50u)
			.require(kAzucar)
			.produce(kPastelListo)
			.build(),
	} };
	eng::ai::Goal goal {};
	goal.want_true.facts.set(kPastelListo);

	eng::ai::Planner<32> planner;
	u16 plan[4] {};

	// Sin azucar: gana la accion que prohibe el azucar (coste 2, sin comprar).
	const usize n0 = planner.plan(eng::ai::WorldState {}, goal, acts.span(),
				      eng::Span<u16> {plan, 4u});
	check(n0 == 1u && plan[0] == 1u, "forbid: sin azucar se elige `sin_azucar`");
	check(planner.plan_cost() == 2u, "forbid: coste 2");

	// Con azucar ya presente: `sin_azucar` no es aplicable -> via cara (coste 50).
	const auto con_azucar = eng::ai::make_state(kAzucar);
	const usize n1 = planner.plan(con_azucar, goal, acts.span(), eng::Span<u16> {plan, 4u});
	check(n1 == 1u && plan[0] == 2u, "forbid: con azucar no se aplica `sin_azucar`");
	check(planner.plan_cost() == 50u, "forbid: coste 50");
}

} // namespace

int main() {
	std::printf("GOAP:\n");
	test_hanoi();
	test_cake();
	test_soldier();
	test_already_satisfied();
	test_no_solution();
	test_forbid();

	if (g_fail == 0u) {
		std::printf("OK: GOAP planifica Hanoi, pastel y soldado con coste minimo\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
