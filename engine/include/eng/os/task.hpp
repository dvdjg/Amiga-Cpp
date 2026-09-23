#pragma once

/// \file task.hpp
/// **Tareas asíncronas de fondo del mini-SO** (`eng::os::TaskSystem`): tareas **cooperativas** con
/// identidad, estado y función `poll()` que avanzan un poco y vuelven. **No compiten** con el bucle
/// de mensajes: solo corren en **idle** (cuando la cola principal está vacía) y se **abortan en
/// cuanto la ISR marca `preempt` (VBlank/input/disco). Así el frame y la entrada siempre ganan.
/// Ver `docs/engine/architecture/MINI_OS_TASKS.md` §1–2 y `ROADMAP_MINI_OS.md` (M10).
///
/// Sin heap, sin excepciones y sin preempción dura: el `TaskSystem` elige una tarea `Ready` por
/// prioridad (round-robin entre iguales), la marca `Running`, llama a su `poll()` con un
/// **presupuesto orientativo** (`slice_us`) y, si al volver sigue `Running`, la deja `Ready` (o
/// `Finished` si `poll()` devolvió `false`). Las corrutinas (`co_await idle_yield`) son opcionales
/// (M11).

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/os/port.hpp>

namespace eng::os {

/// Estado de una tarea.
enum class TaskState : eng::u8 {
	Invalid = 0,
	Created,   ///< creada, aún no arrancada
	Ready,     ///< lista para un slice de idle
	Running,   ///< ejecutando su `poll()` (dentro de `run_idle`)
	Blocked,   ///< esperando una señal/cola (la tarea la deja así y otro la despierta)
	Suspended, ///< pausada por la aplicación
	Finished,  ///< terminó (poll() devolvió false)
	Aborted,   ///< cancelada por la aplicación
};

/// Identificador de tarea (`0` = inválido).
using TaskId = eng::u16;

/// Función de tarea: `true` = sigue viva; `false` = terminó. `slice_us` es **orientativo** (el
/// presupuesto de tiempo del slice); la tarea debe ceder en bucles largos.
using TaskFn = bool (*)(TaskId self, void* user, eng::u32 slice_us);

/// **Cola propia de una tarea** (reducida): la tarea drena **su** puerto (nunca el del SO), y la
/// aplicación la despierta posteando aquí. Útil para pipeline productor/consumidor en el fondo.
struct TaskMsgPort {
	PrioMsgQueue<16> q {};

	bool post(const Msg& m, MsgPrio p = MsgPrio::Normal) noexcept { return q.push(m, p); }
	bool wait(Msg& out) noexcept { return q.pop(out); }
	[[nodiscard]] bool peek(Msg& out) const noexcept { return q.peek(out); }
	[[nodiscard]] bool empty() const noexcept { return q.empty(); }
};

/// Descripción de una tarea.
struct TaskDesc {
	TaskFn fn = nullptr;
	void* user = nullptr;
	const char* name = nullptr;
	eng::u8 priority = 128u; ///< mayor = se elige antes en idle
	bool own_port = false;   ///< crea un `TaskMsgPort` propio (ver `TaskSystem::port`)
	eng::u16 stack_words = 0u; ///< 0 = sin stack propio (solo `poll()` cooperativo; M11)
};

/// **Sistema de tareas de fondo** de capacidad fija (sin heap).
class TaskSystem {
public:
	static constexpr eng::u8 kMaxTasks = 8u;

	/// Prepara el sistema para `max_tasks` (<= `kMaxTasks`). `false` si `max_tasks` es 0 o excede.
	bool init(eng::u8 max_tasks = kMaxTasks) noexcept {
		if (max_tasks == 0u || max_tasks > kMaxTasks) {
			return false;
		}
		m_max = max_tasks;
		m_count = 0u;
		m_preempt = false;
		return true;
	}

	/// Cancela todas las tareas (quedan `Aborted`).
	void shutdown() noexcept {
		for (eng::u8 i = 0u; i < m_max; ++i) {
			if (m_tasks[i].state != TaskState::Invalid &&
			    m_tasks[i].state != TaskState::Finished) {
				m_tasks[i].state = TaskState::Aborted;
			}
		}
	}

