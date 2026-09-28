// Test HOST-369: **bucle cerrado de la intención** — al ejecutarse una petición de una
// `IntentQueue`, el `IntentDonePoster` postea un `Msg` `IntentDone` con su `ticket` al puerto del
// mini-SO. Es el contrato de completación del planner (INTENT_PLANNER.md §5).
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/os/369_intent_done

#include <eng/graphics/intent_queue.hpp>
#include <eng/os/intent_done.hpp>

#include <cstdio>

using eng::graphics::DrawIntent;
using eng::graphics::DrawKind;
using eng::graphics::DrawQueue;
using eng::graphics::Ticket;
using eng::os::IntentDonePoster;
using eng::os::Msg;
using eng::os::MsgPort;
using eng::os::MsgType;

namespace {
int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Via de doble: siempre lista; no hace nada (el aviso es lo que se prueba).
struct FakeExec {
	bool ready() { return true; }
	void run(const DrawIntent&) {}
};
} // namespace

int main() {
	std::printf("== HOST-369 intent_done ==\n");

	MsgPort<8u> port;
	FakeExec ex;
	DrawQueue<4u, FakeExec, IntentDonePoster<8u>> q;
	q.bind(ex);
	q.bind_done(IntentDonePoster<8u> {&port});

	const Ticket t = q.enqueue(DrawIntent {DrawKind::Rect});
	check(port.empty(), "declarar no publica evento (aun)");

	q.flush();
	Msg m;
	check(port.pop(m), "la completacion llega como mensaje");
	check(m.type == MsgType::IntentDone && m.payload.user.a == t,
	      "IntentDone con el ticket correcto");
	check(!port.pop(m), "no hay mas mensajes");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: IntentDonePoster (intencion -> cola -> evento) validado.\n");
	return 0;
}
