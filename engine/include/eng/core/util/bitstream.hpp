#pragma once

/// \file bitstream.hpp
/// `eng::util::BitWriter` / `BitReader`: lectura y escritura de **campos de bits** sobre
/// un buffer de bytes, en orden **LSB-first** (el bit 0 es el de menor peso del byte, y
/// se avanza hacia el bit 7, luego el byte siguiente). Sin heap: el buffer lo aporta el
/// llamador.
///
/// Sirve para empaquetar datos de nivel, assets comprimidos y partidas guardadas, donde
/// cada byte cuenta. `write`/`read` trabajan con campos de 1..32 bits.
///
/// Uso:
///   eng::u8 buf[16];
///   eng::util::BitWriter bw {buf};
///   bw.write(5, 3);          // 3 bits
///   bw.write_bool(true);
///   const eng::usize nbytes = bw.byte_count();
///   eng::util::BitReader br {eng::Span<const eng::u8> {buf, nbytes}};
///   eng::u32 v = 0; br.read(3, v); br.read(1, v);
///
/// Verificación: HOST-121.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::util {

class BitWriter {
public:
	constexpr explicit BitWriter(eng::Span<eng::u8> buffer) noexcept
		: m_buffer(buffer) {}

	[[nodiscard]] constexpr eng::usize bit_count() const noexcept { return m_bits; }
	/// Bytes ocupados (redondeando a byte el último campo).
	[[nodiscard]] constexpr eng::usize byte_count() const noexcept {
		return (m_bits + 7u) / 8u;
	}
	[[nodiscard]] constexpr eng::usize capacity_bits() const noexcept {
		return m_buffer.size() * 8u;
	}
	[[nodiscard]] constexpr bool full() const noexcept { return m_bits >= capacity_bits(); }

	constexpr void clear() noexcept {
		for (eng::usize i = 0; i < m_buffer.size(); ++i) {
			m_buffer[i] = 0u;
		}
		m_bits = 0u;
	}

	/// Escribe los `bits` bajos de `value` (1..32). `false` si no caben.
	constexpr bool write(eng::u32 value, eng::u8 bits) noexcept {
		if (bits == 0u || bits > 32u || m_bits + bits > capacity_bits()) {
			return false;
		}
		for (eng::u8 k = 0u; k < bits; ++k) {
			const eng::usize pos = m_bits + k;
			const eng::u8 mask = static_cast<eng::u8>(1u << (pos & 7u));
			eng::u8& byte = m_buffer[pos >> 3u];
			if (((value >> k) & 1u) != 0u) {
				byte = static_cast<eng::u8>(byte | mask);
			} else {
				byte = static_cast<eng::u8>(byte & static_cast<eng::u8>(~mask));
			}
		}
		m_bits += bits;
		return true;
	}

	constexpr bool write_bool(bool value) noexcept {
		return write(value ? 1u : 0u, 1u);
	}

private:
	eng::Span<eng::u8> m_buffer;
	eng::usize m_bits = 0u;
};

class BitReader {
public:
	constexpr explicit BitReader(eng::Span<const eng::u8> data) noexcept : m_data(data) {}

	[[nodiscard]] constexpr eng::usize bit_count() const noexcept { return m_bits; }
	[[nodiscard]] constexpr eng::usize total_bits() const noexcept {
		return m_data.size() * 8u;
	}
	[[nodiscard]] constexpr eng::usize remaining() const noexcept {
		return total_bits() - m_bits;
	}

	constexpr void rewind() noexcept { m_bits = 0u; }

	/// Lee `bits` (1..32) a `out`. `false` si no quedan suficientes.
	constexpr bool read(eng::u8 bits, eng::u32& out) noexcept {
		if (bits == 0u || bits > 32u || m_bits + bits > total_bits()) {
			return false;
		}
		eng::u32 value = 0u;
		for (eng::u8 k = 0u; k < bits; ++k) {
			const eng::usize pos = m_bits + k;
			const eng::u32 bit = (m_data[pos >> 3u] >> (pos & 7u)) & 1u;
			value |= static_cast<eng::u32>(bit << k);
		}
		out = value;
		m_bits += bits;
		return true;
	}

	constexpr bool read_bool(bool& out) noexcept {
		eng::u32 value = 0u;
		if (!read(1u, value)) {
			return false;
		}
		out = value != 0u;
		return true;
	}

private:
	eng::Span<const eng::u8> m_data;
	eng::usize m_bits = 0u;
};

} // namespace eng::util
