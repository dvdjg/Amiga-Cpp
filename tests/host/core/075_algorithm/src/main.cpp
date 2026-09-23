// ============================================================================
// Test HOST-075: algoritmos sobre Span (eng::util::algorithm).
// ============================================================================
//
// Respalda `eng/core/util/algorithm.hpp`: búsqueda, cuantificadores, recorrido,
// copia, reducción, búsqueda binaria, permutación y compactación, todo sobre
// memoria contigua y sin asignar.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/075_algorithm

#include <cstdio>

#include <eng/core/util/algorithm.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Número de elementos de un array C (para construir el `Span`).
template <class T, eng::usize N>
constexpr eng::Span<T> span_of(T (&a)[N]) {
	return eng::Span<T> {a, N};
}

} // namespace

int main() {
	std::printf("== HOST-075 algorithm ==\n");

	int data[6] = {3, 1, 4, 1, 5, 9};

	// --- Búsqueda ------------------------------------------------------------
	check(eu::find(span_of(data), 4) == &data[2], "find localiza 4");
	check(eu::find(span_of(data), 7) == data + 6, "find ausente devuelve end");
	check(eu::contains(span_of(data), 5), "contains(5)");
	check(!eu::contains(span_of(data), 8), "contains(8) falso");
	check(eu::count(span_of(data), 1) == 2, "count(1) = 2");
	check(*eu::find_if(span_of(data), [](int v) { return v > 4; }) == 5, "find_if(>4) = 5");
	check(eu::count_if(span_of(data), [](int v) { return v > 3; }) == 3, "count_if(>3) = 3");
	check(eu::all_of(span_of(data), [](int v) { return v > 0; }), "all_of(>0)");
	check(!eu::all_of(span_of(data), [](int v) { return v > 1; }), "all_of(>1) falso");
	check(eu::any_of(span_of(data), [](int v) { return v == 9; }), "any_of(==9)");
	check(eu::none_of(span_of(data), [](int v) { return v < 0; }), "none_of(<0)");

	// --- Recorrido / transform ----------------------------------------------
	int sum = 0;
	eu::for_each(span_of(data), [&sum](int v) { sum += v; });
	check(sum == 23, "for_each suma 23");

	eu::transform(span_of(data), [](int v) { return v * 2; });
	check(data[0] == 6 && data[5] == 18, "transform multiplica por 2");
	eu::transform(span_of(data), [](int v) { return v / 2; });

	// --- Copia / relleno -----------------------------------------------------
	int dst[4] = {0, 0, 0, 0};
	check(eu::copy(span_of(data), span_of(dst)) == 4, "copy se limita al destino");
	check(dst[0] == 3 && dst[3] == 1, "copy escribe el prefijo");
	int dst2[6] = {0};
	check(eu::copy_n(span_of(data), 3, span_of(dst2)) == 3, "copy_n copia 3");
	check(dst2[2] == 4 && dst2[3] == 0, "copy_n respeta el prefijo");
	check(eu::fill_n(span_of(dst2), 2, 7) == 2, "fill_n escribe 2");
	check(dst2[0] == 7 && dst2[1] == 7 && dst2[2] == 4, "fill_n no toca el resto");
	check(eu::equal(span_of(data), span_of(data)), "equal de la misma vista");
	check(!eu::equal(span_of(data), span_of(dst)), "equal con tamaños distintos");

	// --- Reducción -----------------------------------------------------------
	check(eu::accumulate(span_of(data), 0) == 23, "accumulate suma");
	check(eu::accumulate(span_of(data), 1, [](int acc, int v) { return acc * v; }) == 540,
	      "accumulate con producto");
	check(*eu::min_element(span_of(data)) == 1, "min_element");
	check(*eu::max_element(span_of(data)) == 9, "max_element");

	// --- Búsqueda binaria (vista ordenada) -----------------------------------
	int sorted[6] = {1, 1, 3, 4, 5, 9};
	check(eu::lower_bound(span_of(sorted), 4) == &sorted[3], "lower_bound(4)");
	check(eu::upper_bound(span_of(sorted), 1) == &sorted[2], "upper_bound(1)");
	check(eu::binary_search(span_of(sorted), 5), "binary_search(5)");
	check(!eu::binary_search(span_of(sorted), 6), "binary_search(6) falso");

	// --- Permutación ---------------------------------------------------------
	int rev[4] = {1, 2, 3, 4};
	eu::reverse(span_of(rev));
	check(rev[0] == 4 && rev[3] == 1, "reverse");

	int rot[5] = {1, 2, 3, 4, 5};
	eu::rotate(span_of(rot), 2);
	check(rot[0] == 3 && rot[2] == 5 && rot[3] == 1 && rot[4] == 2, "rotate por 2");

	int seq[4] = {0, 0, 0, 0};
	eu::iota(span_of(seq), 10);
	check(seq[0] == 10 && seq[3] == 13, "iota desde 10");

	// --- Compactación --------------------------------------------------------
	int raw[6] = {1, 2, 3, 4, 5, 6};
	const auto kept = eu::remove_if(span_of(raw), [](int v) { return (v % 2) == 0; });
	check(kept.size() == 3 && raw[0] == 1 && raw[1] == 3 && raw[2] == 5, "remove_if impares");

	int dup[7] = {1, 1, 2, 2, 2, 3, 3};
	const auto uniq = eu::unique(span_of(dup));
	check(uniq.size() == 3 && dup[0] == 1 && dup[1] == 2 && dup[2] == 3, "unique");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: algoritmos validados.\n");
	return 0;
}
