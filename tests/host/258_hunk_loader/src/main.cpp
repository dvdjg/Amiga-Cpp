// ============================================================================
// Test HOST-258: cargador HUNK (eng/res/hunk.hpp) y deteccion de formato en
// eng/res/dynloader.hpp (`.englib` + HUNK).
// ============================================================================
//
// Monta a mano un ejecutable HUNK minimo (2 hunks: code + data) con una relocacion
// code->data y un simbolo, lo carga en una `LinearArena` y comprueba: segmentos, tamano,
// tipo, relocacion aplicada, simbolo por nombre y entry(). Ademas valida la variante
// RELOC32SHORT, la deteccion de formato del `DynLoader` y los errores de formato.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/258_hunk_loader

#include <cstdio>
#include <cstdint>

#include <eng/core/memory_kind.hpp>
#include <eng/memory/arena.hpp>
#include <eng/res/dynloader.hpp>
#include <eng/res/hunk.hpp>

using namespace eng;
using namespace eng::res;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Escritor secuencial big-endian sobre un buffer (para montar el HUNK a mano).
struct Writer {
	u8* p = nullptr;
	u32 n = 0u;
	void u32v(u32 v) {
		write_be32(p + n, v);
		n += 4u;
	}
	void u16v(u16 v) {
		write_be16(p + n, v);
		n += 2u;
	}
	void bytes(const char* s, u32 len) {
		for (u32 i = 0u; i < len; ++i) {
			p[n + i] = static_cast<u8>(s[i]);
		}
		n += len;
	}
};

/// HUNK minimo con RELOC32: hunk0 (code, 8 B) referencia hunk1 (data, 4 B) en el offset 0,
/// y exporta el simbolo "foo" (offset 0 de hunk0). Devuelve el tamano en bytes.
u32 build_hunk_reloc32(u8* blob) {
	Writer w {blob, 0u};
	w.u32v(kHunkHeaderMagic); // HUNK_HEADER
	w.u32v(0u);               // resident library list
	w.u32v(2u);               // num hunks
	w.u32v(0u);               // first
	w.u32v(1u);               // last
	w.u32v(2u);               // hunk0: 2 longs (8 B)
	w.u32v(1u);               // hunk1: 1 long (4 B)
	// hunk 0 (code)
	w.u32v(HunkCode);
	w.u32v(2u);
	w.u32v(0u); // celda relocable (offset hunk-relativo 0)
	w.u32v(0u);
	// reloc code -> hunk1
	w.u32v(HunkReloc32);
	w.u32v(1u); // count
	w.u32v(1u); // target hunk 1
	w.u32v(0u); // offset dentro de code
	w.u32v(0u); // terminador
	// simbolo local
	w.u32v(HunkSymbol);
	w.u32v(1u); // name_len_longs
	w.bytes("foo\0", 4u);
	w.u32v(0u); // value (offset dentro de hunk0)
	w.u32v(0u); // terminador de simbolos
	w.u32v(HunkEnd);
	// hunk 1 (data)
	w.u32v(HunkData);
	w.u32v(1u);
	w.u32v(0x11223344u); // dato inicializado
	w.u32v(HunkEnd);
	return w.n;
}

/// HUNK minimo con RELOC32SHORT (campos de 16 bits) y total de words IMPAR (fuerza el
/// relleno a longword): hunk0 (code, 4 B) referencia hunk1 (data, 4 B).
u32 build_hunk_reloc32short(u8* blob) {
	Writer w {blob, 0u};
	w.u32v(kHunkHeaderMagic);
	w.u32v(0u);
	w.u32v(2u);
	w.u32v(0u);
	w.u32v(1u);
	w.u32v(1u); // hunk0: 1 long (4 B)
	w.u32v(1u); // hunk1: 1 long (4 B)
	w.u32v(HunkCode);
	w.u32v(1u);
	w.u32v(0u); // celda relocable
	w.u32v(HunkReloc32Short);
	w.u16v(1u); // count
	w.u16v(1u); // target
	w.u16v(0u); // offset
	w.u16v(0u); // terminador (4 words -> par, sin padding)
	w.u32v(HunkEnd);
	w.u32v(HunkData);
	w.u32v(1u);
	w.u32v(0xAABBCCDDu);
	w.u32v(HunkEnd);
	return w.n;
}

