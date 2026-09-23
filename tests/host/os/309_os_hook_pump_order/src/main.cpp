// ============================================================================
// Test HOST-309: contrato de orden hook de VBlank -> pump (eng/os).
// ============================================================================
//
// El bucle de polling del `Engine` llama al hook de VBlank (`os::vblank_hook`) **antes** de
// `Game::update`, y `MessagePumpGame::update` drena el puerto. Este test fija ese contrato:
// un mensaje posteado por el hook (p.ej. un `Timer` de `TimerService::poll_and_post`) se
// entrega en el MISMO `update`, tanto con periodo 1 como con periodo 2. El periodo > 1 es el
// caso que en hardware no llega (bug abierto, ver `docs/guides/roadmap/ROADMAP_MINI_OS.md`);
// aquí se valida el contrato puro, sin backend.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/309_os_hook_pump_order

#include <cstdio>

#include <eng/os/message_pump.hpp>
#include <eng/os/port.hpp>
#include <eng/os/timer.hpp>

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

struct FakeCtx {
	struct { u32 frame_index = 0; } frame;
};

struct App {
	int msgs = 0;
	int timers = 0;
	void on_start(FakeCtx&) {}
	void on_msg(const Msg& m) {
		++msgs;
		if (m.type == MsgType::Timer) {
			++timers;
		}
	}
	void on_frame(u32) {}
	void on_render(auto&) {}
};

/// Simula el bucle del `Engine`: hook (postea) -> update (drena) durante `frames` frames.
template <eng::u16 N>
void run_loop(MessagePumpGame<App, N>& game, MsgPort<N>& port, TimerService& timers,
	      eng::u32 frames) {
	int backend = 0;
	FakeCtx ctx {};
	for (u32 f = 1u; f <= frames; ++f) {
		ctx.frame.frame_index = f;
		(void)timers.poll_and_post(port, f, 0u); // hook de VBlank (antes de update)
		game.update(backend, ctx);               // pump (drena el puerto)
	}
}

/// Periodo 1: un `Timer` por frame, entregado en el mismo frame.
void test_period_one_same_frame() {
	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	TimerService timers;
	(void)timers.start(1u, 1u, TimerUnit::Frames, true, 0u, 0u);

	run_loop(game, port, timers, 10u);
	check(game.app.timers == 10, "periodo 1: 10 Timer en 10 frames");
	check(game.app.msgs == 10, "periodo 1: sin mensajes extra");
}

/// Periodo 2: un `Timer` cada 2 frames, entregado en el mismo frame del vencimiento.
void test_period_two_same_frame() {
	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	TimerService timers;
	(void)timers.start(1u, 2u, TimerUnit::Frames, true, 0u, 0u);

	run_loop(game, port, timers, 10u);
	check(game.app.timers == 5, "periodo 2: 5 Timer en 10 frames");
	check(game.app.msgs == 5, "periodo 2: sin mensajes extra");
}

/// Dos timers con periodos distintos: se entregan todos, en su frame.
void test_two_timers() {
	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	TimerService timers;
	(void)timers.start(1u, 1u, TimerUnit::Frames, true, 0u, 0u);
	(void)timers.start(2u, 4u, TimerUnit::Frames, true, 0u, 0u);

	run_loop(game, port, timers, 8u);
	check(game.app.timers == 10, "dos timers: 8 (periodo 1) + 2 (periodo 4)");
}

/// Un mensaje posteado por el juego (no por el hook) se entrega en el `update` siguiente.
void test_user_post_next_update() {
	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	int backend = 0;
	FakeCtx ctx {};

	Msg m {};
	m.type = MsgType::Timer;
	(void)port.post(m);
	game.update(backend, ctx);
	check(game.app.timers == 1, "post previo al update: entregado en ese update");
}

} // namespace

int main() {
	test_period_one_same_frame();
	test_period_two_same_frame();
	test_two_timers();
	test_user_post_next_update();

	if (failures == 0) {
		std::printf("OK: el hook postea y el pump entrega en el mismo update (periodo 1 y 2).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
