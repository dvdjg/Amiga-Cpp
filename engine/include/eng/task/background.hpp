#pragma once

/// \file background.hpp
/// Tareas de fondo cooperativas: rutinas pesadas que avanzan **solo** cuando el
/// bucle principal no tiene nada que hacer (hueco de VBlank, espera de Blitter…).
///
/// En un Amiga hay una sola CPU (sin hilos): "fondo" es necesariamente
/// **cooperativo**. Este bloque registra *rutinas* (no procesos) que procesan un
/// trozo por rebanada y devuelven las unidades consumidas. El engine las drena
/// desde sus puntos ociosos con un presupuesto, de modo que `update`/`render`
/// siempre van primero: la tarea de fondo nunca retrasa el frame, solo rellena el
/// tiempo muerto (o el juego la trocea mas).
///
/// Contrato bidireccional (todo barato, sin heap):
///
/// ```
///                       +-------------------- BackgroundQueue -------------------+
///   juego  --add-->     |  TaskStep(data, slice) -> unidades                     |
///   juego  --progress-> |  {state, done, total, permille, avg_units_per_slice}  |
///   tarea  <--slice--   |  {frame, vpos, budget, done, total, avg}               |
///                       +------------------------------------------------------+
/// ```
///
/// - La **tarea** recibe en `TaskSlice` informacion del mundo (frame, linea de
///   raster `vpos`), su propio progreso y su rendimiento medido, para **adaptar su
///   carga** (p. ej. procesar mas o menos segun cuantas lineas quedan de frame).
/// - El **juego** consulta `progress()` para saber cuanto le falta a un proceso
///   finito (o `total_units == 0` si es continuo).
///
/// No hay asignacion dinamica: pool fijo de `max_tasks`. Las tareas terminadas
/// liberan su slot y el `generation` del handle lo invalida.

#include <eng/core/types.hpp>

namespace eng::task {

/// Devuelto por `TaskStep` para marcar la tarea como fallida.
constexpr u16 task_abort = 0xffffu;

/// permille (0..1000) de `done/total` sin desbordar `u32` (freestanding, sin u64).
constexpr u16 permille_of(u32 done, u32 total) {
	if (total == 0u) return 0u;
	if (done >= total) return 1000u;
	// done < total. Cuando done*1000 no desborda, es exacto.
	if (done <= 4294967u) {
		return static_cast<u16>((done * 1000u) / total);
	}
	// done enorme: divide primero por ~total/1000 (indicador aproximado, sigue barato).
	const u32 step = (total / 1000u) + 1u;
	u32 p = done / step;
	if (p > 1000u) p = 1000u;
	return static_cast<u16>(p);
}

/// Estado de una tarea.
enum class TaskState : u8 {
	Free = 0,
	Running = 1,
	Done = 2,
	Failed = 3,
};

/// Handle sin punteros que sobrevive a reutilizacion de slots.
struct TaskHandle {
	u8 index = 0xffu;
	u8 generation = 0u;

	constexpr bool valid() const { return index != 0xffu; }
	constexpr bool operator==(const TaskHandle& other) const {
		return index == other.index && generation == other.generation;
	}
};

/// Contexto que el engine entrega a la tarea en cada rebanada.
///
/// La tarea decide **cuanto** procesa (hasta `budget_units`) segun `vpos`/`frame`
/// (para no pasarse del hueco) y sus indicadores (`done_units`/`avg_units_per_slice`).
struct TaskSlice {
	u32 frame = 0;               // frame del engine
	u16 vpos = 0;                // linea de raster actual (barrido del CRT)
	u16 budget_units = 0;        // maximo de unidades que puede consumir ahora
	u32 done_units = 0;          // progreso de la propia tarea
	u32 total_units = 0;         // 0 = tarea continua (sin fin)
	u16 avg_units_per_slice = 0; // rendimiento medido (media movil)
};

/// Rutina de una tarea: procesa un trozo y devuelve las unidades consumidas
/// (<= `slice.budget_units`), o `task_abort` para fallar. `data` es del llamador.
using TaskStep = u16 (*)(void* data, const TaskSlice& slice);

/// Foto barata del estado/progreso de una tarea (para el juego).
struct TaskProgress {
	TaskState state = TaskState::Free;
	u32 done_units = 0;
	u32 total_units = 0;    // 0 = continua
	u16 permille = 0;       // 0..1000 (done/total); 1000 si Done
	u16 avg_units_per_slice = 0;
	u16 slices = 0;

	/// Unidades que faltan (0 si es continua o ya termino).
	constexpr u32 remaining_units() const {
		return (total_units > done_units) ? (total_units - done_units) : 0u;
	}
	/// Terminada (Done o Failed).
	constexpr bool finished() const {
		return state == TaskState::Done || state == TaskState::Failed;
	}
};

/// Cola cooperativa de tareas de fondo (pool fijo, sin heap).
class BackgroundQueue {
public:
	static constexpr u8 max_tasks = 8;

	/// Registra una tarea. `total_units = 0` la hace continua (nunca termina sola;
	/// el juego la cancela). `slice_units` es el presupuesto por rebanada (la tarea
	/// puede consumir menos). Devuelve un handle invalido si no hay slot libre.
	TaskHandle add(TaskStep step, void* data, u32 total_units = 0u, u16 slice_units = 64u) {
		if (step == nullptr) {
			return {};
		}
		for (u8 i = 0; i < max_tasks; ++i) {
			Entry& e = m_entries[i];
			if (!e.live) {
				e.step = step;
				e.data = data;
				e.done = 0u;
				e.total = total_units;
				e.slice_units = (slice_units == 0u) ? 1u : slice_units;
				e.avg = 0u;
				e.slices = 0u;
				e.live = true;
				++m_live;
				return { i, e.generation };
			}
		}
		return {};
	}

