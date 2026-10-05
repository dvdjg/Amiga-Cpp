#pragma once

/// Lectura/escritura little-endian común de los contenedores host. Las operaciones comprueban
/// límites antes de tocar el buffer; los writers de AUZX y ACP1 no deben duplicar estas rutinas.

#include <vector>

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::formats {

class BinaryWriter {
public:
	explicit BinaryWriter(std::vector<eng::u8>& output) noexcept : m_output(output) {}

	bool u8(eng::usize at, eng::u8 value) noexcept { return put(at, value, 1u); }
	bool u16(eng::usize at, eng::u16 value) noexcept { return put(at, value, 2u); }
	bool u32(eng::usize at, eng::u32 value) noexcept { return put(at, value, 4u); }
	bool u64(eng::usize at, eng::u64 value) noexcept { return put(at, value, 8u); }

private:
	template <class T>
	[[nodiscard]] bool put(eng::usize at, T value, eng::usize bytes) noexcept {
		if (at > m_output.size() || bytes > m_output.size() - at) return false;
		for (eng::usize i = 0u; i < bytes; ++i) m_output[at + i] = static_cast<eng::u8>(value >> (i * 8u));
		return true;
	}
	std::vector<eng::u8>& m_output;
};

class BinaryReader {
public:
	explicit BinaryReader(eng::Span<const eng::u8> input) noexcept : m_input(input) {}

	[[nodiscard]] bool u8(eng::usize at, eng::u8& value) const noexcept { return get(at, 1u, value); }
	[[nodiscard]] bool u16(eng::usize at, eng::u16& value) const noexcept { return get(at, 2u, value); }
	[[nodiscard]] bool u32(eng::usize at, eng::u32& value) const noexcept { return get(at, 4u, value); }
	[[nodiscard]] bool u64(eng::usize at, eng::u64& value) const noexcept { return get(at, 8u, value); }

private:
	template <class T>
	[[nodiscard]] bool get(eng::usize at, eng::usize bytes, T& value) const noexcept {
		if (at > m_input.size() || bytes > m_input.size() - at) return false;
		value = 0u;
		for (eng::usize i = 0u; i < bytes; ++i) value |= static_cast<T>(m_input[at + i]) << (i * 8u);
		return true;
	}
	eng::Span<const eng::u8> m_input;
};

} // namespace audio_compressor::formats
