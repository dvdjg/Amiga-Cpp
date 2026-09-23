#pragma once

/// \file byte_order.hpp
/// Lectura/escritura de enteros **big-endian** y **little-endian** sobre buffers de bytes.
///
/// Los formatos de datos del engine (contenedor UAF-R, ejecutables **HUNK**, tablas de
/// assets) están en **big-endian** (nativo m68k) y se leen igual en host x86 que en Amiga.
/// Los formatos nativos de la máquina (`.englib`) usan el orden del host. Estas primitivas
/// son la base común para no repetir el mismo desplazamiento de bits en cada parser.
///
/// Ver `docs/engine/architecture/RESOURCE_SYSTEM.md`.

#include <eng/core/types/types.hpp>

namespace eng {

/// Lector **big-endian** de 2 bytes.
[[nodiscard]] constexpr u16 read_be16(const u8* p) {
	return static_cast<u16>((static_cast<u16>(p[0]) << 8u) | p[1]);
}

/// Lector **big-endian** de 4 bytes.
[[nodiscard]] constexpr u32 read_be32(const u8* p) {
	return (static_cast<u32>(p[0]) << 24u) | (static_cast<u32>(p[1]) << 16u) |
	       (static_cast<u32>(p[2]) << 8u) | static_cast<u32>(p[3]);
}

/// Escritor **big-endian** de 2 bytes.
constexpr void write_be16(u8* p, u16 v) {
	p[0] = static_cast<u8>(v >> 8u);
	p[1] = static_cast<u8>(v);
}

/// Escritor **big-endian** de 4 bytes.
constexpr void write_be32(u8* p, u32 v) {
	p[0] = static_cast<u8>(v >> 24u);
	p[1] = static_cast<u8>(v >> 16u);
	p[2] = static_cast<u8>(v >> 8u);
	p[3] = static_cast<u8>(v);
}

/// Lector **little-endian** de 2 bytes.
[[nodiscard]] constexpr u16 read_le16(const u8* p) {
	return static_cast<u16>(static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8u));
}

/// Lector **little-endian** de 4 bytes.
[[nodiscard]] constexpr u32 read_le32(const u8* p) {
	return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8u) |
	       (static_cast<u32>(p[2]) << 16u) | (static_cast<u32>(p[3]) << 24u);
}

} // namespace eng
