// ============================================================================
// Test HOST-031: WorldView (chunk WorldMap sobre UAF-R)
// ============================================================================
//
// Valida `eng::assets::WorldView` (formato `docs/engine/architecture/WORLD_FORMAT.md`):
// cabecera, descriptores de capa, directorio de chunks ordenado, celdas, wrap/borde,
// chunks ausentes, metadatos y validación de bloques fuera de rango.

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
using eng::assets::WorldView;
using eng::assets::write_be16;
using eng::assets::write_be32;

// Layout del fixture (chunk_log2 = 2 -> chunks de 4x4, 16 celdas).
constexpr u32 kHdr = WorldView::kHeaderSize;        // 16
constexpr u32 kLd = WorldView::kLayerDescSize;      // 32
constexpr u32 kCe = WorldView::kChunkEntrySize;     // 4
constexpr u32 kDesc0 = kHdr;                        // 16
constexpr u32 kDesc1 = kHdr + kLd;                  // 48
constexpr u32 kDir0 = kHdr + 2u * kLd;              // 80
constexpr u32 kCells0 = kDir0 + 2u * kCe;           // 88
constexpr u32 kDir1 = kCells0 + 2u * 16u * 2u;      // 152
constexpr u32 kCells1 = kDir1 + 2u * kCe;           // 160
constexpr u32 kMeta1 = kCells1 + 2u * 16u * 2u;     // 224
constexpr u32 kTotal = kMeta1 + WorldView::kMetaEntrySize; // 236

void write_layer(eng::Span<u8> b, u32 off, u16 id, u16 width, u16 height,
                 u16 wrap_x, u16 wrap_y, u16 meta_count, u32 dir_off,
                 u32 dir_count, u32 cells_off, u32 meta_off) {
	write_be16(b.data() + off + 0u, id);
	write_be16(b.data() + off + 2u, 0u);          // kind = tiles
	write_be16(b.data() + off + 4u, width);
	write_be16(b.data() + off + 6u, height);
	write_be16(b.data() + off + 8u, wrap_x);
	write_be16(b.data() + off + 10u, wrap_y);
	write_be16(b.data() + off + 12u, 0xFFFFu);    // empty_tile
	write_be16(b.data() + off + 14u, meta_count);
	write_be32(b.data() + off + 16u, dir_off);
	write_be32(b.data() + off + 20u, dir_count);
	write_be32(b.data() + off + 24u, cells_off);
	write_be32(b.data() + off + 28u, meta_off);
}

void write_entry(eng::Span<u8> b, u32 off, u16 cx, u16 cy) {
	write_be16(b.data() + off, cx);
	write_be16(b.data() + off + 2u, cy);
}
} // namespace

