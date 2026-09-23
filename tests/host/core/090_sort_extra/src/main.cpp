// ============================================================================
// Test HOST-090: ordenación ampliada (stable/nth/partial/is_sorted/radix).
// ============================================================================
//
// Respalda `eng/core/data/sort.hpp`: stable_sort (con y sin scratch), nth_element,
// partial_sort, is_sorted y radix_sort_u16.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/090_sort_extra

#include <cstdio>

#include <eng/core/data/sort.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Rec {
	eng::u16 key = 0;
	eng::u16 seq = 0; // orden de inserción (para comprobar estabilidad)
};

template <class T, eng::usize N>
constexpr eng::Span<T> span_of(T (&a)[N]) {
	return eng::Span<T> {a, N};
}

} // namespace

int main() {
	std::printf("== HOST-090 sort extra ==\n");

	const auto by_key = [](const Rec& a, const Rec& b) { return a.key < b.key; };
	const auto int_less = [](int a, int b) { return a < b; };

	// --- stable_sort con scratch --------------------------------------------
	{
		Rec data[6] = {{3, 0}, {1, 1}, {3, 2}, {1, 3}, {2, 4}, {1, 5}};
		Rec scratch[6];
		eng::stable_sort(span_of(data), by_key, span_of(scratch));
		for (eng::usize i = 1; i < 6u; ++i) {
			check(data[i - 1u].key <= data[i].key, "stable_sort ordenado por clave");
		}
		// Con claves iguales, el `seq` debe crecer (estable).
		check(data[0].seq == 1 && data[1].seq == 3 && data[2].seq == 5,
		      "stable_sort conserva el orden de inserción");
		check(data[3].seq == 4 && data[4].seq == 0 && data[5].seq == 2,
		      "stable_sort ordena el resto");
	}

	// --- stable_sort sin scratch (inserción) --------------------------------
	{
		Rec data[5] = {{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
		eng::stable_sort(span_of(data), by_key);
		check(data[0].seq == 1 && data[1].seq == 3, "stable sin scratch: claves 1 estables");
		check(data[2].seq == 0 && data[3].seq == 2 && data[4].seq == 4,
		      "stable sin scratch: claves 2 estables");
	}

	// --- nth_element ---------------------------------------------------------
	{
		int data[7] = {9, 3, 7, 1, 8, 2, 5};
		eng::nth_element(span_of(data), 3u, int_less);
		// El elemento 3 (4º) es el que quedaría ahí ordenado: 5.
		check(data[3] == 5, "nth_element coloca el 4º menor");
		for (eng::usize i = 0; i < 3u; ++i) {
			check(data[i] <= data[3], "nth_element: menores antes");
		}
		for (eng::usize i = 4u; i < 7u; ++i) {
			check(data[3] <= data[i], "nth_element: mayores después");
		}
	}

	// --- partial_sort --------------------------------------------------------
	{
		int data[7] = {9, 3, 7, 1, 8, 2, 5};
		eng::partial_sort(span_of(data), 3u, int_less);
		check(data[0] == 1 && data[1] == 2 && data[2] == 3,
		      "partial_sort: los 3 menores, ordenados");
	}

	// --- is_sorted -----------------------------------------------------------
	{
		int good[4] = {1, 2, 3, 4};
		int bad[4] = {1, 3, 2, 4};
		check(eng::is_sorted(span_of(good), int_less), "is_sorted true");
		check(!eng::is_sorted(span_of(bad), int_less), "is_sorted false");
	}

	// --- radix_sort_u16 ------------------------------------------------------
	{
		eng::u16 data[10] = {500, 3, 65535, 0, 42, 300, 3, 1, 999, 7};
		eng::u16 scratch[10];
		int ref[10] = {500, 3, 65535, 0, 42, 300, 3, 1, 999, 7};
		eng::quick_sort(span_of(ref), int_less);
		check(eng::radix_sort_u16(span_of(data), span_of(scratch)), "radix_sort_u16 ok");
		bool same = true;
		for (eng::usize i = 0; i < 10u; ++i) {
			same = same && (data[i] == static_cast<eng::u16>(ref[i]));
		}
		check(same, "radix_sort_u16 coincide con quicksort de referencia");
		eng::u16 tiny[1] = {5};
		check(eng::radix_sort_u16(span_of(tiny), span_of(scratch)) == true,
		      "radix con 1 elemento");
		eng::u16 small_scratch[2];
		eng::u16 big[4] = {4, 3, 2, 1};
		check(!eng::radix_sort_u16(span_of(big), span_of(small_scratch)),
		      "radix rechaza scratch insuficiente");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ordenación ampliada validada.\n");
	return 0;
}
