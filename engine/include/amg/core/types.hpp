#pragma once

namespace amg {

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned long;
using s8 = signed char;
using s16 = signed short;
using s32 = signed long;
using usize = __SIZE_TYPE__;
using uintptr = __UINTPTR_TYPE__;

struct Size2u {
	u16 width;
	u16 height;
};

struct Point2s {
	s16 x;
	s16 y;
};

enum class Result : u8 {
	Ok,
	OutOfMemory,
	InvalidArgument,
	Unsupported,
	HardwareLimit,
};

constexpr u32 align_up(u32 value, u32 alignment) {
	return (value + alignment - 1u) & ~(alignment - 1u);
}

constexpr uintptr align_up_ptr(uintptr value, uintptr alignment) {
	return (value + alignment - 1u) & ~(alignment - 1u);
}

} // namespace amg