int main() {
	eng::u8 buf[256] {};
	const eng::Span<u8> b {buf, sizeof(buf)};

	// Cabecera (16 B).
	write_be16(buf + 0u, 1u);   // version
	write_be16(buf + 2u, 0u);   // flags
	buf[4] = 2u;                // chunk_log2 -> 4x4
	buf[5] = 2u;                // layer_count
	write_be16(buf + 6u, 0u);   // tiles_chunk
	write_be16(buf + 8u, 0u);   // palette_chunk
	write_be32(buf + 10u, 0u);  // reservado
	write_be16(buf + 14u, 0u);  // reservado2

	// Capa 0: mundo 12x4 acotado; chunks (0,0) y (1,0); (2,0) ausente.
	write_layer(b, kDesc0, 0, 12, 4, 0, 0, 0, kDir0, 2, kCells0, 0);
	write_entry(b, kDir0 + 0u, 0, 0);
	write_entry(b, kDir0 + 4u, 1, 0);
	for (u32 i = 0; i < 16u; ++i) write_be16(buf + kCells0 + i * 2u, static_cast<u16>(1u + i));
	for (u32 i = 0; i < 16u; ++i) write_be16(buf + kCells0 + 32u + i * 2u, static_cast<u16>(17u + i));

	// Capa 1: mundo 8x4 CON wrap en X; chunks (0,0) y (1,0); 1 metadato.
	write_layer(b, kDesc1, 1, 8, 4, 8, 0, 1, kDir1, 2, kCells1, kMeta1);
	write_entry(b, kDir1 + 0u, 0, 0);
	write_entry(b, kDir1 + 4u, 1, 0);
	for (u32 i = 0; i < 16u; ++i) write_be16(buf + kCells1 + i * 2u, static_cast<u16>(101u + i));
	for (u32 i = 0; i < 16u; ++i) write_be16(buf + kCells1 + 32u + i * 2u, static_cast<u16>(201u + i));
	write_be16(buf + kMeta1 + 0u, 7u);
	write_be16(buf + kMeta1 + 2u, 1u);
	write_be16(buf + kMeta1 + 4u, 2u);
	write_be16(buf + kMeta1 + 6u, 3u);
	write_be16(buf + kMeta1 + 8u, 4u);
	write_be16(buf + kMeta1 + 10u, 5u);

	WorldView w {};
	check(w.read(eng::UafPayload(buf, kTotal)), "read valido");
	check(w.valid() && w.version() == 1u, "version");
	check(w.chunk_log2() == 2u && w.chunk_size() == 4u && w.chunk_cell_count() == 16u,
	      "geometria de chunk");
	check(w.layer_count() == 2u, "2 capas");

	// Capa 0: directorio (ordenado), celdas y borde.
	check(w.layer_width(0) == 12u && w.layer_wrap_x(0) == 0u, "desc capa 0");
	check(w.find_chunk(0, 0, 0) == 0 && w.find_chunk(0, 1, 0) == 1 && w.find_chunk(0, 2, 0) == -1,
	      "directorio capa 0");
	check(w.tile_at(0, 0, 0) == 1u, "celda (0,0)");
	check(w.tile_at(0, 3, 2) == 12u, "celda (3,2)");
	check(w.tile_at(0, 4, 1) == 21u, "cruce al chunk 1");
	check(w.tile_at(0, 8, 0) == 0xFFFFu, "chunk ausente -> empty");
	check(w.tile_at(0, 12, 0) == 0xFFFFu, "x fuera de width -> empty");
	check(w.tile_at(0, 0, 4) == 0xFFFFu, "y fuera de height -> empty");
	check(w.cell(0, 16u) == 17u, "cell() cruce de chunk");
	check(w.chunk_bytes(0, 0).size() == 32u && w.chunk_bytes(0, 2).size() == 0u,
	      "chunk_bytes (presente/ausente)");

	// Capa 1: wrap toroidal en X.
	check(w.layer_wrap_x(1) == 8u && w.layer_wrap_y(1) == 0u, "desc capa 1");
	check(w.tile_at(1, -1, 0) == w.tile_at(1, 7, 0), "wrap X: -1 == 7");
	check(w.tile_at(1, 8, 0) == w.tile_at(1, 0, 0), "wrap X: 8 == 0");
	check(w.tile_at(1, 0, 0) == 101u && w.tile_at(1, 4, 0) == 201u, "valores con wrap");

	// Metadatos.
	check(w.layer_meta_count(1) == 1u, "meta_count");
	const WorldView::Meta m = w.meta_entry(1, 0);
	check(m.type == 7u && m.x == 1u && m.y == 2u && m.a == 3u && m.b == 4u && m.c == 5u, "meta");
	check(w.meta_entry(1, 5).type == 0u, "meta fuera de rango");

	// Validacion: payload corto y version invalida.
	check(!WorldView {}.read(eng::UafPayload(buf, 10u)), "payload corto");
	eng::u8 bad[256] {};
	for (u32 i = 0; i < kTotal; ++i) bad[i] = buf[i];
	write_be16(bad + 0u, 2u); // version 2
	check(!WorldView {}.read(eng::UafPayload(bad, kTotal)), "version invalida");
	// cells_off fuera de rango en la capa 1.
	for (u32 i = 0; i < kTotal; ++i) bad[i] = buf[i];
	write_be32(bad + kDesc1 + 24u, 0xFFFFu);
	check(!WorldView {}.read(eng::UafPayload(bad, kTotal)), "cells_off fuera de rango");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: WorldView (chunks, wrap, ausentes, meta y validacion) validado.\n");
	return 0;
}
