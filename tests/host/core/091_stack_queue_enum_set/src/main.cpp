// ============================================================================
// Test HOST-091: Stack/Queue/Deque y EnumSet.
// ============================================================================
//
// Respalda `eng/core/util/stack_queue.hpp` (incluye push_front/pop_back de RingBuffer)
// y `eng/core/util/enum_set.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/091_stack_queue_enum_set

#include <cstdio>

#include <eng/core/util/enum_set.hpp>
#include <eng/core/util/ring_buffer.hpp>
#include <eng/core/util/stack_queue.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

enum class Key : eng::u8 { Left = 0, Right = 1, Fire = 2 };

} // namespace

int main() {
	std::printf("== HOST-091 stack/queue/deque + enum_set ==\n");

	// --- Stack (LIFO) --------------------------------------------------------
	{
		eu::Stack<int, 3> s;
		check(s.empty() && s.capacity() == 3u, "Stack vacía");
		check(s.push(1) && s.push(2) && s.push(3) && s.full(), "Stack push");
		check(s.top() == 3, "Stack top LIFO");
		check(!s.push(4), "Stack rechaza al llenar");
		s.pop();
		check(s.top() == 2 && s.size() == 2u, "Stack pop");
		s.clear();
		check(s.empty(), "Stack clear");
	}

	// --- Queue (FIFO) --------------------------------------------------------
	{
		eu::Queue<int, 3> q;
		check(q.push(1) && q.push(2) && q.push(3) && q.full(), "Queue push");
		check(q.front() == 1 && q.back() == 3, "Queue front/back");
		check(q.pop() == 1, "Queue pop FIFO");
		q.push(4);
		check(q.front() == 2 && q.back() == 4 && q.size() == 3u, "Queue tras reciclar");
		check(!q.push(5), "Queue rechaza al llenar");
	}

	// --- Deque (doble cola) --------------------------------------------------
	{
		eu::Deque<int, 4> d;
		check(d.push_back(1) && d.push_back(2), "Deque push_back");
		check(d.push_front(0), "Deque push_front");
		check(d[0] == 0 && d[1] == 1 && d[2] == 2, "Deque orden interno");
		check(d.pop_front() == 0, "Deque pop_front");
		check(d.pop_back() == 2, "Deque pop_back");
		check(d.front() == 1 && d.back() == 1 && d.size() == 1u, "Deque estado");
		check(d.push_front(9) && d.push_back(8), "Deque vuelve a llenar");
		check(d[0] == 9 && d[2] == 8, "Deque orden tras reciclar");
	}

	// --- RingBuffer doble-ended (directo) ------------------------------------
	{
		eu::RingBuffer<int, 4> r;
		check(r.push_front(1) && r.push(2) && r.push_front(0), "RingBuffer push_front/push");
		check(r.front() == 0 && r.back() == 2 && r[1] == 1, "RingBuffer orden");
		check(r.pop_back() == 2, "RingBuffer pop_back");
		check(r.front() == 0 && r.back() == 1, "RingBuffer tras pop_back");
	}

	// --- EnumSet -------------------------------------------------------------
	{
		eu::EnumSet<Key, 3> set;
		check(set.none() && !set.any() && set.count() == 0u && !set.all(), "EnumSet vacío");
		set.set(Key::Fire);
		check(set.test(Key::Fire) && !set.test(Key::Left), "EnumSet set/test");
		set.set(Key::Left, true);
		set.flip(Key::Right);
		check(set.count() == 3u && set.all() && set.any(), "EnumSet con las 3");
		set.reset(Key::Left);
		check(!set.test(Key::Left) && set.count() == 2u, "EnumSet reset");
		eu::EnumSet<Key, 3> other;
		other.set(Key::Right);
		other.flip(Key::Fire);
		check(other == set, "EnumSet operator==");
		set.flip();
		check(!set.test(Key::Right) && !set.test(Key::Fire) && set.test(Key::Left),
		      "EnumSet flip()");
		(void)set.bits();
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Stack/Queue/Deque y EnumSet validados.\n");
	return 0;
}
