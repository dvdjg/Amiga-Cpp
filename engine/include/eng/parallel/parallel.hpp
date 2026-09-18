#pragma once

/// \file parallel.hpp
/// Primitivas de concurrencia del engine (`eng::parallel`): hilos, mutex, atómicos,
/// variables de condición y cancelación cooperativa, **abstraídas** para compilar
/// igual en un Amiga de una sola CPU que en una máquina moderna multinúcleo.
///
/// Motivación
/// ----------
/// El desarrollo del engine apunta a dos extremos:
///
/// - **Clásico (68000/020/030):** una sola CPU, sin hilos ni memoria compartida.
///   "Paralelismo" es necesariamente cooperativo (ver `eng::task::BackgroundQueue`).
/// - **Moderno (PC de herramientas o ports):** varias CPUs; la búsqueda adversaria
///   (u otras rutinas pesadas) puede repartirse entre hilos reales.
///
/// Esta cabecera es la **frontera** entre ambos: la lógica escribe contra
/// `eng::parallel` y no contra `<thread>` ni registros. En el cruce `m68k` las
/// primitivas de sincronización son **no-ops** (un único contexto de ejecución) y
/// `hardware_threads()` devuelve `1`, de modo que los algoritmos caen a su ruta
/// secuencial. En el host moderno delegan en la STL (`std::thread`, `std::mutex`,
/// `std::atomic`, `std::condition_variable`).
///
/// Regla de uso: **ningún camino por frame del Amiga debe crear hilos**. Las rutinas
/// paralelas se reservan para `init`, análisis offline o plataformas modernas; en el
/// A500 el mismo código corre secuencial y sin coste.
///
/// Ejemplo:
///   if (eng::parallel::hardware_threads() > 1u) {
///       eng::parallel::for_each_index(n, grains, [&](eng::u32 i) { work(i); });
///   } else {
///       for (eng::u32 i = 0; i < n; ++i) { work(i); }
///   }
///
/// Verificación: HOST-137.

#include <eng/core/types.hpp>

