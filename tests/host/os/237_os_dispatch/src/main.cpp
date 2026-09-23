// ============================================================================
// Test HOST-237: despacho por tabla (eng/os/dispatch.hpp).
// ============================================================================
//
// Valida `HandlerTable` (default por `fill`, handler por tipo, cobertura de todos los `MsgType`,
// `nullptr` seguro) y `dispatch_all` (drena el puerto).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/237_os_dispatch

#include <cstdio>

#include <eng/os/dispatch.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

struct Ctx {
	int counts[static_cast<u8>(MsgType::COUNT)] {};
};

void on_default(Ctx& c, const Msg& m) { c.counts[static_cast<u8>(m.type)] += 100; }
void on_key(Ctx& c, const Msg&) { c.counts[static_cast<u8>(MsgType::KeyDown)] += 1; }

void test_table() {
	HandlerTable<Ctx> table;
	table.fill(&on_default);
	check(table.get(MsgType::User) == &on_default, "fill pone el default");
	check(table.get(MsgType::Quit) == &on_default, "fill cubre todos");

	table.set(MsgType::KeyDown, &on_key);
	Ctx c {};
	Msg m {};
	m.type = MsgType::KeyDown;
	table.dispatch(c, m);
	check(c.counts[static_cast<u8>(MsgType::KeyDown)] == 1, "handler especifico");
	m.type = MsgType::User;
	table.dispatch(c, m);
	check(c.counts[static_cast<u8>(MsgType::User)] == 100, "default");

	// Cobertura: todos los tipos tienen handler no nulo.
	bool covered = true;
	for (u8 i = 0u; i < static_cast<u8>(MsgType::COUNT); ++i) {
		if (table.handlers[i] == nullptr) {
			covered = false;
		}
	}
	check(covered, "la tabla cubre todos los MsgType");

	// `nullptr` no revienta.
	HandlerTable<Ctx> empty;
	empty.set(MsgType::Quit, nullptr);
	empty.dispatch(c, m);
	check(true, "dispatch con nullptr es seguro");
}

void test_dispatch_all() {
	MsgPort<8> port;
	HandlerTable<Ctx> table;
	table.fill(&on_default);
	Ctx c {};

	Msg m {};
	m.type = MsgType::User;
	(void)port.post(m);
	m.type = MsgType::Timer;
	(void)port.post(m);

	dispatch_all(table, c, port);
	check(c.counts[static_cast<u8>(MsgType::User)] == 100, "dispatch_all: User");
	check(c.counts[static_cast<u8>(MsgType::Timer)] == 100, "dispatch_all: Timer");
	check(port.empty(), "dispatch_all drena la cola");
}

} // namespace

int main() {
	test_table();
	test_dispatch_all();

	if (failures == 0) {
		std::printf("OK: despacho por tabla (cobertura, default, drenado) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
