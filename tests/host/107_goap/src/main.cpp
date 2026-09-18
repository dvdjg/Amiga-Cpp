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
// El dominio se declara UNA vez (`using Ai = eng::ai::Goap<>`); los tipos cuelgan
// de el y no hay que repetir el numero de hechos.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/107_goap

#include <cstdio>

#include <eng/ai/planning/goap.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/type_traits.hpp>

namespace {

using eng::u16;
using eng::usize;

/// Dominio GOAP del test (32 hechos, clave u32): cubre los tres escenarios clasicos.
using Ai = eng::ai::Goap<>;
/// Dominio ancho (64 hechos, clave de 64 bits) que convive con el anterior.
using Huge = eng::ai::Goap<64>;
static_assert(!eng::util::is_same_v<Ai::Action, Huge::Action>,
	      "cada dominio tiene sus propios tipos");

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Ejecuta el planificador de un dominio `D`, valida coste/longitud y REPRODUCE el plan
/// paso a paso (cada accion aplicable y estado final conforme al objetivo).
template <class D, usize A, usize N>
void run_and_check(const char* label, u16 expected_cost, usize expected_len,
		   const typename D::State& start, const typename D::Goal& goal,
		   const eng::util::Array<typename D::Action, A>& actions) {
	typename D::Planner<N> planner;
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

	typename D::State s = start;
	for (usize i = 0; i < n; ++i) {
		const typename D::Action& a = actions[plan[i]];
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

constexpr eng::util::Array<Ai::Action, 18> make_hanoi_actions() {
	eng::util::Array<Ai::Action, 18> acts {};
	usize k = 0;
	for (u16 d = 0; d < 3u; ++d) {
		for (u16 from = 0; from < 3u; ++from) {
			for (u16 to = 0; to < 3u; ++to) {
				if (from == to) {
					continue;
				}
				Ai::Action a {};
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
	const auto start = Ai::state(kDiskFact(0, 0), kDiskFact(1, 0), kDiskFact(2, 0));
	Ai::Goal goal {};
	goal.want_true.facts.set(kDiskFact(0, 2));
	goal.want_true.facts.set(kDiskFact(1, 2));
	goal.want_true.facts.set(kDiskFact(2, 2));
	// La solucion optima de Hanoi con 3 discos son 2^3 - 1 = 7 movimientos.
	run_and_check<Ai, 18, 64>("hanoi", 7u, 7u, start, goal, kHanoiActions);
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

constexpr eng::util::Array<Ai::Action, 8> kCakeActions { {
	Ai::Builder {}.named("comprar_harina").cost(2u).produce(kHarina).build(),
	Ai::Builder {}.named("comprar_huevos").cost(2u).produce(kHuevos).build(),
	Ai::Builder {}.named("comprar_azucar").cost(1u).produce(kAzucar).build(),
	Ai::Builder {}.named("comprar_mantequilla").cost(1u).produce(kMantequilla).build(),
	Ai::Builder {}
		.named("batir")
		.cost(3u)
		.require(kHarina, kHuevos, kAzucar, kMantequilla)
		.produce(kMezcla)
		.build(),
	Ai::Builder {}.named("precalentar").cost(4u).produce(kHornoCaliente).build(),
	Ai::Builder {}
		.named("hornear")
		.cost(5u)
		.require(kMezcla, kHornoCaliente)
		.produce(kHorneado)
		.build(),
	Ai::Builder {}.named("decorar").cost(2u).require(kHorneado).produce(kPastelListo).build(),
} };

void test_cake() {
	const Ai::State start {};
	Ai::Goal goal {};
	goal.want_true.facts.set(kPastelListo);
	// 2+2+1+1 (compras) + 3 (batir) + 4 (horno) + 5 (hornear) + 2 (decorar) = 20.
	run_and_check<Ai, 8, 64>("pastel", 20u, 8u, start, goal, kCakeActions);
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

constexpr eng::util::Array<Ai::Action, 13> kSoldierActions { {
	Ai::Builder {}.named("recoger_alicates").cost(1u).produce(kAlicates).build(),
	Ai::Builder {}.named("recoger_llave").cost(1u).produce(kLlave).build(),
	Ai::Builder {}.named("cortar_alambre").cost(3u).require(kAlicates).produce(kAlambreCortado).build(),
	Ai::Builder {}.named("abrir_puerta").cost(1u).require(kLlave).produce(kPuertaAbierta).build(),
	Ai::Builder {}.named("entrar_arsenal").cost(2u).require(kPuertaAbierta).produce(kEnArsenal).build(),
	Ai::Builder {}.named("coger_rifle").cost(2u).require(kEnArsenal).produce(kRifle).build(),
	Ai::Builder {}.named("coger_municion").cost(2u).require(kEnArsenal).produce(kMunicion).build(),
	Ai::Builder {}.named("cargar_rifle").cost(3u).require(kRifle, kMunicion).produce(kRifleCargado).build(),
	Ai::Builder {}.named("arrancar_generador").cost(4u).produce(kGenerador).build(),
	Ai::Builder {}.named("abrir_puerta_electrica").cost(1u).require(kGenerador).produce(kPuertaElectrica).build(),
	Ai::Builder {}.named("avanzar_puesto").cost(3u).require(kAlambreCortado, kPuertaElectrica).produce(kEnPuesto).build(),
	Ai::Builder {}.named("abatir_enemigo").cost(2u).require(kEnPuesto, kRifleCargado).produce(kEnemigoAbatido).build(),
	Ai::Builder {}.named("cumplir_mision").cost(1u).require(kEnemigoAbatido).produce(kObjetivo).build(),
} };

void test_soldier() {
	const Ai::State start {};
	Ai::Goal goal {};
	goal.want_true.facts.set(kObjetivo);
	// 1+1 +3 +1+2 +2+2 +3 +4+1 +3 +2 +1 = 26; las 13 acciones son necesarias.
	// El presupuesto `MaxNodes` acota el estado de trabajo del planner (medido en m68k:
	// 256 nodos = 8280 B inline): el escenario usa 81. En Amiga se instancia en estatica.
	run_and_check<Ai, 13, 256>("soldado", 26u, 13u, start, goal, kSoldierActions);
}

// ---------------------------------------------------------------------------
// 4) Dominio ancho, de 64 hechos (clave de 64 bits), conviviendo con el de 32.
// ---------------------------------------------------------------------------
// Confirma que la clave de dos palabras funciona y que se llega al hecho 63 (el ultimo
// valido); el resto de escenarios usan el dominio por defecto de 32 hechos.
void test_huge_domain() {
	enum : u16 { kW0 = 0, kW1 = 1, kFar = 63 };
	constexpr eng::util::Array<Huge::Action, 2> acts { {
		Huge::Builder {}.named("paso").cost(1u).require(kW0).produce(kW1).build(),
		Huge::Builder {}.named("salto_largo").cost(1u).require(kW1).produce(kFar).build(),
	} };
	Huge::Goal goal {};
	goal.want_true.facts.set(kFar);
	run_and_check<Huge, 2, 16>("huge64", 2u, 2u, Huge::state(kW0), goal, acts);
}

// ---------------------------------------------------------------------------
// Casos limite del contrato.
// ---------------------------------------------------------------------------
void test_already_satisfied() {
	const auto start = Ai::state(kPastelListo);
	Ai::Goal goal {};
	goal.want_true.facts.set(kPastelListo);
	Ai::Planner<32> planner;
	u16 plan[4] {};
	const usize n = planner.plan(start, goal, kCakeActions.span(), eng::Span<u16> {plan, 4u});
	check(planner.found(), "objetivo ya cumplido: found()");
	check(n == 0u, "objetivo ya cumplido: plan vacio");
	check(planner.plan_cost() == 0u, "objetivo ya cumplido: coste 0");
}

void test_no_solution() {
	// El objetivo pide un hecho (el 7) que ninguna accion produce.
	Ai::Goal goal {};
	goal.want_true.facts.set(7u);
	Ai::Planner<32> planner;
	u16 plan[4] {};
	const usize n = planner.plan(Ai::State {}, goal, kCakeActions.span(),
				     eng::Span<u16> {plan, 4u});
	check(!planner.found(), "sin solucion: found() es false");
	check(n == 0u, "sin solucion: no devuelve acciones");
}

void test_forbid() {
	// `forbid` exige que un hecho este a 0: la via "sin azucar" solo vale mientras no
	// se haya comprado azucar; si el azucar ya esta, hay que usar la via cara.
	constexpr eng::util::Array<Ai::Action, 3> acts { {
		Ai::Builder {}.named("comprar_azucar").cost(1u).produce(kAzucar).build(),
		Ai::Builder {}.named("sin_azucar").cost(2u).forbid(kAzucar).produce(kPastelListo).build(),
		Ai::Builder {}.named("con_azucar").cost(50u).require(kAzucar).produce(kPastelListo).build(),
	} };
	Ai::Goal goal {};
	goal.want_true.facts.set(kPastelListo);

	Ai::Planner<32> planner;
	u16 plan[4] {};

	// Sin azucar: gana la accion que prohibe el azucar (coste 2, sin comprar).
	const usize n0 = planner.plan(Ai::State {}, goal, acts.span(),
				      eng::Span<u16> {plan, 4u});
	check(n0 == 1u && plan[0] == 1u, "forbid: sin azucar se elige `sin_azucar`");
	check(planner.plan_cost() == 2u, "forbid: coste 2");

	// Con azucar ya presente: `sin_azucar` no es aplicable -> via cara (coste 50).
	const auto con_azucar = Ai::state(kAzucar);
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
	test_huge_domain();
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
