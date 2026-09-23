// ============================================================================
// Test HOST-109: emisor de eventos de capacidad fija (eng::util::Event)
// ============================================================================
//
// Valida `engine/include/eng/core/util/event.hpp`:
//
//   1) Suscripcion/emit: los suscriptores se invocan (FunctionRef, sin heap).
//   2) Capacidad: `subscribe` falla cuando ya no caben mas.
//   3) `clear`: da de baja a todos; el siguiente `emit` no invoca a nadie.
//   4) Orden de invocacion: en orden de suscripcion; eventos sin argumentos.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/109_event

#include <cstdio>

#include <eng/core/util/event.hpp>

namespace {

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

int g_sum = 0;
char g_log[16] {};
int g_log_n = 0;
void log_add(char c) {
	if (g_log_n < 16) {
		g_log[g_log_n++] = c;
	}
}

// ---------------------------------------------------------------------------
// 1) Suscripcion, emit y clear.
// ---------------------------------------------------------------------------
void test_subscribe_emit_clear() {
	g_sum = 0;
	eng::util::Event<void(int), 4> ev;
	check(ev.empty() && ev.size() == 0u, "evento: arranca vacio");

	// Lambdas con nombre: FunctionRef no las posee, deben vivir mas que el evento.
	auto add1 = [](int v) { g_sum += v; };
	auto add10 = [](int v) { g_sum += v * 10; };
	check(ev.subscribe(add1), "evento: suscribe el primero");
	check(ev.subscribe(add10), "evento: suscribe el segundo");
	check(ev.size() == 2u, "evento: dos suscriptores");

	ev.emit(2);
	check(g_sum == 22, "evento: 2 + 20 = 22");
	ev.emit(1);
	check(g_sum == 33, "evento: acumula entre emisiones");

	ev.clear();
	check(ev.empty(), "evento: clear deja vacio");
	ev.emit(5);
	check(g_sum == 33, "evento: tras clear no se invoca a nadie");
}

// ---------------------------------------------------------------------------
// 2) Capacidad.
// ---------------------------------------------------------------------------
void test_capacity() {
	eng::util::Event<void(int), 2> ev;
	auto a = [](int) {};
	auto b = [](int) {};
	auto c = [](int) {};
	check(ev.subscribe(a) && ev.subscribe(b), "capacidad: caben dos");
	check(!ev.subscribe(c), "capacidad: el tercero no cabe");
	check(ev.size() == 2u, "capacidad: sigue en dos");
	check(eng::util::Event<void(int), 2>::capacity() == 2u, "capacidad: expuesta");
}

// ---------------------------------------------------------------------------
// 3) Orden de invocacion y eventos sin argumentos.
// ---------------------------------------------------------------------------
void test_order() {
	g_log_n = 0;
	eng::util::Event<void(), 4> ev;
	auto first = []() { log_add('1'); };
	auto second = []() { log_add('2'); };
	auto third = []() { log_add('3'); };
	ev.subscribe(first);
	ev.subscribe(second);
	ev.subscribe(third);
	ev.emit();
	check(g_log_n == 3 && g_log[0] == '1' && g_log[1] == '2' && g_log[2] == '3',
	      "orden: los suscriptores se invocan en orden");
}

} // namespace

int main() {
	std::printf("Event:\n");
	test_subscribe_emit_clear();
	test_capacity();
	test_order();

	if (g_fail == 0u) {
		std::printf("OK: Event (suscripcion, capacidad, clear y orden)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
