// ============================================================================
// Test HOST-219: nucleo del mini-SO (eng/os/message.hpp + port.hpp).
// ============================================================================
//
// Valida `MsgType`/`Msg` (trivial, contiguo), `MsgQueue` (FIFO, overflow), `MsgPort` (senales
// OR-eadas, take_signals selectivo) y `prio_of`/`signal_for`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/219_os_core

#include <cstdio>
#include <type_traits>

#include <eng/os/message.hpp>
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

void test_message() {
	static_assert(std::is_trivially_copyable_v<Msg>, "Msg debe ser trivialmente copiable");
	static_assert(static_cast<u8>(MsgType::None) == 0u, "None = 0");
	static_assert(static_cast<u8>(MsgType::COUNT) == static_cast<u8>(MsgType::Quit) + 1u,
		      "MsgType contiguo desde 0");
	check(true, "Msg trivial y MsgType contiguo");
}

void test_queue() {
	MsgQueue<4> q; // capacidad util = 3 (un slot reservado)
	check(q.empty(), "cola vacia");
	Msg m {};
	m.type = MsgType::User;
	m.payload.user = {1u, 2u, 3u};
	check(q.push_isr(m), "push 1");
	m.payload.user = {4u, 5u, 6u};
	check(q.push_isr(m), "push 2");
	m.payload.user = {7u, 8u, 9u};
	check(q.push_isr(m), "push 3");

	Msg pk {};
	check(q.peek(pk) && pk.payload.user.code == 1u, "peek no retira el primero");
	check(q.peek(pk) && pk.payload.user.code == 1u, "peek sigue devolviendo el primero");

	Msg out {};
	check(q.pop(out) && out.payload.user.code == 1u, "FIFO 1");
	check(q.pop(out) && out.payload.user.code == 4u, "FIFO 2");
	check(q.pop(out) && out.payload.user.code == 7u, "FIFO 3");
	check(!q.pop(out), "cola vacia tras drenar");

	for (int i = 0; i < 3; ++i) {
		(void)q.push_isr(m);
	}
	check(!q.push_isr(m), "push con cola llena falla");
	check(q.overflows() == 1u, "overflow contado");
}

void test_signals() {
	MsgPort<8> port;
	Msg m {};
	m.type = MsgType::VBlank;
	check(port.post(m), "post VBlank");
	m.type = MsgType::KeyDown;
	check(port.post(m), "post KeyDown");
	check((port.signalled & SigVBlank) != 0u, "SigVBlank puesto");
	check((port.signalled & SigInput) != 0u, "SigInput puesto");
	check((port.signalled & SigHigh) != 0u, "SigHigh puesto por KeyDown");

	const u32 got = port.take_signals(SigVBlank);
	check(got == SigVBlank, "take_signals consume solo lo pedido");
	check((port.signalled & SigVBlank) == 0u, "SigVBlank consumido");
	check((port.signalled & SigInput) != 0u, "SigInput intacto");

	// Coalescing de senales: dos VBlank, una sola senal.
	MsgPort<8> p2;
	Msg v {};
	v.type = MsgType::VBlank;
	(void)p2.post(v);
	(void)p2.post(v);
	check((p2.signalled & SigVBlank) != 0u, "dos mensajes, una senal");
}

void test_prio_and_signal() {
	check(prio_of(MsgType::KeyDown) == MsgPrio::High, "KeyDown High");
	check(prio_of(MsgType::MouseButton) == MsgPrio::High, "MouseButton High");
	check(prio_of(MsgType::VBlank) == MsgPrio::Normal, "VBlank Normal");
	check(prio_of(MsgType::FileDone) == MsgPrio::Low, "FileDone Low");
	check(signal_for(MsgType::FileError) == SigFile, "FileError -> SigFile");
	check(signal_for(MsgType::Timer) == SigTimer, "Timer -> SigTimer");
}

} // namespace

int main() {
	test_message();
	test_queue();
	test_signals();
	test_prio_and_signal();

	if (failures == 0) {
		std::printf("OK: mini-SO nucleo (Msg, cola SPSC, senales, prioridad) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
