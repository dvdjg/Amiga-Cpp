// ============================================================================
// Test HOST-026: cache de chunks residentes con streaming (LRU) y presupuesto
// ============================================================================
//
// Valida `eng::field::ChunkCache<ChunkSize, Capacity>`: mantiene `Capacity` chunks
// en un pool aportado por el llamador, carga bajo demanda con un `Loader` y evicta
// por LRU. Es la pieza que hace viable un `WorldMap` disperso sin tener todo el
// mundo en Chip RAM.

#include <cstdio>

#include <eng/field/chunk_cache.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

struct LoaderData { int calls; };

eng::field::LoadResult fake_load(void* user, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) {
	++static_cast<LoaderData*>(user)->calls;
	for (eng::u32 i = 0; i < eng::field::ChunkCache<4, 2>::kCells; ++i) {
		dst[i] = static_cast<eng::u16>(cx * 100 + cy * 10 + static_cast<eng::s32>(i));
	}
	return eng::field::LoadResult::Ready;
}

// Carga que ejercita los tres estados: (9,*) aún no lista; (5,*) ausente; resto lista.
eng::field::LoadResult special_load(void* user, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) {
	++static_cast<LoaderData*>(user)->calls;
	if (cx == 9) return eng::field::LoadResult::Pending;
	if (cx == 5) {
		for (eng::u32 i = 0; i < eng::field::ChunkCache<4, 2>::kCells; ++i) dst[i] = 0xFFFFu;
		return eng::field::LoadResult::Empty;
	}
	for (eng::u32 i = 0; i < eng::field::ChunkCache<4, 2>::kCells; ++i) {
		dst[i] = static_cast<eng::u16>(cx * 100 + cy * 10 + static_cast<eng::s32>(i));
	}
	return eng::field::LoadResult::Ready;
}

} // namespace

int main() {
	using Cache = eng::field::ChunkCache<4, 2>;   // 2 chunks de 4x4
	eng::u16 pool[Cache::kPoolCells] {};          // pool del llamador
	LoaderData ld {0};
	Cache cache {};

	check(cache.init({ &fake_load, &ld }, eng::Span<eng::u16>(pool, Cache::kPoolCells)),
	      "init");
	check(!cache.init({ nullptr, nullptr }, eng::Span<eng::u16>(pool, Cache::kPoolCells)),
	      "init sin loader falla");

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
	using Cache2 = eng::field::ChunkCache<4, 2>;
	eng::u16 pool2[Cache2::kPoolCells] {};
	LoaderData ld2 {0};
	Cache2 c2 {};
	check(c2.init({ &special_load, &ld2 }, eng::Span<eng::u16>(pool2, Cache2::kPoolCells)), "init c2");
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
