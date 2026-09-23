// ============================================================================
// Test HOST-307: integration del fondo en el bucle de mensajes
// (eng/os/message_pump.hpp + eng/os/task.hpp, M10).
// ============================================================================
//
// Valida que `MessagePumpGame`:
//   - si hubo mensajes en el frame, NO da slice a las tareas de fondo;
//   - si no hubo mensajes, da UN slice de idle (`run_idle`);
//   - si un productor pide `preempt`, no da idle y limpia la marca;
//   - sigue llamando a `on_frame` en todos los casos.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/307_os_pump_tasks

#include <cstdio>

#include <eng/os/message_pump.hpp>
#include <eng/os/task.hpp>

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
	int frames = 0;
	void on_start(FakeCtx&) {}
	void on_msg(const Msg&) { ++msgs; }
	void on_frame(u32) { ++frames; }
	void on_render(auto&) {}
};

// Contador de slices: la tarea vive indefinidamente (`true`) y suma un paso por `poll()`.
int g_steps = 0;
bool step_task(TaskId, void*, eng::u32) {
	++g_steps;
	return true;
}

/// Un puerto con mensajes preempta el fondo; sin mensajes, se le da un slice.
void test_idle_only_without_messages() {
	g_steps = 0;
	TaskSystem ts;
	check(ts.init(), "tasks.init");
	TaskDesc d {};
	d.fn = &step_task;
	d.name = "bg";
	const TaskId id = ts.create(d);
	check(id != 0u, "tasks.create");
	check(ts.start(id), "tasks.start");

	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	game.bind_tasks(ts);
	int backend = 0;
	FakeCtx ctx {};

	// Frame con mensaje: el fondo NO avanza (el frame gana).
	Msg m {};
	m.type = MsgType::KeyDown;
	(void)port.post(m);
	game.update(backend, ctx);
	check(game.app.msgs == 1 && game.app.frames == 1, "frame con mensaje");
	check(g_steps == 0, "con mensaje no hay idle");

	// Frame sin mensajes: un slice de idle.
	game.update(backend, ctx);
	check(g_steps == 1, "sin mensaje -> un slice de idle");
	check(game.app.frames == 2, "on_frame sigue cada frame");

	game.update(backend, ctx);
	check(g_steps == 2, "otro frame sin mensaje -> otro slice");
}

/// Un `preempt` pendiente (de un productor) impide el idle y se limpia en el bucle.
void test_preempt_blocks_idle() {
	g_steps = 0;
	TaskSystem ts;
	(void)ts.init();
	TaskDesc d {};
	d.fn = &step_task;
	const TaskId id = ts.create(d);
	(void)ts.start(id);

	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	game.bind_tasks(ts);
	int backend = 0;
	FakeCtx ctx {};

	ts.request_preempt();
	game.update(backend, ctx); // sin mensajes, pero con preempt pedido
	check(g_steps == 0, "preempt -> sin idle");
	check(!ts.preempt_requested(), "el bucle limpia el preempt");

	game.update(backend, ctx); // ya limpio: vuelve a dar slice
	check(g_steps == 1, "tras limpiar el preempt -> idle");
}

/// Sin `TaskSystem` ligado, el bucle funciona igual (sin idle).
void test_without_tasks() {
	g_steps = 0;
	MsgPort<8> port;
	MessagePumpGame<App, 8> game;
	game.bind_port(port);
	int backend = 0;
	FakeCtx ctx {};
	game.update(backend, ctx);
	check(game.app.frames == 1 && g_steps == 0, "sin tasks no hay idle");
}

} // namespace

int main() {
	test_idle_only_without_messages();
	test_preempt_blocks_idle();
	test_without_tasks();

	if (failures == 0) {
		std::printf("OK: fondo integrado en el bucle (idle si no hay mensajes; preempt respetado).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
