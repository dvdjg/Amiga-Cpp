#pragma once

/// \file auzx.hpp
/// **Contenedor AUZX**: cabecera + índice de chunks para audio PCM comprimido (el que consume
/// `PcmStream` y produce el packer de PC). Freestanding, sin heap: solo describe el layout; no
/// interpreta el flujo comprimido (de eso se encarga `pcm_codec`).
///
/// `AUDIO_STREAMING.md` §2 describe el diseño; esta es la definición **exacta** (todo
/// little-endian):
///
/// ```text
///   offset  tam  campo
///   0       4    magic  "AUZX"
///   4       1    version        (1)
///   5       1    compression    (eng::audio::pcm_codec::Codec)
///   6       2    sample_rate    (u16)   p. ej. 8000, 11025, 16000, 22050
///   8       2    channels       (u16)   1 = mono
///   10      1    bits           (u8)    8 (PCM con signo)
///   11      1    reserved       (0)
///   12      4    total_samples  (u32)   muestras sin comprimir en todo el flujo
///   16      2    chunk_samples  (u16)   muestras por chunk DEScomprimido (potencia de 2)
///   18      2    num_chunks     (u16)
///   20      4    table_offset   (u32)   offset del índice de chunks (normalmente 32)
///   24      4    data_offset    (u32)   offset del primer chunk comprimido
///   28      4    checksum       (u32)   0 = sin checksum; si != 0, XOR de los bytes del payload
///   ------------------------------------------------------------------
///   32      …    índice: num_chunks entradas de 8 bytes { offset u32, size u32 }
///   data_offset … chunks comprimidos, en orden de reproducción
/// ```
///
/// El **índice** permite acceso aleatorio (saltar a un chunk sin leer los anteriores) y es lo
/// que un trackloader necesita para precargar. El payload de cada chunk es lo que se le pasa a
/// `pcm_codec::decode(..., compression)`, que lo expande a `chunk_samples` muestras (salvo el
/// último chunk, que puede ser más corto).
///
/// Ver `docs/engine/architecture/AUDIO_STREAMING.md` §2/§7.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::auzx {

/// Tamaño de la cabecera fija.
inline constexpr eng::usize kHeaderSize = 32u;
/// Tamaño de una entrada del índice de chunks.
inline constexpr eng::usize kChunkEntrySize = 8u;
/// Versión del formato.
inline constexpr eng::u8 kVersion = 1u;

/// Cabecera AUZX ya validada (campos deserializados).
struct Header {
	eng::u8 version = 0u;
	eng::u8 compression = 0u;
	eng::u16 sample_rate = 0u;
	eng::u16 channels = 0u;
	eng::u8 bits = 0u;
	eng::u32 total_samples = 0u;
	eng::u16 chunk_samples = 0u;
	eng::u16 num_chunks = 0u;
	eng::u32 table_offset = 0u;
	eng::u32 data_offset = 0u;
	eng::u32 checksum = 0u;
};

/// Lee un `u16` little-endian en el offset `o`.
[[nodiscard]] inline eng::u16 rd16(eng::Span<const eng::u8> b, eng::usize o) noexcept {
	return static_cast<eng::u16>(static_cast<eng::u16>(b[o]) |
				     static_cast<eng::u16>(static_cast<eng::u16>(b[o + 1u]) << 8u));
}
/// Lee un `u32` little-endian en el offset `o`.
[[nodiscard]] inline eng::u32 rd32(eng::Span<const eng::u8> b, eng::usize o) noexcept {
	return static_cast<eng::u32>(b[o]) | (static_cast<eng::u32>(b[o + 1u]) << 8u) |
	       (static_cast<eng::u32>(b[o + 2u]) << 16u) | (static_cast<eng::u32>(b[o + 3u]) << 24u);
}
/// Escribe un `u16` little-endian en el offset `o`.
inline void wr16(eng::Span<eng::u8> b, eng::usize o, eng::u16 v) noexcept {
	b[o] = static_cast<eng::u8>(v & 0xffu);
	b[o + 1u] = static_cast<eng::u8>(v >> 8u);
}
/// Escribe un `u32` little-endian en el offset `o`.
inline void wr32(eng::Span<eng::u8> b, eng::usize o, eng::u32 v) noexcept {
	b[o] = static_cast<eng::u8>(v & 0xffu);
	b[o + 1u] = static_cast<eng::u8>((v >> 8u) & 0xffu);
	b[o + 2u] = static_cast<eng::u8>((v >> 16u) & 0xffu);
	b[o + 3u] = static_cast<eng::u8>((v >> 24u) & 0xffu);
}

/// Valida y deserializa la cabecera (magic, versión, coherencia del índice). Devuelve `false` si
/// el buffer es corto, el magic no coincide, la versión no es soportada o el índice se sale.
[[nodiscard]] inline bool parse(eng::Span<const eng::u8> file, Header& out) noexcept {
	if (file.size() < kHeaderSize) {
		return false;
	}
	if (file[0] != 'A' || file[1] != 'U' || file[2] != 'Z' || file[3] != 'X') {
		return false;
	}
	out.version = file[4];
	out.compression = file[5];
	out.sample_rate = rd16(file, 6u);
	out.channels = rd16(file, 8u);
	out.bits = file[10];
	out.total_samples = rd32(file, 12u);
	out.chunk_samples = rd16(file, 16u);
	out.num_chunks = rd16(file, 18u);
	out.table_offset = rd32(file, 20u);
	out.data_offset = rd32(file, 24u);
	out.checksum = rd32(file, 28u);
	if (out.version != kVersion || out.channels != 1u || out.bits != 8u) {
		return false;
	}
	if (out.table_offset < kHeaderSize ||
	    out.table_offset + out.num_chunks * kChunkEntrySize > file.size()) {
		return false;
	}
	if (out.data_offset > file.size()) {
		return false;
	}
	return true;
}

/// Offsets y tamaño del chunk `index` segun el indice (valida que el chunk quepa).
[[nodiscard]] inline bool chunk_extent(const Header& h, eng::Span<const eng::u8> file,
				       eng::u16 index, eng::u32& offset, eng::u32& size) noexcept {
	if (index >= h.num_chunks) {
		return false;
	}
	const eng::usize e = h.table_offset + static_cast<eng::usize>(index) * kChunkEntrySize;
	offset = rd32(file, e);
	size = rd32(file, e + 4u);
	if (offset > file.size() || offset + size > file.size()) {
		return false;
	}
	return true;
}

/// Vista del payload comprimido del chunk `index` (vacia si fuera de rango).
[[nodiscard]] inline eng::Span<const eng::u8> chunk(eng::Span<const eng::u8> file,
						    const Header& h, eng::u16 index,
						    eng::u32& size_out) noexcept {
	eng::u32 off = 0u;
	eng::u32 sz = 0u;
	if (!chunk_extent(h, file, index, off, sz)) {
		size_out = 0u;
		return eng::Span<const eng::u8> {};
	}
	size_out = sz;
	return eng::Span<const eng::u8> {file.data() + off, static_cast<eng::usize>(sz)};
}

} // namespace eng::audio::auzx
