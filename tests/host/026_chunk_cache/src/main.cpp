// ============================================================================
// Test HOST-026: cache de chunks residentes con streaming (LRU) y presupuesto
// ============================================================================
//
// Valida `eng::field::ChunkCache<ChunkSize, Capacity, Loader>`: mantiene `Capacity`
// chunks en un pool aportado por el llamador, carga bajo demanda con un loader (un
// **tipo** con `load(cx,cy,dst)`, concept `ChunkLoader`) y evicta por LRU. Es la
// pieza que hace viable un `WorldMap` disperso sin tener todo el mundo en Chip RAM.

#include <cstdio>

#include <eng/field/chunk_cache.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

struct LoaderData { int calls; };

/// Loader de prueba: rellena cada chunk con un patrón dependiente de (cx,cy).
struct FakeLoader {
	LoaderData* data;
	eng::field::LoadResult load(eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) {
		++data->calls;
		for (eng::u32 i = 0; i < dst.size(); ++i) {
			dst[i] = static_cast<eng::u16>(cx * 100 + cy * 10 + static_cast<eng::s32>(i));
		}
		return eng::field::LoadResult::Ready;
	}
};

/// Carga que ejercita los tres estados: (9,*) aún no lista; (5,*) ausente; resto lista.
struct SpecialLoader {
	LoaderData* data;
	eng::field::LoadResult load(eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) {
		++data->calls;
		if (cx == 9) return eng::field::LoadResult::Pending;
		if (cx == 5) {
			for (eng::u32 i = 0; i < dst.size(); ++i) dst[i] = 0xFFFFu;
			return eng::field::LoadResult::Empty;
		}
		for (eng::u32 i = 0; i < dst.size(); ++i) {
			dst[i] = static_cast<eng::u16>(cx * 100 + cy * 10 + static_cast<eng::s32>(i));
		}
		return eng::field::LoadResult::Ready;
	}
};

} // namespace

int main() {
	using Cache = eng::field::ChunkCache<4u, 2u, FakeLoader>; // 2 chunks de 4x4
	eng::u16 pool[Cache::kPoolCells] {};                      // pool del llamador
	LoaderData ld {0};
	FakeLoader loader {&ld};
	Cache cache {};

	check(cache.init(loader, eng::TileBankBuffer {pool}), "init");
	// Un pool demasiado pequeño no se acepta (no se toca el loader).
	check(!cache.init(loader, eng::TileBankBuffer {pool, 0u}), "init con pool pequeno falla");

	const eng::u16* a = cache.get(0, 0);
	check(a != nullptr && a[0] == 0 && a[15] == 15, "carga (0,0) y datos");
	check(cache.loads() == 1 && cache.hits() == 0 && cache.evictions() == 0, "stats tras 1 load");

	const eng::u16* a2 = cache.get(0, 0);
	check(a2 == a && cache.hits() == 1, "hit (0,0) devuelve el mismo puntero");

	const eng::u16* b = cache.get(1, 0);
	check(b != nullptr && b[0] == 100 && cache.loads() == 2, "carga (1,0)");

	// Capacidad 2 llena: pedir (2,0) evicta el LRU. Tras el hit, (1,0) es el más
	// reciente y (0,0) el menos reciente -> se evicta (0,0).
	const eng::u16* c = cache.get(2, 0);
	check(c != nullptr && c[0] == 200 && cache.loads() == 3 && cache.evictions() == 1, "evicta LRU");

	// (1,0) sigue residente (era el MRU); (0,0) se recargó.
	check(cache.get(1, 0) == b && cache.hits() == 2, "LRU conservó (1,0)");
	check(cache.get(0, 0) != nullptr && cache.loads() == 4, "(0,0) recargado");

	// Tres estados del Loader: Pending no queda residente (se reintenta), Empty sí.
	using Cache2 = eng::field::ChunkCache<4u, 2u, SpecialLoader>;
	eng::u16 pool2[Cache2::kPoolCells] {};
	LoaderData ld2 {0};
	SpecialLoader special {&ld2};
	Cache2 c2 {};
	check(c2.init(special, eng::TileBankBuffer {pool2}), "init c2");
	check(c2.get(9, 0) == nullptr && c2.pendings() == 1 && c2.loads() == 0,
	      "Pending: sin residente ni load");
	check(c2.get(9, 0) == nullptr && c2.pendings() == 2, "Pending se reintenta");
	check(c2.get(5, 0) != nullptr && c2.empties() == 1 && c2.loads() == 1,
	      "Empty: residente (no reintenta)");
	check(c2.get(5, 0) != nullptr && c2.hits() == 1 && ld2.calls == 3, "Empty residente");
	check(c2.get(0, 0) != nullptr && c2.loads() == 2, "Ready tras Pending");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: ChunkCache (LRU + streaming) validado.\n");
	return 0;
}
