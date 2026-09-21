// ============================================================================
// Test HOST-236: prioridad, peek, coalescing y VBlank latched (eng/os/port.hpp).
// ============================================================================
//
// Valida `PrioMsgQueue` (los High se cuelan, `peek` sin retirar, `has_at_least`,
// `push_mouse_coalesced`) y `VBlankLatch` (secuencia + frames perdidos, como maximo uno pendiente).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/236_os_latched

#include <cstdio>

#include <eng/os/port.hpp>

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

void test_prio_order() {
	PrioMsgQueue<8> q;
	Msg low {};
	low.type = MsgType::FileDone; // Low
	Msg norm {};
	norm.type = MsgType::VBlank; // Normal
	Msg high {};
	high.type = MsgType::KeyDown; // High
	(void)q.push(low, MsgPrio::Low);
	(void)q.push(norm, MsgPrio::Normal);
	(void)q.push(high, MsgPrio::High);

	Msg out {};
	MsgPrio p {};
	check(q.pop(out, &p) && out.type == MsgType::KeyDown && p == MsgPrio::High,
	      "el High se cuela el primero");
	check(q.pop(out, &p) && out.type == MsgType::VBlank && p == MsgPrio::Normal,
	      "despues el Normal");
	check(q.pop(out, &p) && out.type == MsgType::FileDone && p == MsgPrio::Low,
	      "el Low al final");
}

void test_peek_and_has() {
	PrioMsgQueue<8> q;
	check(!q.has_at_least(MsgPrio::Low), "cola vacia no tiene nada");
	Msg m {};
	m.type = MsgType::FileDone;
	(void)q.push(m, MsgPrio::Low);
	check(q.has_at_least(MsgPrio::Low), "tiene Low");
	check(!q.has_at_least(MsgPrio::Normal), "no tiene Normal");
	m.type = MsgType::KeyDown;
	(void)q.push(m, MsgPrio::High);

	Msg out {};
	check(q.peek(out) && out.type == MsgType::KeyDown, "peek devuelve el de mas prioridad");
	check(q.peek(out) && out.type == MsgType::KeyDown, "peek no retira");
	check(q.pop(out) && out.type == MsgType::KeyDown, "pop tras peek");
}

void test_coalesce() {
	PrioMsgQueue<8> q;
	Msg m {};
	m.type = MsgType::MouseMove;
	m.payload.mouse = {1, 1, 0, 0, 0};
	(void)q.push_mouse_coalesced(m);
	m.payload.mouse = {2, 2, 0, 0, 0};
	(void)q.push_mouse_coalesced(m);
	m.payload.mouse = {3, 3, 0, 0, 0};
	(void)q.push_mouse_coalesced(m);

	Msg out {};
	check(q.pop(out) && out.payload.mouse.x == 3, "gana el ultimo MouseMove");
	check(q.empty(), "solo un MouseMove en la cola");
}

void test_vblank_latched() {
	VBlankLatch latch;
	Msg out {};
	check(!take_vblank(latch, out), "sin VBlank pendiente");

	latch.signal(10u);
	latch.signal(11u);
	latch.signal(12u);
	check(take_vblank(latch, out), "hay VBlank pendiente");
	check(out.payload.vblank.sequence == 12u, "secuencia = la ultima");
	check(out.payload.vblank.missed == 2u, "missed = 2 pisados");
	check(!take_vblank(latch, out), "como maximo uno pendiente");

	latch.signal(13u);
	check(take_vblank(latch, out) && out.payload.vblank.missed == 0u,
	      "sin missed tras consumir");
	check(out.payload.vblank.sequence == 13u, "secuencia avanza");
}

} // namespace

int main() {
	test_prio_order();
	test_peek_and_has();
	test_coalesce();
	test_vblank_latched();

	if (failures == 0) {
		std::printf("OK: prioridad, peek, coalescing y VBlank latched validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
