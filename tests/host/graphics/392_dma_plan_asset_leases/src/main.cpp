// Test HOST-392: retención Chip desde FramePlan y copper::Plan durante vida de escena/lista.
#define ENG_SCALAR_RETRO16
#include <cstdio>

#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/res/asset_cache.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* message) {
	if (!ok) {
		std::printf("[FAIL] %s\n", message);
		++failures;
	}
}

struct Backend {
	eng::u8 data[128] {};
	eng::MemoryBlock alloc(eng::u32 size, eng::MemoryKind kind) noexcept {
		return size <= sizeof(data) ? eng::MemoryBlock {data, size, kind} : eng::MemoryBlock {};
	}
	void free(const eng::MemoryBlock&) noexcept {}
	bool load(eng::res::AssetId, const char*, eng::Span<eng::u8>) noexcept { return true; }
};

eng::u8 chip[4096] {};

} // namespace

int main() {
	Backend backend {};
	eng::res::AssetCache<Backend, 2u> cache;
	check(cache.init(backend, {.chip_budget = 128u, .max_assets = 2u}), "AssetCache initializes");
	const eng::res::AssetId id = cache.declare("bob", 32u, eng::res::MemoryRequest::Chip);
	check(cache.prefetch(id), "Chip asset load starts");
	cache.on_load_done(id, 32);

	eng::graphics::FramePlan frame {};
	check(frame.retain_dma_asset(cache.lease_dma(cache.view(id).handle)),
	      "FramePlan accepts a valid Chip DMA lease during setup");
	check(!cache.shutdown(), "FramePlan-owned lease prevents cache teardown");
	frame.clear();
	check(!cache.shutdown(), "per-frame clear preserves the level owner and lease");
	frame.release_dma_assets();
	check(cache.shutdown(), "scene teardown releases FramePlan lease");

	check(cache.init(backend, {.chip_budget = 128u, .max_assets = 2u}), "cache reinitializes");
	const eng::res::AssetId second = cache.declare("copper", 32u, eng::res::MemoryRequest::Chip);
	check(cache.prefetch(second), "second Chip asset load starts");
	cache.on_load_done(second, 32);
	eng::MemoryManager memory {};
	check(memory.configure(chip, sizeof(chip), nullptr, 0u, nullptr, 0u, 16u), "scene memory configures");
	eng::copper::Plan copper;
	check(copper.begin(memory, {.copper_bytes = 1024u}), "Copper plan begins");
	check(copper.retain_dma_asset(cache.lease_dma(cache.view(second).handle)),
	      "Copper plan retains a list asset for its active-list lifetime");
	check(!cache.shutdown(), "Copper-owned lease prevents cache teardown");
	copper.begin_frame();
	check(!cache.shutdown(), "frame rotation does not release the Copper plan owner");
	copper.release();
	check(cache.shutdown(), "Copper teardown releases its DMA owner");

	if (failures != 0) return 1;
	std::printf("OK: FramePlan y copper::Plan retienen leases Chip hasta teardown, sin pin por frame.\n");
	return 0;
}
