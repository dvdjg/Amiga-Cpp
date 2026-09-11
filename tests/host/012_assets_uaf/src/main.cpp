// Test host de eng::assets::Blob (contenedor UAF-R, big-endian).
// Construye un blob mínimo (header + chunks) y valida bind/find/data y errores.
#include <eng/assets/uaf.hpp>

#include <cstdio>
#include <cstring>

using namespace eng::assets;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

static void w16(eng::u8* b, eng::u32 o, eng::u16 v) {
	b[o] = static_cast<eng::u8>(v >> 8);
	b[o + 1] = static_cast<eng::u8>(v);
}
static void w32(eng::u8* b, eng::u32 o, eng::u32 v) {
	b[o] = static_cast<eng::u8>(v >> 24);
	b[o + 1] = static_cast<eng::u8>(v >> 16);
	b[o + 2] = static_cast<eng::u8>(v >> 8);
	b[o + 3] = static_cast<eng::u8>(v);
}

int main() {
	eng::u8 buf[64];
	std::memset(buf, 0, sizeof(buf));

	// header
	w32(buf, 0, kUafMagic);
	w16(buf, 4, kUafVersion);
	w16(buf, 6, 2);

	// chunk 0: Palette, count=2, size=4, datos AABBCCDD
	eng::u32 off = 8;
	w16(buf, off, static_cast<eng::u16>(ChunkType::Palette));
	w16(buf, off + 2, 2);
	w32(buf, off + 4, 4);
	buf[off + 8] = 0xAA; buf[off + 9] = 0xBB; buf[off + 10] = 0xCC; buf[off + 11] = 0xDD;
	off += kChunkHeaderSize + 4; // 20

	// chunk 1: Samples, count=3, size=4, datos 11223344
	w16(buf, off, static_cast<eng::u16>(ChunkType::Samples));
	w16(buf, off + 2, 3);
	w32(buf, off + 4, 4);
	buf[off + 8] = 0x11; buf[off + 9] = 0x22; buf[off + 10] = 0x33; buf[off + 11] = 0x44;
	off += kChunkHeaderSize + 4; // 32

	Blob b;
	check(b.bind({buf, off}), "bind de blob valido");
	check(b.ok() && b.chunk_count() == 2, "2 chunks");
	check(b.chunk(0).type == ChunkType::Palette, "chunk 0 tipo");
	check(b.blob().size() == off, "vista del blob");

	const ChunkRef* pal = b.find(ChunkType::Palette);
	check(pal != nullptr && pal->count == 2 && pal->size == 4, "find Palette");

	const eng::Span<const eng::u8> d = b.data(0);
	check(d.size() == 4 && d[0] == 0xAA && d[3] == 0xDD, "data chunk 0");

	const ChunkRef* snd = b.find(ChunkType::Samples);
	check(snd != nullptr && snd->count == 3, "find Samples");
	check(b.find(ChunkType::Modules) == nullptr, "find inexistente -> null");

	// Errores.
	{
		eng::u8 bad[64];
		std::memcpy(bad, buf, sizeof(buf));
		bad[0] = 0;
		Blob bb;
		check(!bb.bind({bad, off}), "magic invalido -> false");
	}
	{
		eng::u8 bad[64];
		std::memcpy(bad, buf, sizeof(buf));
		w32(bad, 12, 1000); // tamaño del chunk 0 mayor que el blob
		Blob bb;
		check(!bb.bind({bad, off}), "chunk fuera de rango -> false");
	}
	{
		Blob bb;
		check(!bb.bind({buf, 4}), "blob demasiado corto -> false");
	}

	// BlobWriter -> Blob -> vistas tipadas (round-trip).
	{
		eng::u8 store[128];
		BlobWriter w {eng::Span<eng::u8>(store, sizeof(store))};
		check(w.begin(), "writer begin");

		const eng::u8 pal[4] = {0x0F, 0x00, 0x00, 0xF0}; // 2 colores RGB444
		check(w.add_chunk(ChunkType::Palette, 2, eng::Span<const eng::u8>(pal, 4)), "writer add palette");

		const eng::u8 snd[3] = {0x80, 0x40, 0xC0};
		check(w.add_chunk(ChunkType::Samples, 3, eng::Span<const eng::u8>(snd, 3)), "writer add sample");

		const eng::Span<const eng::u8> out = w.finish();
		check(w.ok(), "writer ok");

		Blob bb;
		check(bb.bind(out), "writer -> bind");
		check(bb.chunk_count() == 2, "writer 2 chunks");

		const PaletteView pv {bb.data(0)};
		check(pv.valid() && pv.count() == 2, "PaletteView count");
		check(pv.color(0) == 0x0F00 && pv.color(1) == 0x00F0, "PaletteView colores");

		const ChunkRef* sr = bb.find(ChunkType::Samples);
		check(sr != nullptr, "writer find Samples");
		const SampleView sv {bb.data(1)};
		check(sv.size() == 3 && !sv.empty(), "SampleView size");
		check(sv.bytes()[0] == 0x80 && sv.bytes()[2] == 0xC0, "SampleView datos");
	}

	// Reader tipado + BitplanesView (cabecera + datos planares).
	{
		eng::u8 bp[14];
		write_be16(bp, 16);
		write_be16(bp + 2, 2);
		write_be16(bp + 4, 2);
		bp[6] = 1; bp[7] = 0; bp[8] = 0; bp[9] = 0; // planes, layout, flags, resv
		bp[10] = 0xAA; bp[11] = 0x55; bp[12] = 0x11; bp[13] = 0x22;

		eng::u8 store[128];
		BlobWriter w {eng::Span<eng::u8>(store, sizeof(store))};
		w.begin();
		check(w.add_chunk(ChunkType::Bitplanes, 1, eng::Span<const eng::u8>(bp, 14)), "writer add bitplanes");
		Blob bb;
		check(bb.bind(w.finish()), "bitplanes bind");

		BitplanesView bv;
		check(bv.read(bb.data(0)), "BitplanesView read");
		check(bv.width() == 16 && bv.height() == 2 && bv.row_bytes() == 2 && bv.planes() == 1,
			"BitplanesView geometria");
		check(bv.data().size() == 4 && bv.data()[0] == 0xAA, "BitplanesView datos");
	}

	// Reader: comprobacion de limites.
	{
		const eng::u8 d[2] = {0x12, 0x34};
		Reader r {eng::Span<const eng::u8>(d, 2)};
		check(r.read_u16() == 0x1234, "Reader u16 big-endian");
		check(r.remaining() == 0 && r.ok(), "Reader agotado sigue ok");
		check(r.read_u8() == 0 && !r.ok(), "Reader underflow -> !ok");
	}

	// StringsView: chunk de textos NUL-separados.
	{
		const char txt[] = "hola\0mundo\0\0"; // 3 cadenas (la ultima vacia)
		StringsView sv {eng::Span<const eng::u8>(reinterpret_cast<const eng::u8*>(txt), sizeof(txt) - 1)};
		check(sv.count() == 3, "StringsView count");
		check(sv.string(0) != nullptr && std::strcmp(sv.string(0), "hola") == 0, "StringsView[0]");
		check(sv.string(1) != nullptr && std::strcmp(sv.string(1), "mundo") == 0, "StringsView[1]");
		check(sv.string(2) != nullptr && sv.string(2)[0] == '\0', "StringsView[2] vacia");
		check(sv.string(3) == nullptr, "StringsView fuera de rango -> null");
	}

	// TilesView: banco de tiles de tamano fijo.
	{
		const eng::u8 tiles[6] = {1, 2, 3, 4, 5, 6};
		TilesView tv {eng::Span<const eng::u8>(tiles, 6), 2};
		check(tv.count() == 3, "TilesView count");
		check(tv.tile(1).size() == 2 && tv.tile(1)[0] == 3 && tv.tile(1)[1] == 4, "TilesView tile 1");
		check(tv.tile(3).empty(), "TilesView fuera de rango -> vacio");
		TilesView none {eng::Span<const eng::u8>(tiles, 6), 0};
		check(none.count() == 0, "TilesView tile_bytes=0 -> count 0");
	}

	// SpritesView: cabecera + sprites de tamano fijo.
	{
		eng::u8 sprites[2 + 2 * 2 * 2]; // words_per_sprite=2, 2 sprites
		write_be16(sprites, 2);
		write_be16(sprites + 2, 0x1234); write_be16(sprites + 4, 0x5678);
		write_be16(sprites + 6, 0x9abc); write_be16(sprites + 8, 0xdef0);
		const SpritesView sv {eng::Span<const eng::u8>(sprites, sizeof(sprites))};
		check(sv.words_per_sprite() == 2 && sv.count() == 2, "SpritesView count");
		check(sv.word(0, 0) == 0x1234 && sv.word(1, 0) == 0x9abc && sv.word(1, 1) == 0xdef0, "SpritesView datos");
		check(sv.word(2, 0) == 0 && sv.word(0, 5) == 0, "SpritesView fuera de rango -> 0");
	}

	// CopperView: palabras big-endian (WAIT/MOVE).
	{
		eng::u8 cop[8];
		write_be16(cop, 0x2c81); write_be16(cop + 2, 0x2cc1);
		write_be16(cop + 4, 0xffff); write_be16(cop + 6, 0xfffe);
		const CopperView cv {eng::Span<const eng::u8>(cop, 8)};
		check(cv.count() == 4 && cv.word(0) == 0x2c81 && cv.word(3) == 0xfffe, "CopperView");
		check(cv.word(4) == 0, "CopperView fuera de rango -> 0");
	}

	// MeshAssetView: cabecera + vertices + caras (formato obj2c).
	{
		eng::u8 mesh[4 + 2 * 6 + 6];
		write_be16(mesh, 2); write_be16(mesh + 2, 1); // 2 vertices, 1 cara
		write_be16(mesh + 4, static_cast<eng::u16>(-48));
		write_be16(mesh + 6, static_cast<eng::u16>(-48));
		write_be16(mesh + 8, static_cast<eng::u16>(-48));
		write_be16(mesh + 10, 48);
		write_be16(mesh + 12, static_cast<eng::u16>(-48));
		write_be16(mesh + 14, static_cast<eng::u16>(-48));
		write_be16(mesh + 16, 0); write_be16(mesh + 18, 1); write_be16(mesh + 20, 1);
		const MeshAssetView mv {eng::Span<const eng::u8>(mesh, sizeof(mesh))};
		check(mv.valid() && mv.vertex_count() == 2 && mv.face_count() == 1, "MeshAssetView cabecera");
		const eng::math3d::Vec3 v0 = mv.vertex(0);
		check(v0.x == -48 && v0.y == -48 && v0.z == -48, "MeshAssetView vertex 0");
		const eng::math3d::Face f0 = mv.face(0);
		check(f0.a == 0 && f0.b == 1 && f0.c == 1, "MeshAssetView face 0");
		const MeshAssetView bad {eng::Span<const eng::u8>(mesh, 8)};
		check(!bad.valid(), "MeshAssetView truncado -> !valid");
	}

	if (failures == 0) {
		std::printf("OK: contenedor UAF-R validado (bind/find/data + errores).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
