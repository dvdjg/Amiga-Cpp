// ============================================================================
// Test HOST-128: tareas secuenciales stackless (TaskSequence + Delay)
// ============================================================================
//
// Valida `engine/include/eng/core/util/task.hpp`:
//
//   1) Encadena pasos instantaneos en un mismo tick hasta terminar.
//   2) Un paso que devuelve Running espera al tick siguiente.
//   3) Failure aborta: no se ejecutan los pasos siguientes.
//   4) `Delay` espera N ticks.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/128_task

#include <cstdio>

#include <eng/core/util/task.hpp>

namespace {

using eng::u16;
using eng::util::Delay;
using eng::util::TaskSequence;
using eng::util::TaskStatus;

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

u16 g_a_ticks = 0;
TaskStatus step_a() {
	++g_a_ticks;
	if (g_a_ticks < 2u) {
		return TaskStatus::Running; // espera un tick
	}
	log_add('A');
	return TaskStatus::Success;
}
TaskStatus step_b() {
	log_add('B');
	return TaskStatus::Success;
}
TaskStatus step_c() {
	log_add('C');
	return TaskStatus::Failure;
}
TaskStatus step_d() {
	log_add('D');
	return TaskStatus::Success;
}

void test_sequence() {
	g_n = 0;
	g_a_ticks = 0;
	TaskSequence<4> seq;
	check(seq.add(step_a) && seq.add(step_b) && seq.add(step_d), "tarea: anade pasos");
	check(seq.step_count() == 3u, "tarea: 3 pasos");

	check(seq.tick() == TaskStatus::Running, "tarea: A espera en el primer tick");
	check(log_is(""), "tarea: aun no ha ejecutado nada");
	check(seq.tick() == TaskStatus::Success, "tarea: A termina y siguen B y D");
	check(log_is("ABD"), "tarea: pasos instantaneos encadenados en un tick");
	check(seq.done() && seq.tick() == TaskStatus::Success, "tarea: ya terminada");
}

void test_failure_aborts() {
	g_n = 0;
	TaskSequence<4> seq;
	check(seq.add(step_b) && seq.add(step_c) && seq.add(step_d), "tarea: pasos con fallo");
	check(seq.tick() == TaskStatus::Failure, "tarea: el paso C falla");
	check(log_is("BC"), "tarea: D no se ejecuta tras el fallo");
	check(seq.failed() && seq.tick() == TaskStatus::Failure, "tarea: sigue fallando");
}

void test_delay() {
	g_n = 0;
	Delay espera {2};
	TaskSequence<2> seq;
	check(seq.add(espera) && seq.add(step_b), "tarea: Delay + paso");
	check(seq.tick() == TaskStatus::Running, "tarea: Delay tick 1 -> Running");
	check(seq.tick() == TaskStatus::Success, "tarea: Delay tick 2 -> Success, sigue B");
	check(log_is("B"), "tarea: B corre tras el Delay");
	check(espera() == TaskStatus::Success, "tarea: Delay ya agotado");
}

} // namespace

int main() {
	std::printf("Task:\n");
	test_sequence();
	test_failure_aborts();
	test_delay();

	if (g_fail == 0u) {
		std::printf("OK: Task (secuencia, espera, fallo, Delay)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
