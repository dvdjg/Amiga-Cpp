#pragma once

/// \file binary.hpp
/// **Lectura/escritura binaria segura** sobre `Span` (`eng::util`): un cursor
/// little-endian que comprueba límites en cada operación. Sustituye al patrón
/// `*ptr++` / `memcpy` y a la aritmética de punteros, que en 68000 además falla si
/// el acceso queda desalineado.
///
/// Se usa para serializar/deserializar datos empaquetados del engine (libro de
/// aperturas, tablas, formatos de assets) sin `reinterpret_cast` a structs: cada
/// campo se lee/escribe explícitamente y el cursor no avanza si no hay bytes.
///
/// Uso:
///   eng::util::ByteReader reader{eng::Span<const eng::u8>{bytes, n}};
///   eng::u32 value = 0;
///   if (reader.read_u32(value)) { ... }
///
///   eng::util::ByteWriter writer{eng::Span<eng::u8>{buffer, n}};
///   (void)writer.write_u32(0x12345678u);
///
/// Verificación: HOST-150.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::util {

/// Cursor de lectura little-endian con comprobación de límites.
class ByteReader {
public:
	constexpr ByteReader() noexcept = default;
	constexpr explicit ByteReader(eng::Span<const u8> data) noexcept : m_data(data) {}

	[[nodiscard]] constexpr usize position() const noexcept { return m_pos; }
	[[nodiscard]] constexpr usize remaining() const noexcept { return m_data.size() - m_pos; }
	[[nodiscard]] constexpr bool exhausted() const noexcept { return m_pos >= m_data.size(); }

	/// Avanza `count` bytes sin leerlos. `false` si no hay suficientes.
	[[nodiscard]] constexpr bool skip(usize count) noexcept {
		if (count > remaining()) {
			return false;
		}
		m_pos += count;
		return true;
	}

	[[nodiscard]] constexpr bool read_u8(u8& out) noexcept {
		if (remaining() < 1u) {
			return false;
		}
		out = m_data[m_pos];
		++m_pos;
		return true;
	}

	[[nodiscard]] constexpr bool read_u16(u16& out) noexcept {
		if (remaining() < 2u) {
			return false;
		}
		out = static_cast<u16>(static_cast<u16>(m_data[m_pos]) |
		                       static_cast<u16>(static_cast<u16>(m_data[m_pos + 1u]) << 8u));
		m_pos += 2u;
		return true;
	}

	[[nodiscard]] constexpr bool read_u32(u32& out) noexcept {
		if (remaining() < 4u) {
			return false;
		}
		out = static_cast<u32>(m_data[m_pos]) |
		      (static_cast<u32>(m_data[m_pos + 1u]) << 8u) |
		      (static_cast<u32>(m_data[m_pos + 2u]) << 16u) |
		      (static_cast<u32>(m_data[m_pos + 3u]) << 24u);
		m_pos += 4u;
		return true;
	}

	[[nodiscard]] constexpr bool read_s16(s16& out) noexcept {
		u16 value = 0u;
		if (!read_u16(value)) {
			return false;
		}
		out = static_cast<s16>(value);
		return true;
	}

	[[nodiscard]] constexpr bool read_s32(s32& out) noexcept {
		u32 value = 0u;
		if (!read_u32(value)) {
			return false;
		}
		out = static_cast<s32>(value);
		return true;
	}

	/// Vista de los siguientes `count` bytes (sin copiar) y avance del cursor.
	[[nodiscard]] constexpr bool read_view(usize count, eng::Span<const u8>& out) noexcept {
		if (count > remaining()) {
			return false;
		}
		out = m_data.subspan(m_pos, count);
		m_pos += count;
		return true;
	}

	/// Copia `count` bytes en `dst` y avanza. `false` (sin avanzar) si no caben.
	[[nodiscard]] constexpr bool read_into(eng::Span<u8> dst) noexcept {
		if (dst.size() > remaining()) {
			return false;
		}
		for (usize i = 0; i < dst.size(); ++i) {
			dst[i] = m_data[m_pos + i];
		}
		m_pos += dst.size();
		return true;
	}

private:
	eng::Span<const u8> m_data {};
	usize m_pos = 0u;
};

/// Cursor de escritura little-endian con comprobación de capacidad.
class ByteWriter {
public:
	constexpr ByteWriter() noexcept = default;
	constexpr explicit ByteWriter(eng::Span<u8> data) noexcept : m_data(data) {}

	[[nodiscard]] constexpr usize position() const noexcept { return m_pos; }
	[[nodiscard]] constexpr usize remaining() const noexcept { return m_data.size() - m_pos; }

	[[nodiscard]] constexpr bool write_u8(u8 value) noexcept {
		if (remaining() < 1u) {
			return false;
		}
		m_data[m_pos] = value;
		++m_pos;
		return true;
	}

	[[nodiscard]] constexpr bool write_u16(u16 value) noexcept {
		if (remaining() < 2u) {
			return false;
		}
		m_data[m_pos] = static_cast<u8>(value & 0xffu);
		m_data[m_pos + 1u] = static_cast<u8>((value >> 8u) & 0xffu);
		m_pos += 2u;
		return true;
	}

	[[nodiscard]] constexpr bool write_u32(u32 value) noexcept {
		if (remaining() < 4u) {
			return false;
		}
		m_data[m_pos] = static_cast<u8>(value & 0xffu);
		m_data[m_pos + 1u] = static_cast<u8>((value >> 8u) & 0xffu);
		m_data[m_pos + 2u] = static_cast<u8>((value >> 16u) & 0xffu);
		m_data[m_pos + 3u] = static_cast<u8>((value >> 24u) & 0xffu);
		m_pos += 4u;
		return true;
	}

	[[nodiscard]] constexpr bool write_s16(s16 value) noexcept {
		return write_u16(static_cast<u16>(value));
	}

	[[nodiscard]] constexpr bool write_s32(s32 value) noexcept {
		return write_u32(static_cast<u32>(value));
	}

	[[nodiscard]] constexpr bool write_bytes(eng::Span<const u8> src) noexcept {
		if (src.size() > remaining()) {
			return false;
		}
		for (usize i = 0; i < src.size(); ++i) {
			m_data[m_pos + i] = src[i];
		}
		m_pos += src.size();
		return true;
	}

	/// Vista mutable de los siguientes `count` bytes (sin avanzar). `false` si no caben.
	[[nodiscard]] constexpr bool reserve_view(usize count, eng::Span<u8>& out) noexcept {
		if (count > remaining()) {
			return false;
		}
		out = m_data.subspan(m_pos, count);
		m_pos += count;
		return true;
	}

private:
	eng::Span<u8> m_data {};
	usize m_pos = 0u;
};

} // namespace eng::util
