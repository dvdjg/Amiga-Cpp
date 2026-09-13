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

bool fake_load(void* user, eng::s32 cx, eng::s32 cy, eng::u16* dst) {
	++static_cast<LoaderData*>(user)->calls;
	for (eng::u32 i = 0; i < eng::field::ChunkCache<4, 2>::kCells; ++i) {
		dst[i] = static_cast<eng::u16>(cx * 100 + cy * 10 + static_cast<eng::s32>(i));
	}
	return true;
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

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: ChunkCache (LRU + streaming) validado.\n");
	return 0;
}
