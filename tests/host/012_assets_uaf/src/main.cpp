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

	if (failures == 0) {
		std::printf("OK: contenedor UAF-R validado (bind/find/data + errores).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
