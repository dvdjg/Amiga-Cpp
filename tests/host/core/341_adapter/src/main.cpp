// ============================================================================
// Test HOST-341: prueba de decision §7.6 — adaptador de consumidor sobre el engine.
// ============================================================================
//
// Un consumidor externo define SUS interfaces (I*) y las implementa con SOLO `<eng/api/api.hpp>`
// y los helpers generales (ChipPool, Copper, ...), sin tocar `field`/`BobTarget`/planos ni
// registros. Si esto compila y funciona, la frontera es correcta y **el engine no se ve afectado**
// (las I* son externas). Ver ROADMAP_API_COHERENCE.md §7.6.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/341_adapter

#include <cstdio>

#include <eng/api/api.hpp> // fachada base
#include <eng/api/copper.hpp> // helper opcional (opt-in, fuera del umbrella)
#include <eng/res/chip_pool.hpp> // helper opcional (opt-in, fuera del umbrella)

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// --- Interfaces del consumidor (externas, no del engine) ---
struct IChipMem {
	virtual ~IChipMem() = default;
	virtual void* alloc(eng::u32 bytes) = 0;
	virtual void free(void* p) = 0;
	virtual eng::u32 free_bytes() const = 0;
};
struct ICopper {
	virtual ~ICopper() = default;
	virtual void set_color(eng::u8 index, eng::u16 color) = 0;
	virtual eng::u16 words_used() const = 0;
};

// --- Adaptadores finos sobre el engine (solo api.hpp + helpers) ---
struct ChipMemAdapter final : IChipMem {
	eng::res::ChipPool pool;
	ChipMemAdapter(eng::u8* base, eng::u32 n) : pool(base, n, 16u) {}
	void* alloc(eng::u32 bytes) override { return pool.alloc(bytes); }
	void free(void* p) override { pool.free(p); }
	eng::u32 free_bytes() const override { return pool.free_bytes(); }
};

struct CopperAdapter final : ICopper {
	eng::copper::Scheduler sched;
	eng::Copper copper;
	explicit CopperAdapter(eng::Block<eng::CopperTag> blk) : sched(blk), copper(sched) {}
	void set_color(eng::u8 index, eng::u16 color) override { copper.set_color(index, color); }
	eng::u16 words_used() const override { return copper.words_used(); }
};

} // namespace

int main() {
	std::printf("== HOST-341 adapter ==\n");

	// El consumidor usa sus interfaces; por debajo, el engine.
	eng::u8 chip[1024] {};
	ChipMemAdapter mem {chip, sizeof(chip)};
	void* a = mem.alloc(64u);
	check(a != nullptr && mem.free_bytes() == 1024u - 64u, "IChipMem sobre ChipPool");
	mem.free(a);
	check(mem.free_bytes() == 1024u, "IChipMem free");

	eng::u16 buf[128] {};
	eng::Block<eng::CopperTag> blk {
		eng::Bytes<eng::CopperTag> {reinterpret_cast<eng::u8*>(buf), sizeof(buf)},
		eng::MemoryKind::Chip};
	CopperAdapter cop {blk};
	cop.set_color(1u, 0x0f00u);
	check(cop.words_used() > 0u, "ICopper sobre eng::Copper");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: adaptador de consumidor (I* externas) sobre api.hpp validado.\n");
	return 0;
}
