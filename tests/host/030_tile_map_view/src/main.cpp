// ============================================================================
// Test HOST-030: TileMapView (accesor de scroll con límites) + wrap_period
// ============================================================================
//
// Valida `eng::field::TileMapView<Src>`: envuelve un accesor `TileSource` (aquí un
// `StreamingWorldMap`) con los límites/wrap del mundo que consume el scroll, de modo
// que el playfield no depende del almacén concreto. También valida `wrap_period`.

#include <cstdio>

#include <eng/field/streaming_map.hpp>
#include <eng/field/tile_source.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::s32;
using eng::u16;
using eng::u32;
using Map = eng::field::StreamingWorldMap<16, 4>;
constexpr u16 kEmpty = 0xFFFFu;
} // namespace

namespace {
// Chunk "poblado": valor único por celda para verificar el direccionamiento.
eng::field::LoadResult load_chunk(void*, s32 cx, s32 cy, eng::TileBankBuffer cells) {
	const s32 ccx = cx & 15; // wrap de chunks en X (16 chunks = mundo 256)
	for (u32 i = 0; i < Map::kCells; ++i) {
		cells[i] = static_cast<u16>((ccx * 16 + cy) * 256 + static_cast<s32>(i));
	}
	return eng::field::LoadResult::Ready;
}
} // namespace

int main() {
	// wrap_period: potencia de dos (máscara, incluye negativos), no-pow2 y 0.
	check(eng::field::wrap_period(-1, 256) == 255, "wrap_period(-1,256)==255");
	check(eng::field::wrap_period(300, 256) == 44, "wrap_period(300,256)==44");
	check(eng::field::wrap_period(-1, 10) == 9, "wrap_period(-1,10)==9");
	check(eng::field::wrap_period(5, 0) == 5, "wrap_period(_,0) sin wrap");

	u16 pool[Map::kPoolCells] {};
	Map world {};
	check(world.init({ &load_chunk, nullptr }, eng::TileBankBuffer{pool}, kEmpty), "init");

	eng::field::TileMapView<Map> view {};
	check(!view.has_data(), "vista sin src -> sin datos");
	check(view.tile_at(3, 4) == 0 && view.is_empty(0xABCDu) == false, "src null -> edge_tile, no vacío");

	view.src = &world;
	view.width = 256; view.height = 32;
	view.wrap_x = 256; view.wrap_y = 0; view.edge_tile = 0;
	check(view.has_data(), "vista con src -> datos");

	// Antes de prefetch no hay residentes -> streaming devuelve `empty`.
	check(view.is_empty(view.tile_at(0, 0)), "no residente -> empty");

	world.prefetch(0, 0, 16, 16); // cubre el chunk (0,0)
	const u16 expect = static_cast<u16>((0 * 16 + 0) * 256 + (2 * 16 + 1));
	check(view.tile_at(1, 2) == expect, "residente: celda (1,2) del chunk (0,0)");
	check(!view.is_empty(view.tile_at(1, 2)), "residente no es vacío");

	// Wrap en X: cotas negativas y > width caen en la misma celda.
	check(view.tile_at(-1, 2) == view.tile_at(255, 2), "wrap X (-1 == 255)");
	check(view.tile_at(257, 2) == view.tile_at(1, 2), "wrap X (257 == 1)");
	// Y acotado: fuera de rango -> edge_tile.
	check(view.tile_at(1, 32) == 0, "Y fuera de mundo -> edge_tile");

	// Fuerza evicciones: la capacidad es 4. Tras 5 chunks, el más antiguo sale.
	world.prefetch(64, 0, 80, 16);   // chunk (4,0)
	world.prefetch(128, 0, 144, 16); // chunk (8,0)
	world.prefetch(192, 0, 208, 16); // chunk (12,0)
	world.prefetch(240, 0, 256, 16); // chunk (15,0) -> evicta el LRU
	check(world.evictions() >= 1, "evicciones al superar la capacidad");
	// El documento del piloto: el acceso durante el dibujo es SOLO residente.
	check(!view.is_empty(view.tile_at(192, 1)), "chunk recién prefetcheado residente");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: TileMapView (accesor + límites/wrap + solo-residentes) validado.\n");
	return 0;
}
