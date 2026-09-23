// ============================================================================
// Test HOST-089: PriorityQueue (heap binario de capacidad fija).
// ============================================================================
//
// Respalda `eng/core/util/priority_queue.hpp`: max-heap/min-heap, llenado, rechazo al
// estar llena, comparador propio y `emplace`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/089_priority_queue

#include <cstdio>

#include <eng/core/util/priority_queue.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Task {
	int prio = 0;
	int id = 0;
};

struct ByPrio {
	[[nodiscard]] bool operator()(const Task& a, const Task& b) const {
		return a.prio < b.prio;
	}
};

} // namespace

int main() {
	std::printf("== HOST-089 priority_queue ==\n");

	// --- Max-heap (por defecto) ---------------------------------------------
	{
		eu::PriorityQueue<int, 8> pq;
		static_assert(pq.capacity() == 8u, "capacity()");
		check(pq.empty() && pq.size() == 0u, "vacía");
		check(pq.push(3) && pq.push(1) && pq.push(4) && pq.push(1) && pq.push(5) &&
			      pq.push(9) && pq.push(2),
		      "push dentro de capacidad");
		check(pq.size() == 7u && !pq.full(), "tamaño");
		check(pq.top() == 9, "top = máximo");

		int last = 1000;
		int n = 0;
		while (!pq.empty()) {
			const int v = pq.top();
			check(v <= last, "la extracción es no creciente");
			last = v;
			pq.pop();
			++n;
		}
		check(n == 7 && pq.empty(), "pop vacía la cola");
	}

	// --- Min-heap (Greater) --------------------------------------------------
	{
		eu::PriorityQueue<int, 8, eu::Greater<int>> pq;
		pq.push(3);
		pq.push(1);
		pq.push(4);
		pq.push(1);
		pq.push(5);
		check(pq.top() == 1, "min-heap: top = mínimo");
		int last = -1;
		int n = 0;
		while (!pq.empty()) {
			const int v = pq.top();
			check(v >= last, "min-heap: extracción no decreciente");
			last = v;
			pq.pop();
			++n;
		}
		check(n == 5, "min-heap: extrae los 5");
	}

	// --- Llena y rechaza -----------------------------------------------------
	{
		eu::PriorityQueue<int, 3> pq;
		check(pq.push(1) && pq.push(2) && pq.push(3) && pq.full(), "llena");
		check(!pq.push(4) && pq.size() == 3u, "rechaza al estar llena");
		check(pq.top() == 3, "top conservado");
		pq.clear();
		check(pq.empty(), "clear");
	}

	// --- Comparador propio y emplace ----------------------------------------
	{
		eu::PriorityQueue<Task, 4, ByPrio> pq;
		check(pq.emplace(Task {.prio = 1, .id = 10}), "emplace 1");
		check(pq.emplace(Task {.prio = 5, .id = 20}), "emplace 2");
		check(pq.emplace(Task {.prio = 3, .id = 30}), "emplace 3");
		check(pq.top().id == 20, "top por prio");
		pq.pop();
		check(pq.top().id == 30, "siguiente por prio");
		pq.pop();
		check(pq.top().id == 10, "último por prio");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: PriorityQueue validada.\n");
	return 0;
}
