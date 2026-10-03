#pragma once

/// \file engz.hpp
/// **Contenedor `.engz`** (`ROADMAP_RESOURCES.md` R6.4): un blob comprimido con una **cabecera** que
/// declara codec, tamaños, alineación del destino y **CRC-32**, para cargar assets y librerías
/// comprimidos desde disco. Compone la etapa genérica `res::decode` (R6.5).
///
/// Layout **little-endian** (portable), cabecera de 20 B:
///
/// ```text
///  off  campo                tipo
///   0   magic ('ENGZ')       u32
///   4   version              u16
///   6   codec (res::Codec)   u8
///   7   align_log2           u8     (2^align, alineación del destino)
///   8   compressed_size      u32
///  12   uncompressed_size    u32
///  16   payload_crc32        u32    (CRC-32 del payload comprimido)
///  20   payload              bytes
/// ```
///
/// El `magic` sigue la convención de `.englib` (`kEngLibMagic`): el valor entero de los 4 caracteres
/// en orden (`'E'<<24 | 'N'<<16 | 'G'<<8 | 'Z'`).

#include <eng/core/data/crc32.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/res/decode.hpp>

namespace eng::res {

inline constexpr eng::u32 kEngzMagic = 0x454e475au; // 'ENGZ'
inline constexpr eng::u16 kEngzVersion = 1u;
inline constexpr eng::usize kEngzHeaderSize = 20u;

/// Cabecera de `.engz` (valores ya en orden de máquina tras el parseo LE).
struct EngzHeader {
	eng::u32 magic = kEngzMagic;
	eng::u16 version = kEngzVersion;
	eng::u8 codec = 0u;        ///< `res::Codec`
	eng::u8 align_log2 = 0u;   ///< alineación del destino = `2^align_log2`
	eng::u32 compressed_size = 0u;
	eng::u32 uncompressed_size = 0u;
	eng::u32 payload_crc32 = 0u;
};

/// Lee un `u32` little-endian en `s[o..o+4)` (sin depender de la alineación ni del orden nativo).
[[nodiscard]] inline eng::u32 engz_le32(eng::Span<const eng::u8> s, eng::usize o) noexcept {
	return eng::u32(s[o]) | (eng::u32(s[o + 1u]) << 8u) | (eng::u32(s[o + 2u]) << 16u) |
	       (eng::u32(s[o + 3u]) << 24u);
}
/// Lee un `u16` little-endian en `s[o..o+2)`.
[[nodiscard]] inline eng::u16 engz_le16(eng::Span<const eng::u8> s, eng::usize o) noexcept {
	return static_cast<eng::u16>(eng::u16(s[o]) | static_cast<eng::u16>(eng::u16(s[o + 1u]) << 8u));
}
/// Escribe `v` como `u32` little-endian en `s[o..o+4)`.
inline void engz_put_le32(eng::Span<eng::u8> s, eng::usize o, eng::u32 v) noexcept {
	s[o] = static_cast<eng::u8>(v & 0xffu);
	s[o + 1u] = static_cast<eng::u8>((v >> 8u) & 0xffu);
	s[o + 2u] = static_cast<eng::u8>((v >> 16u) & 0xffu);
	s[o + 3u] = static_cast<eng::u8>((v >> 24u) & 0xffu);
}

/// Parsea y **valida** la cabecera de un `.engz` (`InvalidArgument` si el magic/versión no cuadran
/// o el payload declarado no cabe en `src`).
[[nodiscard]] inline eng::util::Expected<EngzHeader, eng::Result>
parse_engz(eng::Span<const eng::u8> src) noexcept {
	if (src.size() < kEngzHeaderSize) return eng::util::unexpected(eng::Result::InvalidArgument);
	EngzHeader h {};
	h.magic = engz_le32(src, 0u);
	h.version = engz_le16(src, 4u);
	h.codec = src[6u];
	h.align_log2 = src[7u];
	h.compressed_size = engz_le32(src, 8u);
	h.uncompressed_size = engz_le32(src, 12u);
	h.payload_crc32 = engz_le32(src, 16u);
	if (h.magic != kEngzMagic || h.version != kEngzVersion) {
		return eng::util::unexpected(eng::Result::InvalidArgument);
	}
	if (src.size() < kEngzHeaderSize + h.compressed_size) {
		return eng::util::unexpected(eng::Result::InvalidArgument);
	}
	return h;
}

/// Vista del payload comprimido de un `.engz` ya parseado.
[[nodiscard]] inline eng::Span<const eng::u8> engz_payload(eng::Span<const eng::u8> src,
							  const EngzHeader& h) noexcept {
	return src.subspan(kEngzHeaderSize, h.compressed_size);
}

/// **Verifica el CRC** del payload comprimido (`Corrupt` si no coincide).
[[nodiscard]] inline eng::util::Expected<void, eng::Result>
verify_engz(eng::Span<const eng::u8> src, const EngzHeader& h) noexcept {
	const eng::Span<const eng::u8> payload = engz_payload(src, h);
	if (eng::crc32(payload.data(), payload.size()) != h.payload_crc32) {
		return eng::util::unexpected(eng::Result::Corrupt);
	}
	return {};
}

/// **Decodifica** un `.engz` en `dst`: parsea, verifica el CRC y decodifica con `res::decode`.
/// Devuelve los bytes descomprimidos escritos, o la causa (`InvalidArgument`/`Corrupt`/`Unsupported`).
[[nodiscard]] inline eng::util::Expected<eng::u32, eng::Result>
decode_engz(eng::Span<const eng::u8> src, eng::Span<eng::u8> dst) noexcept {
	const auto parsed = parse_engz(src);
	if (!parsed) return eng::util::unexpected(parsed.error());
	const EngzHeader h = *parsed;
	if (dst.size() < h.uncompressed_size) return eng::util::unexpected(eng::Result::InvalidArgument);
	const auto ok = verify_engz(src, h);
	if (!ok) return eng::util::unexpected(ok.error());
	const eng::s32 n = decode(static_cast<Codec>(h.codec), engz_payload(src, h),
				  dst.subspan(0u, h.uncompressed_size));
	if (n < 0) return eng::util::unexpected(eng::Result::Unsupported);
	return static_cast<eng::u32>(n);
}

/// **Construye** un `.engz` en `dst` (para el pipeline host o un test): cabecera LE + payload +
/// CRC. Devuelve el tamaño escrito, o `0` si no cabe.
[[nodiscard]] inline eng::usize build_engz(eng::Span<eng::u8> dst, eng::Span<const eng::u8> payload,
					   Codec codec, eng::u32 uncompressed_size,
					   eng::u8 align_log2 = 1u) noexcept {
	if (dst.size() < kEngzHeaderSize + payload.size()) return 0u;
	engz_put_le32(dst, 0u, kEngzMagic);
	dst[4u] = static_cast<eng::u8>(kEngzVersion & 0xffu);
	dst[5u] = static_cast<eng::u8>((kEngzVersion >> 8u) & 0xffu);
	dst[6u] = static_cast<eng::u8>(codec);
	dst[7u] = align_log2;
	engz_put_le32(dst, 8u, static_cast<eng::u32>(payload.size()));
	engz_put_le32(dst, 12u, uncompressed_size);
	engz_put_le32(dst, 16u, eng::crc32(payload.data(), payload.size()));
	for (eng::usize i = 0u; i < payload.size(); ++i) {
		dst[kEngzHeaderSize + i] = payload[i];
	}
	return kEngzHeaderSize + payload.size();
}

} // namespace eng::res
