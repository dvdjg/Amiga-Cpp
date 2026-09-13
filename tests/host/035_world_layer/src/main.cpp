// ============================================================================
// Test HOST-035: WorldView -> scroll (WorldLayerSource + Loader-RAM)
// ============================================================================
//
// Valida los puentes `eng::field::WorldLayerSource` (capa de `WorldView` como
// `TileMap`) y `eng::field::WorldMapChunkLoader` (Loader de `StreamingWorldMap`
// que sirve chunks desde el blob del mundo). Es la cadena completa:
// WorldMap -> StreamingWorldMap (Loader-RAM) -> TileMapView -> contrato `TileMap`.

#include <cstdio>

#include <eng/assets/uaf.hpp>
#include <eng/field/world_layer.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::u8;
using eng::u16;
using eng::u32;
using eng::assets::WorldView;
using eng::assets::write_be16;
using eng::assets::write_be32;

constexpr u32 kHdr = WorldView::kHeaderSize;     // 16
constexpr u32 kLd = WorldView::kLayerDescSize;   // 32
constexpr u32 kCe = WorldView::kChunkEntrySize;  // 4
constexpr u32 kDesc0 = kHdr;                     // 16
constexpr u32 kDir0 = kHdr + kLd;                // 48
constexpr u32 kCells0 = kDir0 + 2u * kCe;        // 56
constexpr u32 kTotal = kCells0 + 2u * 256u * 2u; // 1080

void build_world(eng::u8* buf, u32 size) {
	for (u32 i = 0; i < size; ++i) buf[i] = 0;
	write_be16(buf + 0u, 1u);   // version
	buf[4] = 4u;                // chunk_log2 -> 16x16
	buf[5] = 1u;                // 1 capa
	// LayerDesc: id,kind,width,height,wrap_x,wrap_y,empty,meta_count,dir_off,dir_count,cells_off,meta_off
	write_be16(buf + kDesc0 + 0u, 0u);
	write_be16(buf + kDesc0 + 4u, 32u);      // width
	write_be16(buf + kDesc0 + 6u, 16u);      // height
	write_be16(buf + kDesc0 + 12u, 0xFFFFu); // empty_tile
	write_be32(buf + kDesc0 + 16u, kDir0);
	write_be32(buf + kDesc0 + 20u, 2u);      // dir_count
	write_be32(buf + kDesc0 + 24u, kCells0);
	// Directorio ordenado (cy,cx): (0,0),(1,0)
	write_be16(buf + kDir0 + 0u, 0u);
	write_be16(buf + kDir0 + 2u, 0u);
	write_be16(buf + kDir0 + 4u, 1u);
	write_be16(buf + kDir0 + 6u, 0u);
	// Celdas: chunk 0 -> 1..256 ; chunk 1 -> 1001..1256
	for (u32 i = 0; i < 256u; ++i) write_be16(buf + kCells0 + i * 2u, static_cast<u16>(1u + i));
	for (u32 i = 0; i < 256u; ++i) write_be16(buf + kCells0 + 512u + i * 2u, static_cast<u16>(1001u + i));
}
} // namespace

int main() {
	// El adaptador directo cumple el contrato del scroll.
	static_assert(eng::field::TileMap<eng::field::WorldLayerSource>,
	              "WorldLayerSource debe cumplir TileMap");

	eng::u8 buf[1200] {};
	build_world(buf, sizeof(buf));
	WorldView w {};
	check(w.read(eng::UafPayload(buf, kTotal)), "read WorldMap");

	// 1) Capa entera residente como TileMap.
	eng::field::WorldLayerSource ws {};
	check(ws.bind(w, 0u), "bind capa 0");
	check(ws.has_data() && ws.width == 32u && ws.height == 16u && ws.wrap_x == 0u,
	      "bounds de la capa");
	check(ws.tile_at(0, 0) == 1u && ws.tile_at(15, 0) == 16u, "celda chunk 0");
	check(ws.tile_at(16, 0) == 1001u && ws.tile_at(17, 0) == 1002u, "cruce al chunk 1");
	check(ws.is_empty(ws.tile_at(32, 0)) && ws.tile_at(32, 0) == 0xFFFFu, "fuera de width -> empty");
	check(ws.is_empty(ws.tile_at(0, 16)), "fuera de height -> empty");

	// 2) Streaming con Loader-RAM desde el blob del mundo.
	using World = eng::field::StreamingWorldMap<16, 4>;
	using View = eng::field::TileMapView<World>;
	static_assert(eng::field::TileMap<View>, "TileMapView debe cumplir TileMap");

	eng::u16 pool[World::kPoolCells] {};
	World world {};
	eng::field::WorldMapChunkLoader loader {&w, 0u, 16u};
	check(world.init({&eng::field::WorldMapChunkLoader::load, &loader},
	                 eng::TileBankBuffer{pool}, 0xFFFFu), "init streaming");

	View view {};
	view.src = &world;
	view.width = ws.width; view.height = ws.height;
	view.wrap_x = ws.wrap_x; view.wrap_y = ws.wrap_y; view.edge_tile = 0xFFFFu;

	// Sin prefetch, el chunk no está residente -> empty (solo-residentes).
	check(view.is_empty(view.tile_at(0, 0)), "no residente -> empty antes de prefetch");

	// Prefetch de las dos columnas de chunks y comparación con el acceso directo.
	world.prefetch(0, 0, 16, 16);
	check(view.tile_at(0, 0) == 1u && view.tile_at(15, 0) == 16u, "chunk (0,0) desde Loader-RAM");
	world.prefetch(16, 0, 32, 16);
	check(view.tile_at(16, 0) == 1001u && view.tile_at(17, 0) == 1002u, "chunk (1,0) desde Loader-RAM");

	// Equivalencia en un barrido del mundo.
	bool eq = true;
	for (eng::s32 y = 0; y < 16 && eq; ++y) {
		for (eng::s32 x = 0; x < 32 && eq; ++x) eq = view.tile_at(x, y) == ws.tile_at(x, y);
	}
	check(eq, "streaming == capa directa (barrido)");

	// Chunk ausente: el Loader devuelve Empty sin romper.
	eng::u16 tmp[256] {};
	check(eng::field::WorldMapChunkLoader::load(&loader, 9, 9, eng::TileBankBuffer {tmp}) == eng::field::LoadResult::Empty,
	      "chunk ausente -> Empty");
	check(world.loads() >= 2, "el Loader contó cargas");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: WorldLayerSource + Loader-RAM (WorldView->scroll) validados.\n");
	return 0;
}
