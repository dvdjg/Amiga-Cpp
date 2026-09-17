// ============================================================================
// Test HOST-087: listas intrusivas (IntrusiveList / IntrusiveSList).
// ============================================================================
//
// Respalda `eng/core/util/intrusive_list.hpp`: enlace dentro del objeto, O(1) sin
// asignar, iteración y uso como free-list.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/087_intrusive_list

#include <cstdio>

#include <eng/core/util/intrusive_list.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Job : eu::IntrusiveLink<Job> {
	int v = 0;
};

struct Node : eu::IntrusiveSLink<Node> {
	int v = 0;
};

} // namespace

int main() {
	std::printf("== HOST-087 intrusive_list ==\n");

	// --- Lista doble ---------------------------------------------------------
	{
		Job a, b, c;
		a.v = 1;
		b.v = 2;
		c.v = 3;
		eu::IntrusiveList<Job> list;
		check(list.empty() && list.size() == 0u, "vacía");
		list.push_back(&a);
		list.push_back(&c);
		list.push_front(&b);
		check(list.size() == 3u && !list.empty(), "tamaño tras inserts");
		check(list.front() == &b && list.back() == &c, "front/back");
		check(a.prev == &b && a.next == &c && b.prev == nullptr && c.next == nullptr,
		      "enlaces dobles");

		// Iteración en orden.
		int order[3] = {0, 0, 0};
		int n = 0;
		for (Job* j : list) {
			order[n++] = j->v;
		}
		check(n == 3 && order[0] == 2 && order[1] == 1 && order[2] == 3, "iteración en orden");

		check(list.contains(&a) && !list.contains(nullptr), "contains");
		list.erase(&a);
		check(list.size() == 2u && !list.contains(&a), "erase O(1)");
		check(a.prev == nullptr && a.next == nullptr, "erase limpia el enlace");
		check(list.front() == &b && list.back() == &c, "cabecera/cola tras erase");

		check(list.pop_front() == &b && list.pop_front() == &c, "pop_front");
		check(list.empty() && list.pop_front() == nullptr, "vacía tras pop_front");

		// erase del último restante por pop_back.
		list.push_back(&a);
		check(list.pop_back() == &a && list.empty(), "pop_back");

		list.push_back(&a);
		list.push_back(&b);
		list.clear();
		check(list.empty() && list.size() == 0u, "clear");
		check(a.prev == nullptr && a.next == nullptr && b.prev == nullptr,
		      "clear limpia los enlaces");
	}

	// --- Lista simple + uso como free-list -----------------------------------
	{
		Node pool[4];
		for (int i = 0; i < 4; ++i) {
			pool[i].v = i;
		}
		eu::IntrusiveSList<Node> free_list;
		for (int i = 0; i < 4; ++i) {
			free_list.push_front(&pool[i]);
		}
		check(free_list.size() == 4u, "free-list llena");

		// Reparto: pop da el último empujado (LIFO).
		Node* n0 = free_list.pop_front();
		Node* n1 = free_list.pop_front();
		check(n0 == &pool[3] && n1 == &pool[2] && free_list.size() == 2u, "pop_front LIFO");

		// Devolver un nodo a la free-list.
		free_list.push_front(n0);
		check(free_list.front() == n0 && free_list.size() == 3u, "devolver a free-list");

		// insert_after / erase_after.
		free_list.insert_after(n0, n1);
		check(free_list.size() == 4u && n0->next == n1, "insert_after");
		check(free_list.erase_after(n0) == n1 && free_list.size() == 3u, "erase_after");

		check(free_list.contains(n0) && !free_list.contains(nullptr), "contains simple");

		int count = 0;
		for (Node* it : free_list) {
			(void)it;
			++count;
		}
		check(static_cast<eng::usize>(count) == free_list.size(), "iteración simple");

		free_list.clear();
		check(free_list.empty(), "clear simple");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: listas intrusivas validadas.\n");
	return 0;
}
