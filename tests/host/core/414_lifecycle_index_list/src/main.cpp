// ============================================================================
// Test HOST-414: ciclo de vida (Noncopyable/NonMovable) y lista por índices.
// ============================================================================
//
// Respalda `eng/core/util/noncopyable.hpp` y `eng/core/util/index_list.hpp`:
// - `Noncopyable`/`NonMovable` marcan el ciclo de vida de un tipo sin repetir el
//   par de `= delete`; se comprueba que los contenedores que las adoptan (Vector,
//   SmallVector, ChunkedVector, DynamicHashMap) no se copian, y que LruCache tampoco
//   se mueve (sus listas referencian arrays internos).
// - `IndexList` es la lista doble por índices que LruCache usa; se prueba por
//   separado, incluido el reparto de arrays entre dos listas disjuntas.
//
// Ejecucion:
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/414_lifecycle_index_list

#include <cstdio>

#include <eng/core/util/chunked_vector.hpp>
#include <eng/core/util/dynamic_hash_map.hpp>
#include <eng/core/util/index_list.hpp>
#include <eng/core/util/lru_cache.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/core/util/small_vector.hpp>
#include <eng/core/util/vector.hpp>

using eng::u16;
namespace eu = eng::util;

namespace {

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// --- Base de ciclo de vida: no copiable y no movible ------------------------
static_assert(!__is_constructible(eu::Noncopyable, const eu::Noncopyable&),
	      "Noncopyable no se copia");
static_assert(!__is_constructible(eu::NonMovable, const eu::NonMovable&),
	      "NonMovable no se copia");
static_assert(!__is_constructible(eu::NonMovable, eu::NonMovable&&),
	      "NonMovable no se mueve");

// --- Los contenedores adoptan Noncopyable (no copia; el movimiento se declara) ---
static_assert(!__is_constructible(eu::Vector<u16>, const eu::Vector<u16>&), "Vector no copiable");
static_assert(__is_constructible(eu::Vector<u16>, eu::Vector<u16>&&), "Vector movible");
static_assert(!__is_constructible(eu::SmallVector<u16, 4>, const eu::SmallVector<u16, 4>&),
	      "SmallVector no copiable");
static_assert(__is_constructible(eu::SmallVector<u16, 4>, eu::SmallVector<u16, 4>&&),
	      "SmallVector movible");
static_assert(!__is_constructible(eu::ChunkedVector<u16, 4, 2>,
				  const eu::ChunkedVector<u16, 4, 2>&),
	      "ChunkedVector no copiable");
static_assert(!__is_constructible(eu::DynamicHashMap<u16, u16>,
				  const eu::DynamicHashMap<u16, u16>&),
	      "DynamicHashMap no copiable");
// LruCache guarda punteros a sus propios arrays: ni copia ni movimiento.
static_assert(!__is_constructible(eu::LruCache<u16, eng::s32, 4>,
				  const eu::LruCache<u16, eng::s32, 4>&),
	      "LruCache no copiable");
static_assert(!__is_constructible(eu::LruCache<u16, eng::s32, 4>,
				  eu::LruCache<u16, eng::s32, 4>&&),
	      "LruCache no movible");

} // namespace

int main() {
	std::printf("== HOST-414 ciclo de vida + lista por indices ==\n");

	constexpr eng::usize kCap = 8u;
	constexpr u16 kNull = 0xffffu;

	// --- IndexList: orden, recorrido y reenlazado O(1) -----------------------
	{
		u16 prev[kCap] {};
		u16 next[kCap] {};
		eu::IndexList<u16, kNull> list;
		list.bind(prev, next);
		check(list.empty() && list.size() == 0u, "index_list: arranca vacia");

		list.push_back(2u);
		list.push_back(5u);
		list.push_back(7u);
		check(list.size() == 3u && list.front() == 2u && list.back() == 7u,
		      "index_list: push_back mantiene cabeza/cola");
		check(list.next_of(2u) == 5u && list.prev_of(7u) == 5u, "index_list: enlaces");

		u16 sum = 0u;
		for (u16 i : list) {
			sum = static_cast<u16>(sum + i);
		}
		check(sum == 14u, "index_list: iteracion en orden");

		list.push_front(1u);
		check(list.front() == 1u && list.next_of(1u) == 2u, "index_list: push_front");

		list.erase(5u);
		check(list.size() == 3u && list.prev_of(7u) == 2u && list.next_of(2u) == 7u,
		      "index_list: erase recompone los vecinos");

		list.touch_front(7u);
		check(list.front() == 7u && list.next_of(7u) == 1u, "index_list: touch_front");

		check(list.pop_front() == 7u, "index_list: pop_front");
		check(list.pop_back() == 2u, "index_list: pop_back");
		check(list.size() == 1u && list.front() == 1u && list.back() == 1u,
		      "index_list: queda un solo nodo");
	}

	// --- Dos listas comparten los arrays (ranuras disjuntas) -----------------
	{
		u16 prev[kCap] {};
		u16 next[kCap] {};
		eu::IndexList<u16, kNull> a;
		eu::IndexList<u16, kNull> b;
		a.bind(prev, next);
		b.bind(prev, next);

		a.push_back(0u);
		a.push_back(1u);
		b.push_back(2u);
		b.push_back(3u);
		check(a.size() == 2u && b.size() == 2u, "index_list: dos listas independientes");

		u16 sa = 0u;
		for (u16 i : a) sa = static_cast<u16>(sa + i);
		u16 sb = 0u;
		for (u16 i : b) sb = static_cast<u16>(sb + i);
		check(sa == 1u && sb == 5u, "index_list: cada lista recorre sus indices");

		a.erase(0u);
		check(a.front() == 1u && b.front() == 2u, "index_list: erase no afecta a la otra");
	}

	if (g_fail == 0u) {
		std::printf("OK: ciclo de vida (noncopyable/nonmovable) y IndexList validados.\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
