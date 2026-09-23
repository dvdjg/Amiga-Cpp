// ============================================================================
// Test HOST-131: medicion del heap (R5.5) — binario (PriorityQueue) vs 4-ario
// ============================================================================
//
// No valida una API nueva: mide cuantas comparaciones hace el heap binario actual
// (`eng::util::PriorityQueue`) frente a un heap 4-ario equivalente en una carga tipo
// `open set` de A* (N inserciones y N extracciones de minimo). Sirve para decidir si
// merece la pena adoptar el 4-ario en el 68000.
//
// Verificacion: mismo orden de salida en ambos (correctitud) + tabla de comparaciones.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/131_heap_bench

#include <cstdio>

#include <eng/core/util/priority_queue.hpp>

namespace {

using eng::u16;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

unsigned long long g_cmp = 0;

/// Comparador de minimo que cuenta cada comparacion.
struct CmpCount {
	[[nodiscard]] bool operator()(u16 a, u16 b) const noexcept {
		++g_cmp;
		return a > b; // min-heap
	}
};

/// Clave determinista (LCG) para que ambos heaps reciban la misma secuencia.
[[nodiscard]] u16 key_for(usize i) {
	unsigned long long x = static_cast<unsigned long long>(i) * 2654435761ull + 12345ull;
	return static_cast<u16>((x >> 8u) & 0xffffu);
}

/// Heap 4-ario de minimo (candidato), con el mismo contador de comparaciones.
template <usize N>
class Heap4 {
public:
	[[nodiscard]] bool push(u16 v) noexcept {
		if (m_n >= N) {
			return false;
		}
		m_data[m_n] = v;
		usize i = m_n++;
		while (i > 0u) {
			const usize parent = (i - 1u) / 4u;
			if (!less(m_data[i], m_data[parent])) {
				break;
			}
			swap_at(i, parent);
			i = parent;
		}
		return true;
	}
	[[nodiscard]] u16 pop() noexcept {
		const u16 top = m_data[0];
		m_data[0] = m_data[--m_n];
		usize i = 0u;
		for (;;) {
			const usize first = 4u * i + 1u;
			if (first >= m_n) {
				break;
			}
			usize best = first;
			for (usize c = 1u; c < 4u && first + c < m_n; ++c) {
				if (less(m_data[first + c], m_data[best])) {
					best = first + c;
				}
			}
			if (!less(m_data[best], m_data[i])) {
				break;
			}
			swap_at(i, best);
			i = best;
		}
		return top;
	}
	[[nodiscard]] bool empty() const noexcept { return m_n == 0u; }

private:
	[[nodiscard]] static bool less(u16 a, u16 b) noexcept {
		++g_cmp;
		return a < b;
	}
	void swap_at(usize a, usize b) noexcept {
		const u16 t = m_data[a];
		m_data[a] = m_data[b];
		m_data[b] = t;
	}
	u16 m_data[N] {};
	usize m_n = 0u;
};

template <usize N>
void bench(const char* label) {
	// Binario (PriorityQueue con comparador contado).
	g_cmp = 0;
	eng::util::PriorityQueue<u16, N, CmpCount> pq;
	for (usize i = 0; i < N; ++i) {
		(void)pq.push(key_for(i));
	}
	u16 bin[N] {};
	for (usize i = 0; i < N; ++i) {
		bin[i] = pq.top();
		pq.pop();
	}
	const unsigned long long bin_cmp = g_cmp;

	// 4-ario.
	g_cmp = 0;
	Heap4<N> h4;
	for (usize i = 0; i < N; ++i) {
		(void)h4.push(key_for(i));
	}
	u16 four[N] {};
	for (usize i = 0; i < N; ++i) {
		four[i] = h4.pop();
	}
	const unsigned long long four_cmp = g_cmp;

	bool same = true;
	for (usize i = 0; i < N; ++i) {
		if (bin[i] != four[i]) {
			same = false;
		}
	}
	check(same, "misma secuencia de salida");
	const double ratio = static_cast<double>(four_cmp) / static_cast<double>(bin_cmp);
	std::printf("  %-10s N=%3u  binario=%6llu  cuatro=%6llu  ratio=%.2f\n", label,
		    static_cast<unsigned>(N), bin_cmp, four_cmp, ratio);
}

} // namespace

int main() {
	std::printf("HeapBench (comparaciones, min-heap):\n");
	bench<32>("open set");
	bench<64>("open set");
	bench<128>("open set");
	bench<256>("open set");

	if (g_fail == 0u) {
		std::printf("OK: binario y 4-ario dan el mismo orden (ver tabla de coste)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
