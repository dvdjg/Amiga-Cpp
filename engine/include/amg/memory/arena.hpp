#pragma once

#include <amg/core/types.hpp>

namespace amg {

enum class MemoryKind : u8 {
	Chip,
	Slow,
	Fast,
	Any,
};

struct MemoryBlock {
	void* data = nullptr;
	u32 size = 0;
	MemoryKind kind = MemoryKind::Any;

	constexpr bool valid() const {
		return data != nullptr && size != 0;
	}
};

struct ArenaSnapshot {
	u32 base = 0;
	u32 capacity = 0;
	u32 used = 0;
	u32 peak = 0;
	u32 remaining = 0;
	MemoryKind kind = MemoryKind::Any;
};

class LinearArena {
public:
	constexpr LinearArena() = default;

	constexpr LinearArena(void* base, u32 size, MemoryKind kind)
		: m_base(static_cast<u8*>(base)), m_size(size), m_kind(kind) {}

	void reset(void* base, u32 size, MemoryKind kind) {
		m_base = static_cast<u8*>(base);
		m_size = size;
		m_kind = kind;
		m_used = 0;
		m_peak = 0;
	}

	void clear() {
		m_used = 0;
	}

	MemoryBlock allocate(u32 bytes, u32 alignment = 2) {
		if (alignment == 0) {
			alignment = 1;
		}

		const uintptr raw = reinterpret_cast<uintptr>(m_base) + m_used;
		const uintptr aligned = align_up_ptr(raw, alignment);
		const u32 padding = static_cast<u32>(aligned - raw);
		const u32 next = m_used + padding + bytes;

		if (!m_base || next > m_size) {
			return {};
		}

		m_used = next;
		if (m_used > m_peak) {
			m_peak = m_used;
		}

		return {reinterpret_cast<void*>(aligned), bytes, m_kind};
	}

	template <typename T>
	T* allocate_array(u16 count, u32 alignment = alignof(T)) {
		MemoryBlock block = allocate(sizeof(T) * static_cast<u32>(count), alignment);
		return static_cast<T*>(block.data);
	}

	constexpr u32 capacity() const { return m_size; }
	constexpr u32 used() const { return m_used; }
	constexpr u32 peak() const { return m_peak; }
	constexpr u32 remaining() const { return m_size - m_used; }
	constexpr MemoryKind kind() const { return m_kind; }
	constexpr void* base() const { return m_base; }

	constexpr ArenaSnapshot snapshot() const {
		return {
			static_cast<u32>(reinterpret_cast<uintptr>(m_base)),
			m_size,
			m_used,
			m_peak,
			remaining(),
			m_kind,
		};
	}

private:
	u8* m_base = nullptr;
	u32 m_size = 0;
	u32 m_used = 0;
	u32 m_peak = 0;
	MemoryKind m_kind = MemoryKind::Any;
};

struct MemorySystem {
	LinearArena chip;
	LinearArena slow;
	LinearArena frame;
};

struct MemoryConfig {
	u32 chip_bytes = 0;
	u32 slow_bytes = 0;
	u32 frame_bytes = 0;
};

struct MemoryReport {
	ArenaSnapshot chip {};
	ArenaSnapshot slow {};
	ArenaSnapshot frame {};
	bool chip_ok = false;
	bool slow_ok = false;
	bool frame_ok = false;

	constexpr bool ok() const {
		return chip_ok && slow_ok && frame_ok;
	}
};

} // namespace amg
