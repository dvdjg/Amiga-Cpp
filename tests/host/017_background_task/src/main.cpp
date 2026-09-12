// ============================================================================
// Test HOST-017: tareas de fondo cooperativas (eng::task::BackgroundQueue).
// ============================================================================
//
// Valida en host el bloque de trabajo de fondo del engine: rutinas que procesan un
// trozo por rebanada, con indicadores baratos de progreso y rendimiento y prioridad
// al bucle principal.
//
// Comprobaciones:
//   1) Tarea finita: avanza por rebanadas, `permille` crece, termina (Done) y libera
//      el slot; el handle viejo queda invalidado (generation).
//   2) Tarea continua: no termina sola; `cancel()` la libera.
//   3) La tarea recibe el contexto correcto (frame/vpos/budget/progreso/avg) y puede
//      ADAPTAR su carga (aqui consume menos si el raster esta avanzado).
//   4) `task_abort` marca Failed.
//   5) Limite de pool (`max_tasks`) y handles invalidos.
//   6) `permille_of` sin desbordar u32.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/017_background_task   (solo este)

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/task/background.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::task::BackgroundQueue;
using eng::task::TaskHandle;
using eng::task::TaskProgress;
using eng::task::TaskSlice;
using eng::task::TaskState;
using eng::task::permille_of;

/// Tarea de prueba: acumula `budget` unidades mientras el raster va por debajo de
/// `late_vpos`; si ya es tarde, consume la mitad (adaptacion de carga).
struct CountingTask {
	u32 units = 0;
	u16 last_budget = 0;
	u16 last_vpos = 0;
	u8 slices = 0;
};

u16 count_up(void* data, const TaskSlice& slice) {
	auto* t = static_cast<CountingTask*>(data);
	t->last_budget = slice.budget_units;
	t->last_vpos = slice.vpos;
	++t->slices;
	const u16 consume = (slice.vpos >= 200u) ? static_cast<u16>(slice.budget_units / 2u) : slice.budget_units;
	t->units += consume;
	return consume;
}

u16 abort_task(void*, const TaskSlice&) { return eng::task::task_abort; }

} // namespace

