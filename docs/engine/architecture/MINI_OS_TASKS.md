# Mini-SO: tareas asíncronas de fondo (`eng::os::TaskSystem`)

Sistema de **tareas de fondo con ciclo de vida** sobre el mini-SO cooperativo
([`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md)). Las tareas **no compiten** con el bucle de
mensajes: solo corren en **idle** (cuando la cola principal está vacía) y se **suspenden en cuanto
una IRQ marca señales** (VBlank, input, disco…). Así el frame y la entrada **siempre ganan**, y el
fondo usa las migajas de CPU que hoy se pierden esperando el VBlank.

No hay preempción dura: basta con que el idle compruebe señales/pending con frecuencia (y un flag
`preempt` que pone la ISR), y con que las tareas cedan en sus bucles largos. El modelo es el de
`BackgroundQueue` ([`BACKGROUND_TASKS.md`](BACKGROUND_TASKS.md)) llevado a tareas con **identidad,
estado y cola propia**.

## 1. Modelo

```text
loop:
  while (hay msgs o señales prioritarios)
      drenar cola principal            ← siempre primero

  if (idle_tasks_ready)
      ejecutar un slice de tarea de fondo
      (la tarea hace yield o el slice termina)
      si la ISR puso SigVBlank/SigInput → abortar slice y volver arriba
  else
      cpu_wait_for_interrupt()         ← sleep real si no hay tareas
```

Las tareas son **funciones `poll()`** que avanzan un poco y vuelven (o, opcionalmente,
**corrutinas** que ceden con `co_await`, §8).

## 2. API

```cpp
// eng/os/task.hpp
namespace eng::os {

enum class TaskState : eng::u8 {
	Invalid = 0, Created, Ready, Running, Blocked, Suspended, Finished, Aborted,
};

using TaskId = eng::u16; // 0 = inválido

/// Cola propia de una tarea (misma prioridad que la del SO, tamaño reducido).
struct TaskMsgPort {
	PrioMsgQueue<16> q;
	bool wait(Msg& out);   ///< solo drena ESTA cola (nunca la del SO)
	bool post(const Msg& m, MsgPrio p = MsgPrio::Normal);
	bool peek(Msg& out) const;
};

/// Función de tarea: `true` = sigue viva; `false` = terminó. `slice_us` es orientativo.
using TaskFn = bool (*)(TaskId self, void* user, eng::u32 slice_us);

struct TaskDesc {
	TaskFn fn = nullptr;
	void* user = nullptr;
	const char* name = nullptr;
	eng::u8 priority = 128;  ///< mayor = más slices en idle
	bool own_port = false;   ///< crea un `TaskMsgPort`
	eng::u16 stack_words = 0;///< 0 = sin stack propio (solo poll cooperativo)
};

class TaskSystem {
public:
	bool init(eng::u8 max_tasks = 8);
	void shutdown();

	TaskId create(const TaskDesc& d);
	bool start(TaskId id);
	bool suspend(TaskId id);
	bool resume(TaskId id);
	bool abort(TaskId id);
	void join(TaskId id);                       ///< espera Finished (solo desde main)

	TaskState state(TaskId id) const;
	TaskMsgPort* port(TaskId id);               ///< nullptr si no `own_port`

	/// Llamar cuando la cola principal está vacía. `true` si ejecutó trabajo.
	bool run_idle(eng::u32 max_slice_us = 500);

	/// La ISR / `wait()` avisa: hay trabajo del SO → no seguir en idle.
	void request_preempt() { m_preempt = 1u; }
	[[nodiscard]] bool preempt_requested() const { return m_preempt != 0u; }
	void clear_preempt() { m_preempt = 0u; }

	/// La propia tarea la llama en bucles largos.
	[[nodiscard]] static bool yield_if_preempt();

	void on_vblank(eng::u32 frame);             ///< timers de tareas
};

inline TaskSystem g_tasks {};

} // namespace eng::os
```

## 3. Integración con el bucle principal

`wait_or_idle` es el punto donde el SO **duerme o aprovecha** el hueco: si hay señales, las consume
y devuelve el control al drenado de mensajes; si no, ejecuta **un slice de fondo** y reintenta.

```cpp
eng::u32 wait_or_idle(eng::u32 mask, eng::u32 idle_slice_us) {
	for (;;) {
		const eng::u32 s = g_port.signals;
		if ((s & mask) != 0u) {
			g_port.signals = s & ~mask;
			g_tasks.request_preempt();      // el fondo debe ceder
			return s & mask;
		}
		if (!g_tasks.run_idle(idle_slice_us)) {
			cpu_wait_for_interrupt();       // sleep real si no hay tareas
		}
		// Tras la IRQ, `signals` estará puesto → vuelve arriba.
	}
}

void app_main() {
	eng::os::g_tasks.init(8);
	bool running = true;
	while (running) {
		const eng::u32 sig = wait_or_idle(
			SigVBlank | SigInput | SigFile | SigTimer | SigQuit, /*idle_us*/ 200);
		g_tasks.clear_preempt();

		// 1) Prioridad absoluta: mensajes del SO.
		Msg m;
		while (g_port.queue.pop(m)) { dispatch_main(m); }

		// 2) VBlank latched (secuencia + frames perdidos).
		Msg vb;
		if (take_vblank(vb)) { on_vblank(vb); }

		// 3) Solo si no hay nada pendiente: fondo.
		if (!g_port.queue.has_at_least(MsgPrio::Low) && !vblank_pending() &&
		    g_port.signals == 0u) {
			(void)g_tasks.run_idle(/*slice*/ 300);
		}
	}
}
```

La ISR (VBlank, CIA, disco) hace dos cosas: `g_port.signal(...)` (cola del SO) y
`g_tasks.request_preempt()` (avisa al slice en curso). No hay que salvar el PC a mitad de
instrucción: el `yield_if_preempt()` de la tarea detecta el flag y sale.

## 4. Scheduler de idle

```cpp
bool TaskSystem::run_idle(eng::u32 max_slice_us) {
	if (m_preempt != 0u) { return false; }

	// Ready con más prioridad y menos edad (fairness simple).
	TaskId best = 0;
	eng::u8 best_score = 0;
	for (eng::u8 i = 0; i < m_cap; ++i) {
		Task& t = m_tasks[i];
		if (t.state != TaskState::Ready) { continue; }
		const eng::u8 score = static_cast<eng::u8>(t.desc.priority + (255u - t.age));
		if (best == 0u || score >= best_score) { best = static_cast<TaskId>(i + 1u); best_score = score; }
	}
	if (best == 0u) { return false; }

	Task& t = m_tasks[best - 1u];
	t.state = TaskState::Running;
	m_current = best;
	t.age = static_cast<eng::u8>(t.age + 1u);

	const eng::u32 t0 = ticks_now();                 // ver MINI_OS_TIME.md
	const bool alive = t.desc.fn(best, t.desc.user, max_slice_us);
	t.run_ticks += ticks_now() - t0;
	m_current = 0u;

	if (m_preempt != 0u) {                           // interrumpida por el SO
		if (alive && t.state == TaskState::Running) { t.state = TaskState::Ready; }
		return true;
	}
	t.state = alive ? TaskState::Ready : TaskState::Finished;
	return true;
}
```

El slice es **orientativo**: la tarea decide cuándo cede. Un worker sin puntos de `yield` se come el
frame; por eso el contrato es «bucles largos con `yield_if_preempt()`».

## 5. Cómo escribe el autor de una tarea

```cpp
struct UnpackJob { const eng::u8* src; eng::u8* dst; eng::u32 total, pos; };

bool unpack_task(TaskId, void* user, eng::u32) {
	auto* job = static_cast<UnpackJob*>(user);
	while (job->pos < job->total) {
		job->dst[job->pos] = decode_byte(job->src, job->pos);
		++job->pos;
		if (TaskSystem::yield_if_preempt()) { return true; } // sigue Ready, reanuda aquí
	}
	Msg m {};
	m.type = MsgType::User;
	m.payload.user = { /*done*/ 1u, job->pos, 0u };
	post_msg(m);
	return false;                                        // Finished
}
```

Tarea con **cola propia** (no bloquea la del SO):

```cpp
bool worker_task(TaskId self, void*, eng::u32) {
	if (TaskMsgPort* port = g_tasks.port(self)) {
		Msg m;
		if (port->q.pop(m)) { handle_worker_msg(m); }
	}
	// … trabajo de fondo …
	if (TaskSystem::yield_if_preempt()) { return true; }
	return true; // periódica hasta abort
}
```

## 6. Detenidas por interrupciones

| Mecanismo | Efecto |
|---|---|
| ISR hace `signal` + `request_preempt()` | el slice en curso sale en el próximo `yield_if_preempt` |
| `wait_or_idle` ve `signals` | no lanza más idle; drena la cola principal |
| VBlank latched | prioridad sobre cualquier tarea de fondo |

No es preempción dura (no se salva el PC a mitad de un `mul`). En un A500 es el modelo correcto: el
frame y el input **siempre ganan**. Si más adelante se quisiera preempción real, un CIA one-shot al
final de cada slice podría llamar a `request_preempt` (`MINI_OS_TIME.md` §6).

## 7. Ciclo de vida y mensajes

```text
create → Created ──start──► Ready ──run_idle──► Running
                                ▲                  │
                                └── yield/preempt ─┘
                                                   │ fn() == false
                                                   ▼
                                               Finished
suspend → Suspended   ·   resume → Ready   ·   abort → Aborted
```

`join` desde el main se implementa con `wait_or_idle` (nunca busy puro). Al terminar o abortar se
pueden postear `MsgType::TaskFinished`/`TaskAborted` (con el `TaskId` en el payload) para que la app
**no** tenga que sondear el estado.

## 8. Corrutinas y C++23

El scheduler coopera con **cualquier** modelo de tarea; las **corrutinas de C++20** (`co_await`/
`co_yield`) encajan como **front-end** sobre el mismo `TaskSystem`: una corrutina es una tarea cuyo
`poll()` reanuda el `coroutine_handle`.

- **Aplicabilidad en 68000**: sí, con matices. `gnu++23` habilita corrutinas en GCC; el *frame* de
  la corrutina se asigna por defecto en heap, pero se puede colocar en un **buffer fijo** con un
  `operator new(size_t, void*)` en el `promise_type` (sin heap, coherente con el engine). El frame
  es pequeño; reanudar son unos pocos `jsr`.
- **Suspensión = ceder al idle**: un `co_await idle_yield{}` marca la tarea `Ready` y devuelve el
  control al scheduler; `co_await wait_for_signal{SigFile}` la marca `Blocked` hasta que llegue la
  señal. El scheduler no cambia: solo distingue `Ready`/`Blocked`.
- **Coste/riesgo**: la calidad del codegen de corrutinas en m68k hay que **medirla** con
  `tools/analyze/codegen-report.mjs` (como cualquier pieza de camino caliente). Si no compensa, las
  tareas `poll()` de §5 son igual de válidas y más predecibles.
- **Recomendación**: empezar por tareas `poll()` (simples y medibles) y añadir las corrutinas como
  **capa opcional** cuando haya consumidor; ambas comparten `TaskSystem`, `TaskMsgPort` y el
  `preempt`.

```cpp
// Esbozo de tarea-corrutina (frame en buffer fijo del llamador).
Task<void> unpack(const eng::u8* src, eng::u8* dst, eng::u32 n) {
	for (eng::u32 i = 0; i < n; ++i) {
		dst[i] = decode_byte(src, i);
		co_await idle_yield {};   // cede al scheduler de idle
	}
}
```

## 9. Relación con `BackgroundQueue`

`eng::task::BackgroundQueue` ([`BACKGROUND_TASKS.md`](BACKGROUND_TASKS.md)) es el motor de **pasos
anónimos** drenado en el hueco de VBlank (decodificar, precargar). `TaskSystem` es la capa de
**tareas con identidad**: ciclo de vida, prioridad, cola propia y suspensión por señales. Se
componen: una tarea puede usar `BackgroundQueue` para su trabajo por rebanadas, o ser ella misma el
consumidor. Regla: **un hecho, un sitio** — el paso cooperativo por rebanadas en `BackgroundQueue`;
la tarea con estado y puerto, en `TaskSystem`.

## 10. Referencias

- [`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md) — puerto, señales, prioridad, VBlank latched.
- [`MINI_OS_TIME.md`](MINI_OS_TIME.md) — `ticks_now()`, slices y one-shot de CIA.
- [`BACKGROUND_TASKS.md`](BACKGROUND_TASKS.md) — cola cooperativa por rebanadas.
- [`PARALLEL_AND_THREADS.md`](PARALLEL_AND_THREADS.md) — concurrencia abstracta (no aplica en m68k).
- Plan de fases: [`ROADMAP_MINI_OS.md`](../../guides/roadmap/ROADMAP_MINI_OS.md).
