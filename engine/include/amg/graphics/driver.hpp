#pragma once

#include <amg/core/types.hpp>

namespace amg {

enum class GraphicsDriverId : u8 {
	EhbScene,
	Standard5,
	Standard4,
	FakeDualPlayfield,
	DualPlayfield,
	SpriteBackdrop,
	CopperHeavy,
};

struct FrameStats {
	u16 frame_index = 0;
	u16 blit_jobs = 0;
	u16 copper_patches = 0;
	u16 hardware_sprites = 0;
	u16 software_sprites = 0;
};

struct RenderContext {
	FrameStats* stats = nullptr;
};

template <typename Driver>
concept GraphicsDriver = requires(Driver driver, RenderContext& context) {
	Driver::id;
	driver.begin_frame(context);
	driver.end_frame(context);
};

} // namespace amg