	/// Cancela una tarea (libera el slot). Devuelve false si el handle no es valido.
	bool cancel(TaskHandle handle) {
		Entry* e = find(handle);
		if (e == nullptr) {
			return false;
		}
		release(*e);
		return true;
	}

	/// Progreso/rendimiento de una tarea (barato). Handle invalido -> `Free`.
	TaskProgress progress(TaskHandle handle) const {
		const Entry* e = find(handle);
		TaskProgress p {};
		if (e == nullptr) {
			return p;
		}
		p.state = e->failed ? TaskState::Failed
			  : e->completed ? TaskState::Done
			  : TaskState::Running;
		p.done_units = e->done;
		p.total_units = e->total;
		p.avg_units_per_slice = e->avg;
		p.slices = e->slices;
		if (e->total != 0u) {
			p.permille = (p.state == TaskState::Done) ? 1000u : permille_of(e->done, e->total);
		}
		return p;
	}

	/// Ejecuta una rebanada de cada tarea viva (presupuesto por tarea = `slice_units`).
	/// Devuelve las unidades totales consumidas en la rebanada. Coste cero si no hay
	/// tareas o si ya se agoto el cupo de rebanadas del frame (`max_slices_per_frame`).
	/// Lo llaman los puntos ociosos del engine (VBlank, espera de Blitter) con el
	/// `frame` actual, de modo que el cupo se aplica de forma global por frame.
	u16 run_slice(u32 frame, u16 vpos) {
		if (frame != m_frame) {
			m_frame = frame;
			m_frame_slices = 0u;
		}
		if (m_live == 0u || m_frame_slices >= m_max_slices) {
			m_slice_units = 0u;
			return 0u;
		}
		++m_frame_slices;
		u32 total_units = 0u;
		for (u8 i = 0; i < max_tasks; ++i) {
			Entry& e = m_entries[i];
			if (!e.live || e.failed || e.completed) {
				continue;
			}
			TaskSlice slice {};
			slice.frame = frame;
			slice.vpos = vpos;
			slice.budget_units = e.slice_units;
			slice.done_units = e.done;
			slice.total_units = e.total;
			slice.avg_units_per_slice = e.avg;

			const u16 used = e.step(e.data, slice);
			if (used == task_abort) {
				e.failed = true;
				continue;
			}
			const u16 consumed = (used > e.slice_units) ? e.slice_units : used;
			e.done += consumed;
			total_units += consumed;
			// Media movil barata (peso 1/8) del coste por rebanada.
			if (e.slices == 0u) {
				e.avg = consumed;
			} else {
				e.avg = static_cast<u16>((e.avg - (e.avg >> 3)) + (consumed >> 3));
			}
			if (e.slices != 0xffu) {
				++e.slices;
			}
			if (e.total != 0u && e.done >= e.total) {
				e.done = e.total;
				// Tarea finita completada: queda en `Done` hasta que el juego la
				// cancele (asi el juego puede consultar que termino y su progreso).
				e.completed = true;
			}
		}
		m_slice_units = static_cast<u16>(total_units > 0xffffu ? 0xffffu : total_units);
		return m_slice_units;
	}

	/// Unidades consumidas por la ultima rebanada (rendimiento global).
	constexpr u16 last_slice_units() const { return m_slice_units; }

	/// Numero de tareas vivas (Running o Failed).
	u8 live_count() const { return m_live; }

	/// Cap configurable de rebanadas que el engine drena por frame (contra el
	/// presupuesto global de tiempo).
	constexpr void set_max_slices_per_frame(u8 value) { m_max_slices = (value == 0u) ? 1u : value; }
	constexpr u8 max_slices_per_frame() const { return m_max_slices; }

private:
	struct Entry {
		TaskStep step = nullptr;
		void* data = nullptr;
		u32 done = 0u;
		u32 total = 0u;
		u16 slice_units = 0u;
		u16 avg = 0u;
		u8 slices = 0u;
		u8 generation = 0u;
		bool live = false;
		bool failed = false;
		bool completed = false;
	};

	Entry* find(TaskHandle handle) {
		if (!handle.valid()) {
			return nullptr;
		}
		Entry& e = m_entries[handle.index];
		return (e.live && e.generation == handle.generation) ? &e : nullptr;
	}
	const Entry* find(TaskHandle handle) const {
		if (!handle.valid()) {
			return nullptr;
		}
		const Entry& e = m_entries[handle.index];
		return (e.live && e.generation == handle.generation) ? &e : nullptr;
	}

	void release(Entry& e) {
		e.live = false;
		e.failed = false;
		e.completed = false;
		e.step = nullptr;
		e.data = nullptr;
		e.slice_units = 0u;
		e.generation = static_cast<u8>(e.generation + 1u); // invalida handles viejos
		if (m_live != 0u) {
			--m_live;
		}
	}

	Entry m_entries[max_tasks] {};
	u16 m_slice_units = 0u;
	u8 m_live = 0u;
	u8 m_max_slices = 4u;
	u32 m_frame = 0xffffffffu;   // frame actual (para resetear el cupo por frame)
	u8 m_frame_slices = 0u;      // rebanadas ya consumidas en este frame
};

} // namespace eng::task