#if !defined(__m68k__)
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace eng::parallel {

/// Número de CPUs utilizables. En el Amiga (una CPU) es `1`; los algoritmos deben
/// usar esto para decidir si reparten trabajo.
[[nodiscard]] inline unsigned hardware_threads() noexcept {
#if defined(__m68k__)
	return 1u;
#else
	const unsigned n = std::thread::hardware_concurrency();
	return n == 0u ? 1u : n;
#endif
}

/// ¿El target puede crear hilos reales? Constante de compilación.
[[nodiscard]] constexpr bool has_threads() noexcept {
#if defined(__m68k__)
	return false;
#else
	return true;
#endif
}

/// Mutex de exclusión mutua. En el Amiga es un no-op (un solo contexto); en el host
/// envuelve `std::mutex`.
class Mutex {
public:
	Mutex() noexcept = default;
	Mutex(const Mutex&) = delete;
	Mutex& operator=(const Mutex&) = delete;

	void lock() noexcept {
#if !defined(__m68k__)
		m_impl.lock();
#endif
	}

	void unlock() noexcept {
#if !defined(__m68k__)
		m_impl.unlock();
#endif
	}

	[[nodiscard]] bool try_lock() noexcept {
#if defined(__m68k__)
		return true;
#else
		return m_impl.try_lock();
#endif
	}

private:
#if !defined(__m68k__)
	friend class ConditionVariable;
	std::mutex m_impl;
#endif
};

/// Cierre RAII de un `Mutex` (equivalente a `std::lock_guard`).
class LockGuard {
public:
	explicit LockGuard(Mutex& mutex) noexcept : m_mutex(&mutex) { m_mutex->lock(); }
	~LockGuard() noexcept {
		if (m_mutex != nullptr) {
			m_mutex->unlock();
		}
	}
	LockGuard(const LockGuard&) = delete;
	LockGuard& operator=(const LockGuard&) = delete;

private:
	Mutex* m_mutex;
};

/// Atómico abstracto. En el Amiga, con un único contexto de ejecución, basta un
/// `volatile T`; en el host, `std::atomic<T>`. La API es la mínima que usan los
/// algoritmos del engine (contadores de trabajo y flags de cancelación).
template <class T>
class Atomic {
public:
	Atomic() noexcept = default;
	explicit Atomic(T value) noexcept
#if defined(__m68k__)
		: m_value(value)
#else
		: m_value(value)
#endif
	{}
	Atomic(const Atomic&) = delete;
	Atomic& operator=(const Atomic&) = delete;

	[[nodiscard]] T load() const noexcept {
#if defined(__m68k__)
		return m_value;
#else
		return m_value.load();
#endif
	}

	void store(T value) noexcept {
#if defined(__m68k__)
		m_value = value;
#else
		m_value.store(value);
#endif
	}

	/// Suma y devuelve el valor **anterior** (patrón de reparto de índices).
	T fetch_add(T value) noexcept {
#if defined(__m68k__)
		const T old = m_value;
		m_value = static_cast<T>(old + value);
		return old;
#else
		return m_value.fetch_add(value);
#endif
	}

	T exchange(T value) noexcept {
#if defined(__m68k__)
		const T old = m_value;
		m_value = value;
		return old;
#else
		return m_value.exchange(value);
#endif
	}

	/// Compara e intercambia fuerte: actualiza `expected` si falla.
	[[nodiscard]] bool compare_exchange(T& expected, T desired) noexcept {
#if defined(__m68k__)
		if (m_value == expected) {
			m_value = desired;
			return true;
		}
		expected = m_value;
		return false;
#else
		return m_value.compare_exchange_strong(expected, desired);
#endif
	}

private:
#if defined(__m68k__)
	volatile T m_value {};
#else
	std::atomic<T> m_value {};
#endif
};

/// Variable de condición. En el Amiga `wait` es un no-op (no hay otro contexto que
/// notifique); se mantiene para que el código compile sin ramas.
class ConditionVariable {
public:
	ConditionVariable() noexcept = default;
	ConditionVariable(const ConditionVariable&) = delete;
	ConditionVariable& operator=(const ConditionVariable&) = delete;

	void notify_one() noexcept {
#if !defined(__m68k__)
		m_impl.notify_one();
#endif
	}

	void notify_all() noexcept {
#if !defined(__m68k__)
		m_impl.notify_all();
#endif
	}

	void wait(Mutex& mutex) noexcept {
#if !defined(__m68k__)
		std::unique_lock<std::mutex> lock(mutex.m_impl);
		m_impl.wait(lock);
#else
		(void)mutex;
#endif
	}

	template <class Predicate>
	void wait(Mutex& mutex, Predicate pred) noexcept {
		while (!pred()) {
			wait(mutex);
		}
	}

private:
#if !defined(__m68k__)
	std::condition_variable m_impl;
#endif
};

class StopSource;

/// Vista de cancelación de una `StopSource` (se puede copiar a hilos/tareas). Si la
/// fuente muere antes que el token, el token queda colgando: el dueño conserva la
/// `StopSource` viva mientras haya tokens.
class StopToken {
public:
	StopToken() noexcept = default;

	[[nodiscard]] bool stop_requested() const noexcept {
		return m_flag != nullptr && m_flag->load();
	}

private:
	friend class StopSource;
	explicit StopToken(const Atomic<bool>* flag) noexcept : m_flag(flag) {}
	const Atomic<bool>* m_flag = nullptr;
};

/// Fuente de cancelación cooperativa: los hilos/tareas consultan su `StopToken` en
/// cada rebanada y terminan limpiamente.
class StopSource {
public:
	StopSource() noexcept = default;
	StopSource(const StopSource&) = delete;
	StopSource& operator=(const StopSource&) = delete;

	[[nodiscard]] StopToken token() const noexcept { return StopToken(&m_flag); }
	void request_stop() noexcept { m_flag.store(true); }
	[[nodiscard]] bool stop_requested() const noexcept { return m_flag.load(); }

private:
	Atomic<bool> m_flag {false};
};

/// Punto de entrada de un hilo. El `arg` es del llamador y debe sobrevivir al hilo.
using ThreadFn = void (*)(void*);

/// Hilo real. En el Amiga `start` devuelve `false` (no hay hilos); el llamador debe
/// comprobar el retorno o condicionar con `hardware_threads()`.
class Thread {
public:
	Thread() noexcept = default;
	~Thread() noexcept {
		if (joinable()) {
			join();
		}
	}
	Thread(const Thread&) = delete;
	Thread& operator=(const Thread&) = delete;

	/// Arranca el hilo. Devuelve `false` si ya estaba activo o si el target no
	/// soporta hilos (Amiga).
	[[nodiscard]] bool start(ThreadFn fn, void* arg) noexcept {
#if defined(__m68k__)
		(void)fn;
		(void)arg;
		return false;
#else
		if (m_thread.joinable()) {
			return false;
		}
		m_thread = std::thread(fn, arg);
		return true;
#endif
	}

	void join() noexcept {
#if !defined(__m68k__)
		if (m_thread.joinable()) {
			m_thread.join();
		}
#endif
	}

	[[nodiscard]] bool joinable() const noexcept {
#if defined(__m68k__)
		return false;
#else
		return m_thread.joinable();
#endif
	}

private:
#if !defined(__m68k__)
	std::thread m_thread;
#endif
};

/// Maximo de workers auxiliares de `for_each_index` (sin memoria dinamica).
inline constexpr u32 kMaxWorkers = 15u;

/// Reparte `count` iteraciones `[0, count)` entre hasta `grains` hilos. En el Amiga
/// (o con `grains <= 1`) ejecuta secuencial. `fn` se copia y debe ser `(u32) -> void`.
template <class Fn>
void for_each_index(u32 count, u32 grains, Fn fn) {
	if (count == 0u) {
		return;
	}
	if (grains == 0u) {
		grains = 1u;
	}
#if defined(__m68k__)
	(void)grains;
	for (u32 i = 0; i < count; ++i) {
		fn(i);
	}
#else
	if (grains == 1u || !has_threads()) {
		for (u32 i = 0; i < count; ++i) {
			fn(i);
		}
		return;
	}
	if (grains > kMaxWorkers + 1u) {
		grains = kMaxWorkers + 1u;
	}
	Atomic<u32> next {0u};
	struct Job {
		Atomic<u32>* next;
		u32 count;
		Fn* fn;
	};
	Job job {&next, count, &fn};
	ThreadFn entry = +[](void* raw) {
		Job* j = static_cast<Job*>(raw);
		for (;;) {
			const u32 i = j->next->fetch_add(1u);
			if (i >= j->count) {
				break;
			}
			(*j->fn)(i);
		}
	};

	Thread workers[kMaxWorkers];
	u32 spawned = 0u;
	for (u32 w = 1u; w < grains; ++w) {
		if (!workers[spawned].start(entry, &job)) {
			break;
		}
		++spawned;
	}
	entry(&job); // el hilo llamador tambien trabaja
	for (u32 w = 0; w < spawned; ++w) {
		workers[w].join();
	}
#endif
}

} // namespace eng::parallel
