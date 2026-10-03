// ============================================================================
// Test HOST-402: DynLoader propietario (R6.6) — load(..., MemoryManager&) + unload(h, mem)
// ============================================================================
//
// Carga un `.englib` (copiado a un bloque) y un HUNK (segmentos por banco) **poseyendo** la memoria
// en un `MemoryManager`, comprueba Ready/símbolos/formato y que `unload(h, mem)` restaura los bancos.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/res/402_dynloader_owner

#include <cstdio>
#include <cstdint>

#include <eng/core/data/byte_order.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/res/dynloader.hpp>

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

[[nodiscard]] bool in_buf(const void* p, const u8* buf, usize size) noexcept {
	const auto a = reinterpret_cast<uintptr_t>(p);
	const auto b = reinterpret_cast<uintptr_t>(buf);
	return a >= b && a < b + size;
}

/// `.englib` mínimo: header + code(8) + 1 reloc + 1 export ("foo" → offset 0).
usize build_lib(u8* blob) {
	auto* hdr = reinterpret_cast<EngLibHeader*>(blob);
	hdr->magic = kEngLibMagic;
	hdr->version = 1u;
	hdr->code_size = 8u;
	hdr->data_size = 0u;
	hdr->bss_size = 0u;
	hdr->entry_offset = 0u;
	hdr->reloc_count = 1u;
	hdr->export_count = 1u;
	u8* const code = blob + sizeof(EngLibHeader);
	*reinterpret_cast<u32*>(code) = 0u;
	auto* relocs = reinterpret_cast<u32*>(code + 8u);
	relocs[0] = 0u;
	auto* ex = reinterpret_cast<LibExport*>(relocs + 1u);
	ex[0] = LibExport {DynLoader::hash_name("foo"), 0u};
	return sizeof(EngLibHeader) + 8u + 4u + sizeof(LibExport);
}

struct Writer {
	u8* p = nullptr;
	u32 n = 0u;
	void u32v(u32 v) {
		write_be32(p + n, v);
		n += 4u;
	}
};

/// HUNK de 2 hunks: hunk0 CODE (Any) + hunk1 DATA (Chip), con símbolo "foo" en hunk0.
u32 build_hunk(u8* blob) {
	Writer w {blob, 0u};
	w.u32v(kHunkHeaderMagic);
	w.u32v(0u);
	w.u32v(2u);
	w.u32v(0u);
	w.u32v(1u);
	w.u32v(2u);
	w.u32v(1u | kHunkFChip);
	w.u32v(HunkCode);
	w.u32v(2u);
	w.u32v(0u);
	w.u32v(0u);
	w.u32v(HunkSymbol);
	w.u32v(1u);
	const char name[4] = {'f', 'o', 'o', '\0'};
	for (u32 i = 0u; i < 4u; ++i) {
		blob[w.n++] = static_cast<u8>(name[i]);
	}
	w.u32v(0u);
	w.u32v(0u);
	w.u32v(HunkEnd);
	w.u32v(HunkData);
	w.u32v(1u);
	w.u32v(0x22222222u);
	w.u32v(HunkEnd);
	return w.n;
}

void test_englib_owner() {
	alignas(16) u8 chip_buf[256] {};
	alignas(16) u8 slow_buf[256] {};
	alignas(16) u8 fast_buf[256] {};
	MemoryManager mm {};
	(void)mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), fast_buf,
			   sizeof(fast_buf), 16u);

	alignas(8) u8 blob[64] {};
	const usize n = build_lib(blob);
	DynLoader dl;
	const LibHandle h = dl.declare("foo.englib");
	check(dl.load(h, Span<u8> {blob, n}, mm), ".englib load(mem)");
	check(dl.state(h) == LibState::Ready, ".englib Ready");
	check(dl.format(h) == LibFormat::EngLib, ".englib formato");
	void* const foo = dl.symbol(h, "foo");
	check(foo != nullptr, ".englib symbol foo");
	check(in_buf(foo, fast_buf, sizeof(fast_buf)) || in_buf(foo, slow_buf, sizeof(slow_buf)),
	      ".englib code en banco CPU (no en el blob del llamador)");

	dl.unload(h, mm);
	check(dl.state(h) == LibState::Empty, ".englib unload → Empty");
	check(mm.fast().free_bytes() == mm.fast().capacity(), ".englib unload libera Fast");
	check(mm.slow().free_bytes() == mm.slow().capacity(), ".englib unload libera Slow");
}

void test_hunk_owner() {
	alignas(16) u8 chip_buf[256] {};
	alignas(16) u8 slow_buf[256] {};
	alignas(16) u8 fast_buf[256] {};
	MemoryManager mm {};
	(void)mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), fast_buf,
			   sizeof(fast_buf), 16u);

	alignas(8) u8 blob[160] {};
	const u32 n = build_hunk(blob);
	DynLoader dl;
	const LibHandle h = dl.declare("foo.hunk");
	check(dl.load(h, Span<u8> {blob, n}, mm), "HUNK load(mem)");
	check(dl.state(h) == LibState::Ready, "HUNK Ready");
	check(dl.format(h) == LibFormat::Hunk, "HUNK formato");
	void* const foo = dl.symbol(h, "foo");
	check(foo != nullptr, "HUNK symbol foo");
	check(in_buf(foo, fast_buf, sizeof(fast_buf)), "HUNK code (Any) → Fast");

	dl.unload(h, mm);
	check(dl.state(h) == LibState::Empty, "HUNK unload → Empty");
	check(mm.chip().free_bytes() == mm.chip().capacity(), "HUNK unload libera Chip");
	check(mm.fast().free_bytes() == mm.fast().capacity(), "HUNK unload libera Fast");
}

void test_out_of_memory() {
	alignas(16) u8 tiny[8] {}; // banco enano: no cabe
	MemoryManager mm {};
	(void)mm.configure(tiny, sizeof(tiny), nullptr, 0u, nullptr, 0u, 4u);

	alignas(8) u8 blob[64] {};
	const usize n = build_lib(blob);
	DynLoader dl;
	const LibHandle h = dl.declare("oom.englib");
	check(!dl.load(h, Span<u8> {blob, n}, mm), "sin memoria → falla");
	check(dl.state(h) == LibState::Error, "sin memoria → Error");
}

} // namespace

int main() {
	test_englib_owner();
	test_hunk_owner();
	test_out_of_memory();
	if (failures == 0) {
		std::printf("OK: DynLoader propietario (.englib + HUNK con bancos) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
