#include <amg/platform/amiga_minimal.hpp>

#include "support/gcc8_c_support.h"

namespace {

volatile unsigned short* const custom_base = reinterpret_cast<volatile unsigned short*>(0xdff000);
volatile unsigned long* const vpos_long = reinterpret_cast<volatile unsigned long*>(0xdff004);

constexpr unsigned short custom_color_offset = 0x180 / 2;

} // namespace

namespace amg::amiga {

void DebugOverlay::clear() {
	debug_clear();
}

void DebugOverlay::text(s16 x, s16 y, const char* value, u32 rgb) {
	debug_text(x, y, value, rgb);
}

void DebugOverlay::rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb) {
	debug_rect(left, top, right, bottom, rgb);
}

void DebugOverlay::filled_rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb) {
	debug_filled_rect(left, top, right, bottom, rgb);
}

void MinimalBackend::boot() {
	set_warpmode(false);
}

void MinimalBackend::wait_vblank() {
	debug_start_idle();
	while ((*vpos_long & 0x1ff00u) == (311u << 8)) {
	}
	while ((*vpos_long & 0x1ff00u) != (311u << 8)) {
	}
	debug_stop_idle();
}

void MinimalBackend::set_color(u8 index, u16 rgb444) {
	if (index < 32) {
		custom_base[custom_color_offset + index] = rgb444;
	}
}

void MinimalBackend::set_warpmode(bool enabled) {
	warpmode(enabled ? 1 : 0);
}

} // namespace amg::amiga
