// ============================================================================
// Test HOST-401: HUNK con **banco por segmento** (R6.3 — MemoryManager)
// ============================================================================
//
// Monta un HUNK con un hunk CODE sin flag (Any) y uno DATA con `HUNKF_CHIP`, lo carga en un
// `MemoryManager` y comprueba que cada segmento cae en el banco que le toca (Any → Fast si hay,
// si no Slow; Chip → Chip), que `owns_memory()` es true y que `unload(mm)` libera todo.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/res/401_hunk_banks

#include <cstdio>
#include <cstdint>

#include <eng/core/data/byte_order.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/memory/memory_manager.hpp>
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

struct Writer {
	u8* p = nullptr;
	u32 n = 0u;
	void u32v(u32 v) {
		write_be32(p + n, v);
		n += 4u;
	}
};

/// HUNK de 2 hunks: hunk0 CODE sin flag, hunk1 DATA con `HUNKF_CHIP`.
u32 build_hunk_banks(u8* blob) {
	Writer w {blob, 0u};
	w.u32v(kHunkHeaderMagic);
	w.u32v(0u);
	w.u32v(2u);
	w.u32v(0u);
	w.u32v(1u);
	w.u32v(1u);                // hunk0: 1 long, sin flag (Any)
	w.u32v(1u | kHunkFChip);   // hunk1: 1 long, HUNKF_CHIP
	w.u32v(HunkCode);
	w.u32v(1u);
	w.u32v(0x11111111u);
	w.u32v(HunkEnd);
	w.u32v(HunkData);
	w.u32v(1u);
	w.u32v(0x22222222u);
	w.u32v(HunkEnd);
	return w.n;
}

[[nodiscard]] bool in_buf(const eng::u8* p, const eng::u8* buf, eng::usize size) noexcept {
	const auto a = reinterpret_cast<uintptr_t>(p);
	const auto b = reinterpret_cast<uintptr_t>(buf);
	return a >= b && a < b + size;
}

void test_banks_with_fast() {
	alignas(16) u8 chip_buf[256] {};
	alignas(16) u8 slow_buf[256] {};
	alignas(16) u8 fast_buf[256] {};
	MemoryManager mm {};
	check(mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), fast_buf,
			   sizeof(fast_buf), 16u),
	      "configure con Chip/Slow/Fast");

	alignas(8) u8 blob[160] {};
	const u32 n = build_hunk_banks(blob);
	HunkImage img;
	check(img.load(Span<u8> {blob, n}, mm), "load HUNK en bancos");
	check(img.owns_memory(), "owns_memory");
	check(img.hunk(0).mem == HunkMem::Any, "hunk0 pide Any");
	check(img.hunk(1).mem == HunkMem::Chip, "hunk1 pide Chip");
	check(in_buf(img.hunk(0).base, fast_buf, sizeof(fast_buf)), "Any → Fast (hay Fast)");
	check(in_buf(img.hunk(1).base, chip_buf, sizeof(chip_buf)), "Chip → Chip");
	check(read_be32(img.hunk(0).base) == 0x11111111u, "hunk0 copiado");
	check(read_be32(img.hunk(1).base) == 0x22222222u, "hunk1 copiado");

	img.unload(mm);
	check(mm.chip().free_bytes() == mm.chip().capacity(), "unload libera Chip");
	check(mm.fast().free_bytes() == mm.fast().capacity(), "unload libera Fast");
}

void test_banks_no_fast() {
	alignas(16) u8 chip_buf[256] {};
	alignas(16) u8 slow_buf[256] {};
	MemoryManager mm {};
	check(mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), nullptr, 0u, 16u),
	      "configure sin Fast");

	alignas(8) u8 blob[160] {};
	const u32 n = build_hunk_banks(blob);
	HunkImage img;
	check(img.load(Span<u8> {blob, n}, mm), "load sin Fast");
	check(in_buf(img.hunk(0).base, slow_buf, sizeof(slow_buf)), "Any sin Fast → Slow");
	check(in_buf(img.hunk(1).base, chip_buf, sizeof(chip_buf)), "Chip → Chip");
	img.unload(mm);
	check(mm.chip().free_bytes() == mm.chip().capacity(), "unload libera Chip (sin Fast)");
	check(mm.slow().free_bytes() == mm.slow().capacity(), "unload libera Slow");
}

} // namespace

int main() {
	test_banks_with_fast();
	test_banks_no_fast();
	if (failures == 0) {
		std::printf("OK: HUNK con banco por segmento (Chip/Fast/Slow) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