void test_reloc32() {
	alignas(8) u8 blob[160] {};
	const u32 n = build_hunk_reloc32(blob);
	alignas(8) u8 pool_buf[256] {};
	LinearArena pool(pool_buf, sizeof(pool_buf), MemoryKind::Any);

	HunkImage img;
	check(img.load(Span<u8> {blob, n}, pool), "load HUNK RELOC32");
	check(img.hunk_count() == 2u, "2 hunks");
	check(img.hunk(0).type == static_cast<u16>(HunkCode), "hunk0 es CODE");
	check(img.hunk(1).type == static_cast<u16>(HunkData), "hunk1 es DATA");
	check(img.hunk(0).size == 8u, "hunk0 = 8 B");
	check(img.hunk(1).size == 4u, "hunk1 = 4 B");
	check(img.entry() == img.hunk(0).base, "entry = base del code");

	const u32 data_base = static_cast<u32>(reinterpret_cast<uintptr>(img.hunk(1).base));
	check(read_be32(img.hunk(0).base) == data_base, "reloc aplicada (celda = base de data)");
	check(read_be32(img.hunk(1).base) == 0x11223344u, "dato copiado intacto");

	check(img.symbol("foo") == img.hunk(0).base, "symbol foo = base del code");
	check(img.symbol("bar") == nullptr, "symbol inexistente = nullptr");
	check(img.symbol_count() == 1u, "1 simbolo indexado");

	// Exports propios: enumerables por hash + direccion (para ligar sin conocer los nombres).
	check(img.export_count() == 1u, "1 export enumerable");
	check(img.export_hash(0u) == symbol_hash("foo"), "export 0 = hash de foo");
	check(img.export_address(0u) == img.hunk(0).base, "export 0 = base del code");
	check(img.export_address(1u) == nullptr, "export fuera de rango = nullptr");
	check(img.export_hash(1u) == 0u, "hash fuera de rango = 0");
}

void test_reloc32short() {
	alignas(8) u8 blob[160] {};
	const u32 n = build_hunk_reloc32short(blob);
	alignas(8) u8 pool_buf[256] {};
	LinearArena pool(pool_buf, sizeof(pool_buf), MemoryKind::Any);

	HunkImage img;
	check(img.load(Span<u8> {blob, n}, pool), "load HUNK RELOC32SHORT");
	check(img.hunk_count() == 2u, "2 hunks (short)");
	const u32 data_base = static_cast<u32>(reinterpret_cast<uintptr>(img.hunk(1).base));
	check(read_be32(img.hunk(0).base) == data_base, "reloc short aplicada");
	check(read_be32(img.hunk(1).base) == 0xAABBCCDDu, "dato short copiado intacto");
}

void test_dynloader_detect() {
	alignas(8) u8 blob[160] {};
	const u32 n = build_hunk_reloc32(blob);
	alignas(8) u8 pool_buf[256] {};
	LinearArena pool(pool_buf, sizeof(pool_buf), MemoryKind::Any);

	DynLoader dl;
	const LibHandle h = dl.declare("mod.hunk");
	check(dl.load(h, Span<u8> {blob, n}, &pool), "DynLoader::load HUNK");
	check(dl.state(h) == LibState::Ready, "DynLoader Ready");
	check(dl.format(h) == LibFormat::Hunk, "formato detectado = Hunk");
	check(dl.symbol(h, "foo") != nullptr, "symbol foo via DynLoader");
	check(dl.symbol(h, "bar") == nullptr, "symbol inexistente via DynLoader");

	DynLoader dl2;
	const LibHandle h2 = dl2.declare("mod.hunk");
	check(!dl2.load(h2, Span<u8> {blob, n}), "HUNK sin pool -> falla");
	check(dl2.state(h2) == LibState::Error, "estado Error sin pool");
}

void test_bad() {
	alignas(8) u8 pool_buf[64] {};
	LinearArena pool(pool_buf, sizeof(pool_buf), MemoryKind::Any);

	u8 short_img[8] {};
	HunkImage i1;
	check(!i1.load(Span<u8> {short_img, 8u}, pool), "imagen corta -> falla");

	alignas(8) u8 no_magic[32] {};
	HunkImage i2;
	check(!i2.load(Span<u8> {no_magic, 32u}, pool), "magic invalido -> falla");
}

} // namespace

int main() {
	test_reloc32();
	test_reloc32short();
	test_dynloader_detect();
	test_bad();

	if (failures == 0) {
		std::printf("OK: HUNK (segmentos, relocaciones y simbolos) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
