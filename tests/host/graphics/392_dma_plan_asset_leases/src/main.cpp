// HOST-392: las leases Chip sobreviven mientras FramePlan o copper::Plan puedan usarlas.

#include <cstdio>

#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/res/asset_cache.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

unsigned g_fail = 0u;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct FakeAssetBackend {
	eng::u8 bytes[256] {};
	eng::u32 used = 0u;

	eng::MemoryBlock alloc(eng::u32 size, eng::MemoryKind kind) {
		if (kind != eng::MemoryKind::Chip || size > sizeof(bytes) - used) return {};
		eng::MemoryBlock block {bytes + used, size, kind};
		used += size;
		return block;
	}
	void free(const eng::MemoryBlock&) {}
	bool load(eng::res::AssetId, const char*, eng::Span<eng::u8>, eng::u8) { return true; }
};

} // namespace

int main() {
	std::printf("== HOST-392 DMA plan asset leases ==\n");

	alignas(16) eng::u8 copper_memory[16u * 1024u] {};
	eng::MemoryManager memory {};
	check(memory.configure(copper_memory, sizeof(copper_memory), nullptr, 0u, nullptr, 0u),
	      "configura memoria Chip para copperlist");

	FakeAssetBackend backend {};
	eng::res::AssetCache<FakeAssetBackend, 2u> cache {};
	eng::res::CacheConfig cfg {};
	cfg.chip_budget = 128u;
	cfg.max_assets = 2u;
	check(cache.init(backend, cfg), "inicializa caché");

	const eng::res::AssetId frame_id = cache.declare("frame", 64u, eng::res::MemoryRequest::Chip);
	const eng::res::AssetId copper_id = cache.declare("copper", 64u, eng::res::MemoryRequest::Chip);
	check(cache.prefetch(frame_id) && cache.prefetch(copper_id), "arrancan ambas cargas Chip");
	cache.on_load_done(frame_id, 64);
	cache.on_load_done(copper_id, 64);

	eng::graphics::FramePlan frame_plan {};
	check(frame_plan.retain_dma_asset(cache.lease_dma(cache.view(frame_id).handle)),
	      "FramePlan acepta la lease de su asset");
	for (eng::u8 frame = 0u; frame < 3u; ++frame) frame_plan.clear();
	check(frame_plan.dma_asset_count() == 1u && !cache.shutdown(),
	      "clear entre frames no libera el asset retenido");

	eng::copper::Plan copper_plan {};
	check(copper_plan.begin(memory, eng::copper::PlanConfig {4096u, 0u}),
	      "inicializa copper::Plan");
	check(copper_plan.retain_dma_asset(cache.lease_dma(cache.view(copper_id).handle)),
	      "copper::Plan acepta la lease de su asset");
	for (eng::u8 frame = 0u; frame < 3u; ++frame) copper_plan.begin_frame();
	check(copper_plan.dma_asset_count() == 1u && !cache.shutdown(),
	      "begin_frame conserva la lease del Copper publicada");

	frame_plan.release_dma_assets();
	check(frame_plan.dma_asset_count() == 0u && !cache.shutdown(),
	      "liberar FramePlan no suelta la lease independiente del Copper");
	copper_plan.release_dma_assets();
	check(copper_plan.dma_asset_count() == 0u && cache.shutdown(),
	      "teardown de ambos planes permite cerrar la caché");
	copper_plan.release();
	check(memory.chip().used_bytes() == 0u, "teardown libera los bloques de copperlist");

	if (g_fail != 0u) {
		std::printf("FALLOS: %u\n", g_fail);
		return 1;
	}
	std::printf("OK: FramePlan y copper::Plan retienen assets Chip hasta el teardown.\n");
	return 0;
}
