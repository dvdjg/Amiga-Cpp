// ============================================================================
// Test HOST-250: `eng::os::MsgQueue` (anillo SPSC) y `MsgPort` (prioridad + senal).
// ============================================================================
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/250_os_port

#include <cstdio>

#include <eng/os/message.hpp>
#include <eng/os/port.hpp>

using eng::os::Msg;
using eng::os::MsgPort;
using eng::os::MsgQueue;
using eng::os::MsgType;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

Msg make(MsgType t) {
	Msg m {};
	m.type = t;
	return m;
}

void test_queue() {
	MsgQueue<4> q;
	check(q.empty(), "MsgQueue: recien creada vacia");
	check(q.push_isr(make(MsgType::BlitDone)), "MsgQueue: push_isr 1");
	check(q.push_isr(make(MsgType::VBlank)), "MsgQueue: push_isr 2");
	Msg m {};
	check(q.peek(m) && m.type == MsgType::BlitDone, "MsgQueue: peek sin retirar");
	check(q.pop(m) && m.type == MsgType::BlitDone, "MsgQueue: pop FIFO 1");
	check(q.pop(m) && m.type == MsgType::VBlank, "MsgQueue: pop FIFO 2");
	check(!q.pop(m), "MsgQueue: vacia tras drenar");

	// Capacidad N: caben N-1 (una ranura de separacion); al llenar, descarta y cuenta.
	for (int i = 0; i < 3; ++i) {
		(void)q.push_isr(make(MsgType::User));
	}
	check(!q.push_isr(make(MsgType::User)), "MsgQueue: llena -> false");
	check(q.overflows() == 1u, "MsgQueue: overflow contado");
}

void test_port_priority() {
	MsgPort<8> p;
	check(p.empty(), "MsgPort: vacio");

	Msg low = make(MsgType::BlitDone); // prioridad Low
	Msg normal = make(MsgType::VBlank); // prioridad Normal
	check(p.post(low), "MsgPort: post Low");
	check(p.post(normal), "MsgPort: post Normal");
	check(!p.empty(), "MsgPort: no vacio");

	Msg m {};
	check(p.pop(m) && m.type == MsgType::VBlank, "MsgPort: el Normal se cuela ante el Low");
	check(p.pop(m) && m.type == MsgType::BlitDone, "MsgPort: despues el Low");
	check(!p.pop(m), "MsgPort: vacio tras drenar");
	check(p.peek(m) == false, "MsgPort: peek en vacio -> false");
}

void test_port_signals() {
	MsgPort<8> p;
	p.post(make(MsgType::KeyDown)); // signal_for(KeyDown) = SigInput
	p.signal(eng::os::SigFile);
	check(p.pending(eng::os::SigInput) == eng::os::SigInput,
	      "MsgPort: pending ve la senal de input sin consumirla");
	check(p.pending(eng::os::SigTimer) == 0u, "MsgPort: pending ignora senales no pedidas");
	check(p.take_signals(eng::os::SigInput) == eng::os::SigInput, "MsgPort: take_signals consume");
	check(p.take_signals(eng::os::SigInput) == 0u, "MsgPort: ya consumida");
	check(p.pending(eng::os::SigFile) == eng::os::SigFile, "MsgPort: la senal file sigue puesta");
}

} // namespace

int main() {
	std::printf("== HOST-250 os_port ==\n");
	test_queue();
	test_port_priority();
	test_port_signals();
	if (g_fail == 0) {
		std::printf("OK: os::MsgQueue (SPSC) + MsgPort (prioridad/senales) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d\n", g_fail);
	return 1;
}
