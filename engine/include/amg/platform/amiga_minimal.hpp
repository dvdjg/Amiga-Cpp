#pragma once

#include <amg/core/types.hpp>
#include <amg/memory/arena.hpp>

namespace amg::amiga {

struct HardwareProfile {
	const char* id;
	u16 chip_kb;
	u16 slow_kb;
	u16 fast_kb;
	bool pal;
};

constexpr HardwareProfile a500_1mb_slow {
	"A500_1MB_Slow",
	512,
	512,
	0,
	true,
};

struct DebugOverlay {
	void clear();
	void text(s16 x, s16 y, const char* value, u32 rgb);
	void rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb);
	void filled_rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb);
};

class MinimalBackend {
public:
	using Profile = HardwareProfile;

	constexpr explicit MinimalBackend(Profile profile = a500_1mb_slow)
		: m_profile(profile) {}
	~MinimalBackend();

	MinimalBackend(const MinimalBackend&) = delete;
	MinimalBackend& operator=(const MinimalBackend&) = delete;

	void boot();
	bool configure_memory(const MemoryConfig& config);
	void release_memory();
	void wait_vblank();
	void set_color(u8 index, u16 rgb444);
	void set_warpmode(bool enabled);

	constexpr const Profile& profile() const { return m_profile; }
	constexpr MemorySystem& memory() { return m_memory; }
	constexpr const MemorySystem& memory() const { return m_memory; }
	constexpr const MemoryReport& memory_report() const { return m_memory_report; }
	constexpr DebugOverlay& debug() { return m_debug; }

private:
	Profile m_profile;
	MemorySystem m_memory {};
	MemoryReport m_memory_report {};
	DebugOverlay m_debug {};
	void* m_chip_alloc = nullptr;
	u32 m_chip_alloc_size = 0;
	void* m_slow_alloc = nullptr;
	u32 m_slow_alloc_size = 0;
	void* m_frame_alloc = nullptr;
	u32 m_frame_alloc_size = 0;
};

} // namespace amg::amiga
