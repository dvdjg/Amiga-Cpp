// ============================================================================
// Test HOST-037: UAF-R -> WorldView (chunk WorldMap dentro del contenedor)
// ============================================================================
//
// Valida la cadena completa del formato de mundo: se ensambla un blob UAF-R con
// un chunk `WorldMap` (usando el `BlobWriter` del runtime), se valida con `Blob`
// y se lee con `WorldView`. Cierra "world.bin -> world.uafr -> runtime".

#include <cstdio>

#include <eng/assets/uaf.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::u8;
using eng::u16;
using eng::u32;
using eng::assets::Blob;
using eng::assets::BlobWriter;
using eng::assets::ChunkType;
using eng::assets::WorldView;
using eng::assets::write_be16;
using eng::assets::write_be32;

// Payload WorldMap minimo: 1 capa, 1 chunk 16x16 con celdas 1..256.
constexpr u32 kHdr = WorldView::kHeaderSize;
constexpr u32 kLd = WorldView::kLayerDescSize;
constexpr u32 kCe = WorldView::kChunkEntrySize;
constexpr u32 kDir = kHdr + kLd;
constexpr u32 kCells = kDir + kCe;
constexpr u32 kWorld = kCells + 256u * 2u;

void build_world(u8* b) {
	for (u32 i = 0; i < kWorld; ++i) b[i] = 0;
	write_be16(b + 0u, 1u);  // version
	b[4] = 4u;               // chunk_log2 -> 16x16
	b[5] = 1u;               // layer_count
	write_be16(b + kHdr + 4u, 16u);      // width
	write_be16(b + kHdr + 6u, 16u);      // height
	write_be16(b + kHdr + 12u, 0xFFFFu); // empty_tile
	write_be32(b + kHdr + 16u, kDir);
	write_be32(b + kHdr + 20u, 1u);      // dir_count
	write_be32(b + kHdr + 24u, kCells);
	// entry (0,0)
	write_be16(b + kDir + 0u, 0u);
	write_be16(b + kDir + 2u, 0u);
	for (u32 i = 0; i < 256u; ++i) write_be16(b + kCells + i * 2u, static_cast<u16>(1u + i));
}
} // namespace

int main() {
	u8 world[kWorld] {};
	build_world(world);

	// Ensambla un blob UAF-R con el chunk WorldMap.
	u8 blob_buf[1024] {};
	BlobWriter writer {eng::Span<u8>(blob_buf, sizeof(blob_buf))};
	check(writer.begin(), "BlobWriter::begin");
	check(writer.add_chunk(ChunkType::WorldMap, 1u, eng::Span<const u8>(world, kWorld)),
	      "add_chunk WorldMap");
	const eng::Span<const u8> out = writer.finish();
	check(out.size() == writer.size() && out.size() > kWorld, "finish");

	// Valida el contenedor y localiza el chunk.
	Blob blob {};
	check(blob.bind(out) && blob.ok(), "Blob::bind");
	u32 idx = 0xffffffffu;
	for (u32 i = 0; i < blob.chunk_count(); ++i) {
		if (blob.chunk(i).type == ChunkType::WorldMap) idx = i;
	}
	check(idx != 0xffffffffu, "chunk WorldMap presente");
	check(blob.find(ChunkType::WorldMap) != nullptr, "Blob::find WorldMap");

	// Lee el mundo desde el chunk.
	const eng::Span<const u8> data = blob.data(idx);
	check(data.size() == kWorld, "tamano del payload");
	WorldView world_view {};
	check(world_view.read(data) && world_view.valid(), "WorldView::read desde UAF-R");
	check(world_view.layer_count() == 1u && world_view.layer_width(0) == 16u, "capa");
	check(world_view.tile_at(0u, 0, 0) == 1u && world_view.tile_at(0u, 15, 15) == 256u, "celdas");
	check(world_view.tile_at(0u, 16, 0) == 0xFFFFu, "fuera de mundo -> empty");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: UAF-R -> WorldView (chunk WorldMap) validado.\n");
	return 0;
}