	/// Crea una tarea (`Created`). Devuelve `0` si no cabe o `fn` es nulo.
	TaskId create(const TaskDesc& d) noexcept {
		if (d.fn == nullptr || m_count >= m_max) {
			return 0u;
		}
		const eng::u8 i = m_count++;
		Task& t = m_tasks[i];
		t = Task {};
		t.fn = d.fn;
		t.user = d.user;
		t.name = d.name;
		t.priority = d.priority;
		t.own_port = d.own_port;
		t.state = TaskState::Created;
		return static_cast<TaskId>(i + 1u); // id = índice + 1
	}

	/// Arranca una tarea `Created` → `Ready`. `false` si no lo estaba.
	bool start(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() || t->state != TaskState::Created) {
			return false;
		}
		t->state = TaskState::Ready;
		return true;
	}

	/// Pausa una tarea viva → `Suspended`. `false` si ya terminó/abortó.
	bool suspend(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() || t->state == TaskState::Finished || t->state == TaskState::Aborted ||
		    t->state == TaskState::Invalid) {
			return false;
		}
		t->state = TaskState::Suspended;
		return true;
	}

	/// Reactiva una tarea `Suspended` → `Ready`. `false` si no lo estaba.
	bool resume(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() || t->state != TaskState::Suspended) {
			return false;
		}
		t->state = TaskState::Ready;
		return true;
	}

	/// Cancela una tarea → `Aborted` (no vuelve a correr en idle).
	bool abort(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() || t->state == TaskState::Invalid) {
			return false;
		}
		t->state = TaskState::Aborted;
		return true;
	}

	/// Espera a que la tarea termine, ejecutando idle (solo desde el hilo principal). Devuelve
	/// `false` si la tarea es inválida o se quedó `Blocked`/`Suspended` (no puede progresar).
	bool join(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid()) {
			return false;
		}
		while (t->state != TaskState::Finished && t->state != TaskState::Aborted) {
			if (t->state == TaskState::Blocked || t->state == TaskState::Suspended) {
				return false; // sin progreso posible desde aquí
			}
			if (!run_idle()) {
				return false;
			}
			clear_preempt();
		}
		return t->state == TaskState::Finished;
	}

	/// Estado actual de la tarea (`Invalid` si el id no existe).
	[[nodiscard]] TaskState state(TaskId id) const noexcept {
		const auto t = get(id);
		return (t.valid()) ? t->state : TaskState::Invalid;
	}

	/// Nombre de la tarea (`nullptr` si el id no existe).
	[[nodiscard]] const char* name(TaskId id) const noexcept {
		const auto t = get(id);
		return (t.valid()) ? t->name : nullptr;
	}

	/// Prioridad de la tarea (0 si el id es inválido).
	[[nodiscard]] eng::u8 priority(TaskId id) const noexcept {
		const auto t = get(id);
		return (t.valid()) ? t->priority : 0u;
	}

	/// Cola propia de la tarea (`Ref` no válida si no se pidió `own_port` o el id es inválido).
	[[nodiscard]] eng::Ref<TaskMsgPort> port(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() || !t->own_port) {
			return {};
		}
		return eng::Ref<TaskMsgPort>(m_ports[id - 1u]);
	}

	/// Marca una tarea `Blocked` (la tarea la llama desde su `poll()` cuando no puede avanzar, o la
	/// aplicación para una tarea `Ready`).
	bool block(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() ||
		    (t->state != TaskState::Running && t->state != TaskState::Ready)) {
			return false;
		}
		t->state = TaskState::Blocked;
		return true;
	}

	/// Despierta una tarea `Blocked` (la aplicación, al llegar su señal/mensaje).
	bool unblock(TaskId id) noexcept {
		auto t = get(id);
		if (!t.valid() || t->state != TaskState::Blocked) {
			return false;
		}
		t->state = TaskState::Ready;
		return true;
	}

	/// **Idle**: da **un slice** a la tarea `Ready` de mayor prioridad (empate: round-robin),
	/// siempre que la ISR no haya pedido `preempt`. Devuelve `true` si ejecutó un slice; el bucle
	/// principal lo llama repetidamente mientras la cola principal esté vacía.
	bool run_idle(eng::u32 max_slice_us = 500u) noexcept {
		if (preempt_requested()) {
			return false;
		}
		auto t = pick_ready();
		if (!t.valid()) {
			return false;
		}
		const TaskId id = static_cast<TaskId>(static_cast<eng::u8>(t.get() - m_tasks) + 1u);
		t->state = TaskState::Running;
		const bool alive = t->fn(id, t->user, max_slice_us);
		// Si la tarea no cambió su estado desde dentro (Blocked/Suspended/Aborted), lo
		// resolvemos por el valor devuelto.
		if (t->state == TaskState::Running) {
			t->state = alive ? TaskState::Ready : TaskState::Finished;
		}
		return true;
	}

	/// La ISR / `wait()` avisa: hay trabajo del SO → no seguir en idle.
	void request_preempt() noexcept { m_preempt = true; }
	[[nodiscard]] bool preempt_requested() const noexcept { return m_preempt; }
	void clear_preempt() noexcept { m_preempt = false; }

	/// La propia tarea la llama en bucles largos para ceder si el SO pidió `preempt`.
	[[nodiscard]] static bool yield_if_preempt() noexcept { return s_preempt_hook != nullptr && s_preempt_hook(); }

	[[nodiscard]] eng::u8 task_count() const noexcept { return m_max; }

