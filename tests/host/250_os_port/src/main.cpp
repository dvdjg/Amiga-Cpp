// Test host del puerto de mensajes del mini-SO (`eng/os/port.hpp`).
#include <cstdio>

#include <eng/os/port.hpp>

using eng::os::Msg;
using eng::os::MsgPort;
using eng::os::MsgType;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}
} // namespace

int main() {
	std::printf("== HOST-250 os_port ==\n");

	MsgPort<4> p;
	check(p.empty(), "recien creado: vacio");
	check(!p.signalled(), "sin senal");

	check(p.post(Msg {MsgType::BlitDone, 1u, 0x1234u}), "post 1");
	check(p.post(Msg {MsgType::VBlank, 2u, 0u}), "post 2");
	check(p.signalled(), "senalado tras post");
	check(!p.empty(), "no vacio");

	Msg m {};
	check(p.try_get(m) && m.type == MsgType::BlitDone && m.code == 1u && m.data == 0x1234u,
	      "FIFO: primero BlitDone");
	check(p.try_get(m) && m.type == MsgType::VBlank, "FIFO: segundo VBlank");
	check(!p.try_get(m), "vacio tras drenar");

	// Cola de capacidad N: cabe N-1 (una ranura de separacion).
	MsgPort<4> q;
	for (int i = 0; i < 3; ++i) {
		check(q.post(Msg {MsgType::User, static_cast<eng::u16>(i), 0u}), "post hasta llenar");
	}
	check(!q.post(Msg {MsgType::User, 9u, 0u}), "post en cola llena -> false");
	q.clear();
	check(q.empty(), "clear vacia");
	check(!q.signalled(), "clear quita la senal");

	if (g_fail == 0) {
		std::printf("OK: os::MsgPort validado (FIFO, llena, senal, clear).\n");
		return 0;
	}
	std::printf("FAIL: %d\n", g_fail);
	return 1;
}
