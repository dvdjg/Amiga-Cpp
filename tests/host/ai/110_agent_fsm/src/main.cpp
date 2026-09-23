// ============================================================================
// Test HOST-110: FSM de agente con efectos de entrada/salida
// ============================================================================
//
// Valida `engine/include/eng/ai/decision/agent_fsm.hpp`: `AgentFsm<State, Event,
// MaxStates>` envuelve `eng::util::StateMachine` y añade los efectos que el agente
// ejecuta al ENTRAR y al SALIR de cada estado (animación, objetivo, sonido...).
//
//   1) Un evento con transición ejecuta salida del estado actual + entrada del nuevo.
//   2) Un evento sin transición NO ejecuta efectos y devuelve false.
//   3) Los estados sin efecto registrado no fallan (Patrulla).
//   4) `previous()` y el contador de transiciones.
//
// Los efectos se registran como funciones libres (FunctionRef), que deben vivir más que
// la FSM; aquí viven todo el programa.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/110_agent_fsm

#include <cstdio>

#include <eng/ai/decision/agent_fsm.hpp>

namespace {

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

char g_log[16] {};
int g_n = 0;
void log_add(char c) {
	if (g_n < 16) {
		g_log[g_n++] = c;
	}
}
[[nodiscard]] bool log_is(const char* s) {
	int i = 0;
	for (; s[i] != '\0'; ++i) {
		if (i >= g_n || g_log[i] != s[i]) {
			return false;
		}
	}
	return i == g_n;
}

enum class Guard : eng::u8 { Patrulla, Alerta, Persecucion };
enum class Cmd : eng::u8 { VeEnemigo, Pierde };

constexpr eng::util::Transition<Guard, Cmd> kTabla[] = {
	{Guard::Patrulla, Cmd::VeEnemigo, Guard::Alerta},
	{Guard::Alerta, Cmd::VeEnemigo, Guard::Persecucion},
	{Guard::Persecucion, Cmd::Pierde, Guard::Alerta},
	{Guard::Alerta, Cmd::Pierde, Guard::Patrulla},
};

void enter_alerta() { log_add('A'); }
void exit_alerta() { log_add('a'); }
void enter_persecucion() { log_add('P'); }
void exit_persecucion() { log_add('p'); }

void test_effects() {
	g_n = 0;
	eng::ai::AgentFsm<Guard, Cmd, 3> fsm {Guard::Patrulla, kTabla};
	fsm.on_enter(Guard::Alerta, enter_alerta);
	fsm.on_exit(Guard::Alerta, exit_alerta);
	fsm.on_enter(Guard::Persecucion, enter_persecucion);
	fsm.on_exit(Guard::Persecucion, exit_persecucion);

	check(fsm.is(Guard::Patrulla), "agente: arranca patrullando");

	// Patrulla no tiene efectos; entra en Alerta -> 'A'.
	check(fsm.dispatch(Cmd::VeEnemigo) && fsm.is(Guard::Alerta), "agente: ve -> alerta");
	check(fsm.previous() == Guard::Patrulla, "agente: recuerda el estado anterior");
	check(log_is("A"), "agente: entra en alerta");

	// Alerta -> Persecucion: sale 'a', entra 'P'.
	check(fsm.dispatch(Cmd::VeEnemigo) && fsm.is(Guard::Persecucion), "agente: ve -> persecucion");
	check(log_is("AaP"), "agente: salida de alerta + entrada en persecucion");

	// Sin transicion desde Persecucion: no toca efectos y devuelve false.
	check(!fsm.dispatch(Cmd::VeEnemigo), "agente: evento sin transicion devuelve false");
	check(log_is("AaP"), "agente: sin transicion no hay efectos");
	check(fsm.is(Guard::Persecucion), "agente: el estado no cambia");

	// Persecucion -> Alerta: sale 'p', entra 'A'.
	check(fsm.dispatch(Cmd::Pierde) && fsm.is(Guard::Alerta), "agente: pierde -> alerta");
	check(log_is("AaPpA"), "agente: salida de persecucion + entrada en alerta");
	check(fsm.transition_count() == 3u, "agente: 3 transiciones");
}

void test_replace_effect() {
	g_n = 0;
	auto other = []() { log_add('X'); };
	eng::ai::AgentFsm<Guard, Cmd, 3> fsm {Guard::Patrulla, kTabla};
	fsm.on_enter(Guard::Alerta, enter_alerta);
	fsm.on_enter(Guard::Alerta, other); // reemplaza al anterior
	fsm.dispatch(Cmd::VeEnemigo);
	check(log_is("X"), "agente: on_enter reemplaza el efecto previo");
}

} // namespace

int main() {
	std::printf("AgentFsm:\n");
	test_effects();
	test_replace_effect();

	if (g_fail == 0u) {
		std::printf("OK: AgentFsm (efectos de entrada/salida, sin transicion, reemplazo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