private:
	struct Task {
		TaskFn fn = nullptr;
		void* user = nullptr;
		const char* name = nullptr;
		eng::u8 priority = 0u;
		bool own_port = false;
		TaskState state = TaskState::Invalid;
	};

	/// Tarea por `id` (`nullptr` si no existe o está libre).
	[[nodiscard]] eng::Ref<Task> get(TaskId id) noexcept {
		if (id == 0u || static_cast<eng::u8>(id - 1u) >= m_max) {
			return {};
		}
		Task& t = m_tasks[id - 1u];
		return (t.state != TaskState::Invalid) ? eng::Ref<Task>(t) : eng::Ref<Task>();
	}
	/// Tarea por `id` (versión const).
	[[nodiscard]] eng::Ref<const Task> get(TaskId id) const noexcept {
		auto t = const_cast<TaskSystem*>(this)->get(id);
		return t.valid() ? eng::Ref<const Task>(*t) : eng::Ref<const Task>();
	}

	/// Tarea `Ready` de mayor prioridad (empate: la de menor índice). Recorre todas.
	[[nodiscard]] eng::Ref<Task> pick_ready() noexcept {
		eng::Ref<Task> best {};
		eng::u8 best_pri = 0u;
		for (eng::u8 i = 0u; i < m_max; ++i) {
			Task& t = m_tasks[i];
			if (t.state != TaskState::Ready) {
				continue;
			}
			if (!best.valid() || t.priority > best_pri) {
				best = eng::Ref<Task>(t);
				best_pri = t.priority;
			}
		}
		return best;
	}

	Task m_tasks[kMaxTasks] {};
	TaskMsgPort m_ports[kMaxTasks] {}; ///< cola propia de cada slot (solo si `own_port`)
	eng::u8 m_max = 0u;
	eng::u8 m_count = 0u; ///< tareas creadas (los ids son estables)
	bool m_preempt = false;

	/// Hook estático para `yield_if_preempt()` (una instancia activa a la vez).
	static inline bool (*s_preempt_hook)() = nullptr;

public:
	/// Activa el hook de `yield_if_preempt` ligándolo a este sistema (una instancia activa).
	void bind_preempt_hook() noexcept {
		s_preempt_hook = &TaskSystem::hook_query;
		s_hook_system = eng::Ref<TaskSystem>(*this);
	}
	static void unbind_preempt_hook() noexcept {
		s_preempt_hook = nullptr;
		s_hook_system.reset();
	}

private:
	static inline eng::Ref<TaskSystem> s_hook_system {}; ///< sistema ligado (no propietario)
	/// Consulta el flag de `preempt` del sistema ligado (destino de `yield_if_preempt`).
	static bool hook_query() noexcept {
		return s_hook_system.valid() && s_hook_system->m_preempt;
	}
};

/// **Espera o aprovecha** (punto de integración del bucle): si hay señales de `mask` en `main`, las
/// consume y pide `preempt` al fondo; si no, da **un slice** de fondo con `tasks`. Devuelve las
/// señales consumidas (0 si solo ejecutó idle o no había nada). El bucle principal lo llama cada
/// frame, antes de drenar los mensajes.
template <eng::u16 N>
[[nodiscard]] eng::u32 wait_or_idle(MsgPort<N>& main, TaskSystem& tasks, eng::u32 mask,
				    eng::u32 idle_slice_us = 200u) noexcept {
	const eng::u32 got = main.pending(mask);
	if (got != 0u) {
		tasks.request_preempt();
		return main.take_signals(got);
	}
	(void)tasks.run_idle(idle_slice_us);
	return 0u;
}

} // namespace eng::os
