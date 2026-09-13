// ============================================================================
// Test HOST-029: mundo disperso con streaming (prefetch + solo-residentes)
// ============================================================================
//
// Valida `eng::field::StreamingWorldMap`: prefetch de los chunks que cubren la
// ventana, lectura de SOLO residentes (sin cargar durante el dibujo), chunks
// ausentes como `empty_tile`, y telemetría de loads/evictions.

#include <cstdio>

#include <eng/field/streaming_map.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
struct Src { int calls; };
} // namespace

namespace {
using Map = eng::field::StreamingWorldMap<4, 2>;

eng::field::LoadResult load_chunk(void* user, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer cells) {
	++static_cast<Src*>(user)->calls;
	if (cx >= 1 && cx <= 4) {   // chunks poblados (0,0)
		for (eng::u32 i = 0; i < Map::kCells; ++i) {
			cells[i] = static_cast<eng::u16>(cx * 100 + cy * 10 + static_cast<eng::s32>(i));
		}
		return eng::field::LoadResult::Ready;
	}
	return eng::field::LoadResult::Empty;   // ausente
}
} // namespace

int main() {
	eng::u16 pool[Map::kPoolCells] {};
	Src src {0};
	Map map {};

	check(map.init({ &load_chunk, &src }, eng::TileBankBuffer{pool}), "init");
	check(map.is_empty(map.tile_at(4, 0)), "no residente -> empty antes de prefetch");

	// prefetch de la ventana que cubre el chunk (1,0).
	map.prefetch(4, 0, 8, 1);
	check(map.tile_at(4, 0) == 100u && map.tile_at(7, 3) == 115u, "residente (1,0)");
	check(map.loads() == 1, "1 load");

	// chunk (0,0) NO poblado -> residente vacío (no se recarga cada prefetch).
	map.prefetch(0, 0, 1, 1);
	check(map.is_empty(map.tile_at(0, 0)), "chunk ausente -> empty");
	check(map.loads() == 2, "el chunk vacío también queda residente");
	const eng::u32 l0 = map.loads();
	map.prefetch(0, 0, 1, 1);
	check(map.loads() == l0, "prefetch repetido no recarga");

	// Prefetch lejano: la capacidad (2) obliga a evictar.
	map.prefetch(8, 0, 12, 1);    // chunk (2,0)
	map.prefetch(12, 0, 16, 1);   // chunk (3,0) -> evicta el LRU
	check(map.evictions() >= 1, "evicciones por capacidad");
	check(map.tile_at(12, 0) == 300u, "chunk (3,0) residente");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: StreamingWorldMap (prefetch, residentes, empty, evictions) validado.\n");
	return 0;
}