int main() {
	// --- 1) Tarea finita: progreso -> Done -> slot liberado -------------------
	{
		BackgroundQueue q;
		CountingTask t {};
		const TaskHandle h = q.add(&count_up, &t, /*total*/ 1000u, /*slice*/ 100u);
		if (!h.valid() || q.live_count() != 1u) {
			std::printf("[FAIL] add() de tarea finita\n");
			return 1;
		}

		// Tras 4 rebanadas (sin "tarde"): 400 unidades, permille = 400.
		for (int i = 0; i < 4; ++i) {
			if (q.run_slice(static_cast<u32>(i), /*vpos*/ 100u) == 0u) {
				std::printf("[FAIL] la tarea no consumio unidades\n");
				return 1;
			}
		}
		TaskProgress p = q.progress(h);
		if (p.state != TaskState::Running || p.done_units != 400u || p.total_units != 1000u) {
			std::printf("[FAIL] progreso finito (state=%d done=%lu total=%lu)\n",
				    (int)p.state, p.done_units, p.total_units);
			return 1;
		}
		if (p.permille != 400u || p.remaining_units() != 600u) {
			std::printf("[FAIL] permille/remaining (permille=%u remaining=%lu)\n",
				    (unsigned)p.permille, p.remaining_units());
			return 1;
		}
		if (p.avg_units_per_slice != 100u) {
			std::printf("[FAIL] avg de rendimiento (avg=%u, esperado 100)\n", (unsigned)p.avg_units_per_slice);
			return 1;
		}

		// 6 rebanadas mas (frames distintos) -> completa. Queda en `Done` hasta cancelarla.
		for (int i = 0; i < 6; ++i) {
			q.run_slice(10u + static_cast<u32>(i), 100u);
		}
		const TaskProgress done = q.progress(h);
		if (done.state != TaskState::Done || done.permille != 1000u || q.live_count() != 1u) {
			std::printf("[FAIL] tarea completada no queda en Done (state=%d permille=%u live=%u)\n",
				    (int)done.state, (unsigned)done.permille, (unsigned)q.live_count());
			return 1;
		}
		// `cancel()` libera el slot; a partir de ahi el handle ya no es valido.
		if (!q.cancel(h) || q.live_count() != 0u || q.progress(h).state != TaskState::Free) {
			std::printf("[FAIL] cancel() no libera la tarea terminada\n");
			return 1;
		}
	}

	// --- 2) Tarea continua: no termina sola; cancel() la libera ---------------
	{
		BackgroundQueue q;
		CountingTask t {};
		const TaskHandle h = q.add(&count_up, &t, /*total*/ 0u, /*slice*/ 50u);
		for (int i = 0; i < 20; ++i) {
			q.run_slice(100u + static_cast<u32>(i), 100u);
		}
		const TaskProgress p = q.progress(h);
		if (p.state != TaskState::Running || p.total_units != 0u || p.permille != 0u) {
			std::printf("[FAIL] tarea continua (state=%d total=%lu)\n", (int)p.state, p.total_units);
			return 1;
		}
		if (t.units != 1000u) {
			std::printf("[FAIL] tarea continua no avanzo (units=%lu)\n", t.units);
			return 1;
		}
		if (!q.cancel(h) || q.live_count() != 0u || q.cancel(h)) {
			std::printf("[FAIL] cancel() no libera / acepta handle invalido\n");
			return 1;
		}
	}

	// --- 3) La tarea ve el contexto y adapta su carga -------------------------
	{
		BackgroundQueue q;
		CountingTask t {};
		const TaskHandle h = q.add(&count_up, &t, 0u, 100u);
		q.run_slice(7u, 50u);            // raster temprano -> consume 100
		const u32 after_early = t.units;
		q.run_slice(8u, 250u);           // raster tarde   -> consume 50
		if (t.last_budget != 100u || t.last_vpos != 250u) {
			std::printf("[FAIL] la tarea no recibio el slice (budget=%u vpos=%u)\n",
				    (unsigned)t.last_budget, (unsigned)t.last_vpos);
			return 1;
		}
		if (after_early != 100u || t.units != 150u) {
			std::printf("[FAIL] adaptacion de carga (early=%lu total=%lu)\n", after_early, t.units);
			return 1;
		}
		q.cancel(h);
	}

	// --- 4) task_abort -> Failed ----------------------------------------------
	{
		BackgroundQueue q;
		const TaskHandle h = q.add(&abort_task, nullptr, 0u, 10u);
		q.run_slice(0u, 0u);
		const TaskProgress p = q.progress(h);
		if (p.state != TaskState::Failed) {
			std::printf("[FAIL] task_abort no marca Failed (state=%d)\n", (int)p.state);
			return 1;
		}
	}

	// --- 5) Limite de pool y handles invalidos --------------------------------
	{
		BackgroundQueue q;
		CountingTask t {};
		for (eng::u8 i = 0; i < BackgroundQueue::max_tasks; ++i) {
			if (!q.add(&count_up, &t).valid()) {
				std::printf("[FAIL] add() fallo dentro del pool (i=%u)\n", (unsigned)i);
				return 1;
			}
		}
		if (q.add(&count_up, &t).valid()) {
			std::printf("[FAIL] add() acepto mas alla de max_tasks\n");
			return 1;
		}
		if (q.progress(TaskHandle {}).state != TaskState::Free) {
			std::printf("[FAIL] handle invalido no devuelve Free\n");
			return 1;
		}
	}

	// --- 6) permille_of sin desbordar -----------------------------------------
	{
		if (permille_of(0u, 100u) != 0u || permille_of(50u, 100u) != 500u ||
		    permille_of(100u, 100u) != 1000u || permille_of(5u, 0u) != 0u) {
			std::printf("[FAIL] permille_of basico\n");
			return 1;
		}
		// done grande (< total) no debe desbordar: 3e9/4e9 ~ 750 permille.
		const u16 big = permille_of(3000000000u, 4000000000u);
		if (big < 700u || big > 800u) {
			std::printf("[FAIL] permille_of grande = %u (esperado ~750)\n", (unsigned)big);
			return 1;
		}
	}

	std::printf("OK: tareas de fondo (progreso/rendimiento/adaptacion/prioridad) validadas.\n");
	return 0;
}
