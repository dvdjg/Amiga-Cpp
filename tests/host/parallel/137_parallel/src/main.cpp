// ============================================================================
// Test HOST-137: primitivas de concurrencia (eng::parallel)
// ============================================================================
//
// Valida `engine/include/eng/parallel/parallel.hpp`: `hardware_threads`, `Mutex` +
// `LockGuard`, `Atomic`, `Thread`, `ConditionVariable`, `StopSource`/`StopToken` y
// `for_each_index`. En el host se ejecutan las rutas con hilos reales; en el cruce
// m68k las mismas APIs compilan como no-ops (esa ruta la cubre el cross-compile, no
// este binario).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/parallel/137_parallel

#include <cstdio>

#include <eng/parallel/parallel.hpp>

namespace {

using eng::u32;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_hardware_threads() {
	check(eng::parallel::hardware_threads() >= 1u, "parallel: al menos 1 cpu");
	check(eng::parallel::has_threads() == (eng::parallel::hardware_threads() > 0u),
	      "parallel: has_threads coherente");
}

void test_mutex_and_atomic() {
	eng::parallel::Mutex mutex;
	{
		eng::parallel::LockGuard guard(mutex);
		// Solo comprueba que el cierre RAII compila y no bloquea en secuencial.
	}
	eng::parallel::Atomic<u32> counter {0u};
	check(counter.load() == 0u, "atomic: arranca a 0");
	check(counter.fetch_add(5u) == 0u && counter.load() == 5u, "atomic: fetch_add");
	check(counter.exchange(9u) == 5u && counter.load() == 9u, "atomic: exchange");
	u32 expected = 9u;
	check(counter.compare_exchange(expected, 11u), "atomic: cas ok");
	check(counter.load() == 11u, "atomic: cas aplicado");
	expected = 100u;
	check(!counter.compare_exchange(expected, 12u) && expected == 11u,
	      "atomic: cas falla y actualiza expected");
}

void test_threads_increment() {
	if (!eng::parallel::has_threads()) {
		return;
	}
	eng::parallel::Atomic<u32> total {0u};
	constexpr u32 kThreads = 4u;
	constexpr u32 kPerThread = 25000u;

	struct Job {
		eng::parallel::Atomic<u32>* total;
		u32 per_thread;
	};
	Job job {&total, kPerThread};
	eng::parallel::ThreadFn entry = +[](void* raw) {
		Job* j = static_cast<Job*>(raw);
		for (u32 i = 0; i < j->per_thread; ++i) {
			j->total->fetch_add(1u);
		}
	};

	eng::parallel::Thread workers[kThreads];
	u32 spawned = 0u;
	for (u32 w = 0; w < kThreads; ++w) {
		if (workers[w].start(entry, &job)) {
			++spawned;
		}
	}
	check(spawned == kThreads, "thread: arrancan todos");
	for (u32 w = 0; w < kThreads; ++w) {
		workers[w].join();
	}
	check(total.load() == kThreads * kPerThread, "thread: contador atomico exacto");
}

void test_condition_variable() {
	if (!eng::parallel::has_threads()) {
		return;
	}
	eng::parallel::Mutex mutex;
	eng::parallel::ConditionVariable cv;
	eng::parallel::Atomic<bool> ready {false};
	eng::parallel::Atomic<u32> value {0u};

	struct Job {
		eng::parallel::Mutex* mutex;
		eng::parallel::ConditionVariable* cv;
		eng::parallel::Atomic<bool>* ready;
		eng::parallel::Atomic<u32>* value;
	};
	Job job {&mutex, &cv, &ready, &value};
	eng::parallel::ThreadFn entry = +[](void* raw) {
		Job* j = static_cast<Job*>(raw);
		eng::parallel::LockGuard guard(*j->mutex);
		j->cv->wait(*j->mutex, [&]() { return j->ready->load(); });
		j->value->store(j->value->load() + 1u);
	};

	eng::parallel::Thread worker;
	check(worker.start(entry, &job), "cv: arranca el worker");
	{
		eng::parallel::LockGuard guard(mutex);
		ready.store(true);
	}
	cv.notify_one();
	worker.join();
	check(value.load() == 1u, "cv: el worker despierta y trabaja");
}

void test_stop_source() {
	eng::parallel::StopSource source;
	eng::parallel::StopToken token = source.token();
	check(!token.stop_requested(), "stop: sin cancelar");
	source.request_stop();
	check(token.stop_requested(), "stop: cancelado visible al token");
}

void test_for_each_index() {
	constexpr u32 kN = 1000u;
	u32 squares[kN] = {};
	eng::parallel::for_each_index(kN, 4u, [&](u32 i) { squares[i] = i * i; });
	bool ok = true;
	for (u32 i = 0; i < kN; ++i) {
		if (squares[i] != i * i) {
			ok = false;
			break;
		}
	}
	check(ok, "for_each_index: cubre todas las iteraciones");

	eng::parallel::Atomic<u32> visits {0u};
	eng::parallel::for_each_index(kN, 4u, [&](u32) { visits.fetch_add(1u); });
	check(visits.load() == kN, "for_each_index: una visita por indice");
}

} // namespace

int main() {
	std::printf("eng::parallel:\n");
	test_hardware_threads();
	test_mutex_and_atomic();
	test_threads_increment();
	test_condition_variable();
	test_stop_source();
	test_for_each_index();

	if (g_fail == 0u) {
		std::printf("OK: eng::parallel (hilos, mutex, atomicos, cv, stop, for_each_index)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
