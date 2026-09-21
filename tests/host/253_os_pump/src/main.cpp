// ============================================================================
// Test HOST-253: bucle reactivo (eng/os/message_pump.hpp).
// ============================================================================
//
// Valida que `MessagePumpGame<App>` drena el puerto en `update` (entregando cada mensaje a
// `on_msg`), llama a `on_frame` con el frame y a `on_render`, y que sin puerto sigue funcionando.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/253_os_pump

#include <cstdio>

#include <eng/os/message_pump.hpp>

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
	int starts = 0;
	int msgs = 0;
	int frames = 0;
	int renders = 0;
	u32 last_frame = 0xffffffffu;
	MsgType last_type = MsgType::None;

	void on_start(FakeCtx&) { ++starts; }
	void on_msg(const Msg& m) {
		++msgs;
		last_type = m.type;
	}
	void on_frame(u32 f) {
		++frames;
		last_frame = f;
	}
	void on_render() { ++renders; }
};

void test_pump() {
	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	int backend = 0;
	FakeCtx ctx {};

	game.init(backend, ctx);
	check(game.app.starts == 1, "on_start");

	Msg m {};
	m.type = MsgType::KeyDown;
	(void)port.post(m);
	m.type = MsgType::VBlank;
	(void)port.post(m);

	ctx.frame.frame_index = 7u;
	game.update(backend, ctx);
	check(game.app.msgs == 2, "drena los 2 mensajes");
	check(game.app.frames == 1 && game.app.last_frame == 7u, "on_frame con el frame");
	check(port.empty(), "el puerto queda drenado");

	game.render(backend, ctx);
	check(game.app.renders == 1, "on_render");
}

void test_without_port() {
	MessagePumpGame<App> game;
	int backend = 0;
	FakeCtx ctx {};
	game.init(backend, ctx);
	ctx.frame.frame_index = 3u;
	game.update(backend, ctx);
	check(game.app.frames == 1 && game.app.last_frame == 3u, "sin puerto sigue el frame");
	check(game.app.msgs == 0, "sin puerto no hay mensajes");
}

} // namespace

int main() {
	test_pump();
	test_without_port();

	if (failures == 0) {
		std::printf("OK: bucle reactivo (drena, on_frame, on_render) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
