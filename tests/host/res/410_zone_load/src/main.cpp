// ============================================================================
// Test HOST-410: cadena de "zona" — Vfs → .engz → HUNK (R6.7, versión host)
// ============================================================================
//
// Integra las piezas de R6: un HUNK con símbolo se envuelve en `.engz`, se sirve por una `Vfs`
// (backend simulado), se lee completo, se decodifica y se carga (segmentos + símbolos). Es el
// "cargar un overlay desde disco" end-to-end, sin emulador.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/res/410_zone_load

#include <cstdio>
#include <cstdint>

#include <eng/core/data/byte_order.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/memory/arena.hpp>
#include <eng/os/vfs.hpp>
#include <eng/res/engz.hpp>
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

bool eq(const char* a, const char* b) {
	for (eng::usize i = 0u;; ++i) {
		if (a[i] != b[i]) return false;
		if (a[i] == '\0') return true;
	}
}

/// Backend VFS en memoria (un fichero).
struct OneFile {
	const char* path = nullptr;
	const eng::u8* data = nullptr;
	eng::u32 len = 0u;
	bool exists(const char* p) const { return eq(path, p); }
	eng::u32 size(const char* p) const { return eq(path, p) ? len : 0u; }
	eng::s32 read(const char* p, eng::Span<eng::u8> dst, eng::u32 off) const {
		if (!eq(path, p) || off > len) return -1;
		eng::u32 k = len - off;
		if (k > dst.size()) k = static_cast<eng::u32>(dst.size());
		for (eng::u32 i = 0u; i < k; ++i) dst[i] = data[off + i];
		return static_cast<eng::s32>(k);
	}
};

struct Writer {
	eng::u8* p = nullptr;
	eng::u32 n = 0u;
	void u32v(eng::u32 v) {
		write_be32(p + n, v);
		n += 4u;
	}
};

/// HUNK mínimo con un hunk CODE (4 B) y símbolo "hero" en offset 0.
eng::u32 build_hunk(eng::u8* blob) {
	Writer w {blob, 0u};
	w.u32v(kHunkHeaderMagic);
	w.u32v(0u);
	w.u32v(1u);
	w.u32v(0u);
	w.u32v(0u);
	w.u32v(1u); // hunk0: 1 long
	w.u32v(HunkCode);
	w.u32v(1u);
	w.u32v(0xCAFEBABEu);
	w.u32v(HunkSymbol);
	w.u32v(1u);
	const char name[4] = {'h', 'e', 'r', 'o'};
	for (eng::u32 i = 0u; i < 4u; ++i) blob[w.n++] = static_cast<eng::u8>(name[i]);
	w.u32v(0u); // value
	w.u32v(0u); // terminador de símbolos
	w.u32v(HunkEnd);
	return w.n;
}

void test_zone_chain() {
	// 1) HUNK → 2) envuelto en `.engz` (Raw) → 3) servido por la Vfs.
	alignas(8) eng::u8 hunk[128] {};
	const eng::u32 hunk_size = build_hunk(hunk);
	alignas(8) eng::u8 engz[256] {};
	const eng::usize engz_size = build_engz(eng::Span<eng::u8> {engz, sizeof(engz)},
						eng::Span<const eng::u8> {hunk, hunk_size},
						Codec::Raw, hunk_size);
	check(engz_size > kEngzHeaderSize, "engz construido");

	OneFile fs {"/code/overlay.engz", engz, static_cast<eng::u32>(engz_size)};
	eng::os::Vfs<OneFile> vfs {fs};

	// Leer completo por la Vfs (path normalizado).
	alignas(8) eng::u8 loaded[256] {};
	const auto got = vfs.read_all("/code/../code/overlay.engz",
				      eng::Span<eng::u8> {loaded, sizeof(loaded)});
	check(got.has_value() && *got == engz_size, "Vfs.read_all del .engz");
	check(!vfs.exists("/code/missing.engz"), "fichero inexistente");

	// Decodificar el `.engz` → bytes del HUNK.
	alignas(8) eng::u8 decoded[128] {};
	const auto dn = decode_engz(eng::Span<const eng::u8> {loaded, *got},
				    eng::Span<eng::u8> {decoded, sizeof(decoded)});
	check(dn.has_value() && *dn == hunk_size, "decode_engz → HUNK");

	// Cargar el HUNK (segmentos + símbolos).
	alignas(16) eng::u8 pool_buf[256] {};
	LinearArena pool {pool_buf, sizeof(pool_buf), MemoryKind::Any};
	HunkImage img;
	check(img.load(eng::Span<eng::u8> {decoded, hunk_size}, pool), "HunkImage::load");
	check(img.hunk_count() == 1u && img.hunk(0).type == static_cast<eng::u16>(HunkCode),
	      "1 hunk CODE");
	check(img.symbol("hero") == img.hunk(0).base, "símbolo hero resuelto");
	check(read_be32(img.hunk(0).base) == 0xCAFEBABEu, "dato del hunk copiado");
}

} // namespace

int main() {
	test_zone_chain();
	if (failures == 0) {
		std::printf("OK: cadena de zona (Vfs → .engz → HUNK) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
