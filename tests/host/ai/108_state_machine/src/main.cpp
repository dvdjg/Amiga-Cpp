// ============================================================================
// Test HOST-108: maquina de estados finita generica (eng::util::StateMachine)
// ============================================================================
//
// Valida `engine/include/eng/core/util/state_machine.hpp`:
//
//   1) Semaforo: ciclo Rojo -> Verde -> Ambar -> Rojo con el evento `Paso`.
//   2) FSM de IA de un guardia (Patrulla/Alerta/Persecucion/Recarga): transiciones,
//      evento sin transicion (no cambia de estado), reset y contador.
//   3) Gana la PRIMERA transicion que coincide (orden de la tabla).
//
// La tabla de transiciones es un array `constexpr` externo; la maquina solo la
// referencia (sin heap, sin virtuals). La capa de IA usa esta misma maquina en vez de
// una FSM propia (ver docs/engine/architecture/GAME_AI_LIBRARY.md).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/108_state_machine

#include <cstdio>

#include <eng/core/util/state_machine.hpp>

namespace {

using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
// 1) Semaforo.
// ---------------------------------------------------------------------------
enum class Luz { Rojo, Verde, Ambar };
enum class Paso { Tick };

constexpr eng::util::Transition<Luz, Paso> kLuz[] = {
	{Luz::Rojo, Paso::Tick, Luz::Verde},
	{Luz::Verde, Paso::Tick, Luz::Ambar},
	{Luz::Ambar, Paso::Tick, Luz::Rojo},
};

void test_traffic_light() {
	eng::util::StateMachine<Luz, Paso> sem {Luz::Rojo, kLuz};
	check(sem.table_size() == 3u, "semaforo: 3 transiciones en la tabla");
	check(sem.is(Luz::Rojo), "semaforo: arranca en rojo");

	check(sem.dispatch(Paso::Tick) && sem.is(Luz::Verde), "semaforo: rojo -> verde");
	check(sem.dispatch(Paso::Tick) && sem.is(Luz::Ambar), "semaforo: verde -> ambar");
	check(sem.dispatch(Paso::Tick) && sem.is(Luz::Rojo), "semaforo: ambar -> rojo");
	check(sem.transition_count() == 3u, "semaforo: 3 transiciones aplicadas");

	sem.reset();
	check(sem.is(Luz::Rojo) && sem.transition_count() == 0u, "semaforo: reset vuelve a rojo");
}

// ---------------------------------------------------------------------------
// 2) FSM de IA de un guardia.
// ---------------------------------------------------------------------------
enum class Guard : eng::u8 { Patrulla, Alerta, Persecucion, Recarga };
enum class GEvent : eng::u8 { VeEnemigo, PierdeEnemigo, SinMunicion, Recargado };

constexpr eng::util::Transition<Guard, GEvent> kGuard[] = {
	{Guard::Patrulla, GEvent::VeEnemigo, Guard::Alerta},
	{Guard::Patrulla, GEvent::SinMunicion, Guard::Recarga},
	{Guard::Alerta, GEvent::VeEnemigo, Guard::Persecucion},
	{Guard::Alerta, GEvent::PierdeEnemigo, Guard::Patrulla},
	{Guard::Persecucion, GEvent::SinMunicion, Guard::Recarga},
	{Guard::Persecucion, GEvent::PierdeEnemigo, Guard::Alerta},
	{Guard::Recarga, GEvent::Recargado, Guard::Alerta},
};

void test_guard_fsm() {
	eng::util::StateMachine<Guard, GEvent> fsm {Guard::Patrulla, kGuard};
	check(fsm.is(Guard::Patrulla), "guardia: arranca patrullando");

	check(fsm.dispatch(GEvent::VeEnemigo) && fsm.is(Guard::Alerta), "guardia: ve -> alerta");
	check(fsm.dispatch(GEvent::VeEnemigo) && fsm.is(Guard::Persecucion), "guardia: ve -> persecucion");

	// Evento sin transicion: no cambia de estado y devuelve false.
	check(!fsm.dispatch(GEvent::Recargado), "guardia: `recargado` no aplica persiguiendo");
	check(fsm.is(Guard::Persecucion), "guardia: el estado no cambio");

	check(fsm.dispatch(GEvent::SinMunicion) && fsm.is(Guard::Recarga), "guardia: sin municion -> recarga");
	check(fsm.dispatch(GEvent::Recargado) && fsm.is(Guard::Alerta), "guardia: recargado -> alerta");
	check(fsm.dispatch(GEvent::PierdeEnemigo) && fsm.is(Guard::Patrulla), "guardia: pierde -> patrulla");
	check(fsm.transition_count() == 5u, "guardia: 5 transiciones aplicadas");

	fsm.reset(Guard::Alerta);
	check(fsm.is(Guard::Alerta) && fsm.transition_count() == 0u, "guardia: reset a un estado concreto");
}

// ---------------------------------------------------------------------------
// 3) Gana la primera transicion que coincide.
// ---------------------------------------------------------------------------
void test_first_match_wins() {
	enum class S { A, B, C };
	enum class E { Go };
	constexpr eng::util::Transition<S, E> tabla[] = {
		{S::A, E::Go, S::B}, // gana esta
		{S::A, E::Go, S::C},
	};
	eng::util::StateMachine<S, E> fsm {S::A, tabla};
	check(fsm.dispatch(E::Go) && fsm.is(S::B), "orden: gana la primera transicion de la tabla");
}

} // namespace

int main() {
	std::printf("FSM:\n");
	test_traffic_light();
	test_guard_fsm();
	test_first_match_wins();

	if (g_fail == 0u) {
		std::printf("OK: StateMachine (semaforo, guardia de IA, orden de tabla)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
