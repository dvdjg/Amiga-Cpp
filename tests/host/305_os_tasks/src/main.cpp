// ============================================================================
// Test HOST-305: M10 — tareas de fondo del mini-SO (`eng::os::TaskSystem`).
// ============================================================================
//
// Valida el ciclo de vida (create/start/suspend/resume/abort/join), que las tareas solo avanzan
// en `run_idle` (nunca fuera), que `preempt` aborta el idle, que `yield_if_preempt` lo ve la tarea,
// el `block`/`unblock`, y el orden por prioridad.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/305_os_tasks

#include <cstdio>

#include <eng/os/task.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

struct Ctx {
	eng::u32 steps = 0;
	eng::u32 limit = 3; // termina a los `limit` slices
};

// Tarea que avanza un paso por slice y termina a los `limit`.
bool counting_task(eng::os::TaskId, void* user, eng::u32) {
	auto* c = static_cast<Ctx*>(user);
	++c->steps;
	return c->steps < c->limit;
}

} // namespace

int main() {
	std::printf("== HOST-305 os tasks ==\n");

	eng::os::TaskSystem ts;
	check(ts.init(8u), "init");

	Ctx a {};
	const eng::os::TaskDesc desc {&counting_task, &a, "counter", 128u};
	const eng::os::TaskId id = ts.create(desc);
	check(id != 0u, "create devuelve id");
	check(ts.state(id) == eng::os::TaskState::Created, "estado Created");

	// Sin start, idle no la corre.
	check(!ts.run_idle(), "idle sin Ready -> false");
	check(a.steps == 0u, "no avanza antes de start");

	check(ts.start(id), "start");
	check(ts.state(id) == eng::os::TaskState::Ready, "estado Ready");

	// un slice; no termina (limit 3)
	(void)ts.run_idle();
	check(a.steps == 1u && ts.state(id) == eng::os::TaskState::Ready, "slice 1 -> Ready");

	// suspend: idle no la corre
	check(ts.suspend(id), "suspend");
	check(!ts.run_idle() && a.steps == 1u, "suspendida no avanza");

	check(ts.resume(id), "resume");
	(void)ts.run_idle();
	check(a.steps == 2u, "slice 2");

	// preempt: idle se niega a correr y la tarea lo ve
	ts.request_preempt();
	check(!ts.run_idle(), "preempt -> idle no corre");
	check(a.steps == 2u, "no avanza con preempt");
	ts.clear_preempt();

	// join: corre hasta Finished (limit 3)
	check(ts.join(id), "join hasta Finished");
	check(ts.state(id) == eng::os::TaskState::Finished, "estado Finished");

	// abort: una tarea suspendida pasa a Aborted (no a Finished)
	Ctx b {};
	const eng::os::TaskId id2 = ts.create({&counting_task, &b, "t2", 128u});
	(void)ts.start(id2);
	check(ts.abort(id2), "abort");
	check(ts.state(id2) == eng::os::TaskState::Aborted, "estado Aborted");

	// block/unblock
	Ctx c {};
	const eng::os::TaskId id3 = ts.create({&counting_task, &c, "t3", 128u});
	(void)ts.start(id3);
	(void)ts.run_idle(); // Running -> vuelve Ready (no terminó)
	check(ts.block(id3), "block");
	check(ts.state(id3) == eng::os::TaskState::Blocked, "estado Blocked");
	check(!ts.run_idle(), "Blocked no corre en idle");
	check(ts.unblock(id3), "unblock");
	check(ts.state(id3) == eng::os::TaskState::Ready, "estado Ready tras unblock");
	(void)ts.abort(id3); // deja el sistema limpio para el test de prioridad

	// prioridad: con las dos Ready, gana la de mayor prioridad; al terminar, corre la otra.
	Ctx hi {};
	Ctx lo {};
	hi.limit = 1u;
	lo.limit = 1u;
	const eng::os::TaskId idlo = ts.create({&counting_task, &lo, "lo", 10u});
	const eng::os::TaskId idhi = ts.create({&counting_task, &hi, "hi", 200u});
	(void)ts.start(idlo);
	(void)ts.start(idhi);
	(void)ts.run_idle();
	check(hi.steps == 1u && lo.steps == 0u, "idle elige la de mayor prioridad primero");
	(void)ts.run_idle();
	check(lo.steps == 1u, "la de menor prioridad corre despues");

	// TaskMsgPort propio + wait_or_idle.
	{
		eng::os::TaskSystem tp;
		check(tp.init(4u), "init (own_port)");
		Ctx d {};
		const eng::os::TaskDesc pd {&counting_task, &d, "port", 128u, /*own_port=*/true, 0u};
		const eng::os::TaskId idd = tp.create(pd);
		check(tp.port(idd).valid(), "own_port -> port() valido");
		eng::os::Msg pm {};
		pm.type = eng::os::MsgType::User;
		check(tp.port(idd)->post(pm), "post a la cola propia");
		eng::os::Msg out {};
		check(tp.port(idd)->wait(out) && out.type == eng::os::MsgType::User,
		      "wait drena la cola propia");

		eng::os::MsgPort<8> main;
		Ctx e {};
		(void)tp.start(tp.create({&counting_task, &e, "w", 128u, false, 0u}));
		check(eng::os::wait_or_idle(main, tp, eng::os::SigVBlank) == 0u && e.steps == 1u,
		      "wait_or_idle sin senal -> idle");
		main.signal(eng::os::SigVBlank);
		check((eng::os::wait_or_idle(main, tp, eng::os::SigVBlank) & eng::os::SigVBlank) != 0u,
		      "wait_or_idle consume la senal");
		check(tp.preempt_requested(), "wait_or_idle pide preempt al fondo");
	}

	// yield_if_preempt: con el hook ligado, refleja el flag de preempt del sistema.
	eng::os::TaskSystem::unbind_preempt_hook();
	check(!eng::os::TaskSystem::yield_if_preempt(), "yield sin hook -> false");
	ts.bind_preempt_hook();
	ts.request_preempt();
	check(eng::os::TaskSystem::yield_if_preempt(), "yield ve el preempt (hook ligado)");
	ts.clear_preempt();
	check(!eng::os::TaskSystem::yield_if_preempt(), "yield sin preempt -> false");
	eng::os::TaskSystem::unbind_preempt_hook();

	if (g_fail == 0) {
		std::printf("OK: tareas de fondo del mini-SO (ciclo de vida + idle + preempt) validadas.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
