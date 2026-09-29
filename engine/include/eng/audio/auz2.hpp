#pragma once

/// \\file auz2.hpp
/// Contenedor AUZ2 para audio PCM por chunks, portable y freestanding.
///
/// El contenedor mantiene la cabecera y la selección de codec separadas del
/// algoritmo de compresión. El decoder Amiga puede procesar un chunk sin cargar
/// el archivo completo: el encoder PC escribe un registro de chunk seguido de
/// su payload y el consumidor avanza linealmente.

#include <eng/audio/pcm_codec.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::auz2 {

/// Versión del layout AUZ2 que entiende este parser.
inline constexpr u16 kVersion = 1u;
/// Número de bytes de la cabecera global, incluidos los cuatro bytes reservados.
inline constexpr usize kHeaderBytes = 28u;
/// Número de bytes del descriptor que precede a cada payload.
inline constexpr usize kChunkHeaderBytes = 8u;

struct Header {
	/// Versión del layout binario; debe ser `kVersion` para este decoder.
	u16 version = kVersion;
	/// Frecuencia de muestreo de la señal PCM, en muestras por segundo.
	u32 sample_rate = 11025u;
	/// Número de canales; AUZ2 v1 solo admite mono (`1`).
	u8 channels = 1u;
	/// Profundidad PCM reconstruida; AUZ2 v1 solo admite 8 bits.
	u8 bits = 8u;
	/// Número total de muestras PCM que deben producir los chunks.
	u32 total_samples = 0u;
	/// Capacidad PCM de cada chunk, excepto el último.
	u16 chunk_samples = 1024u;
	/// Número de descriptores de chunk que siguen a la cabecera.
	u16 chunk_count = 0u;
};

struct Chunk {
	/// Número de muestras que debe escribir el decoder para este chunk.
	u16 raw_samples = 0u;
	/// Número de bytes presentes en el payload comprimido.
	u16 packed_bytes = 0u;
	/// Codec indicado por el descriptor y delegado a `pcm_codec`.
	pcm_codec::Codec codec = pcm_codec::Codec::None;
};

namespace detail {
/// Escribe un entero de 16 bits en little-endian y avanza el cursor.
constexpr void put_u16(Span<u8> out, usize& at, u16 value) {
	out[at++] = static_cast<u8>(value & 0xffu);
	out[at++] = static_cast<u8>(value >> 8u);
}

/// Escribe un entero de 32 bits en little-endian usando dos palabras.
constexpr void put_u32(Span<u8> out, usize& at, u32 value) {
	put_u16(out, at, static_cast<u16>(value));
	put_u16(out, at, static_cast<u16>(value >> 16u));
}

/// Lee un entero de 16 bits en little-endian y avanza el cursor.
constexpr u16 get_u16(Span<const u8> in, usize& at) {
	const u16 value = static_cast<u16>(in[at]) | static_cast<u16>(in[at + 1u] << 8u);
	at += 2u;
	return value;
}

/// Lee un entero de 32 bits en little-endian usando dos palabras.
constexpr u32 get_u32(Span<const u8> in, usize& at) {
	const u32 lo = get_u16(in, at);
	const u32 hi = get_u16(in, at);
	return lo | (hi << 16u);
}
} // namespace detail

/// Serializa la cabecera de 28 bytes. Devuelve false si `out` no tiene capacidad.
[[nodiscard]] inline bool write_header(const Header& header, Span<u8> out) noexcept {
	if (out.size() < kHeaderBytes || header.channels != 1u || header.bits != 8u ||
		header.chunk_samples == 0u) {
		return false;
	}
	usize at = 0u;
	out[at++] = 'A'; out[at++] = 'U'; out[at++] = 'Z'; out[at++] = '2';
	detail::put_u16(out, at, header.version);
	detail::put_u16(out, at, static_cast<u16>(kHeaderBytes));
	detail::put_u32(out, at, header.sample_rate);
	out[at++] = header.channels;
	out[at++] = header.bits;
	detail::put_u16(out, at, 0u);
	detail::put_u32(out, at, header.total_samples);
	detail::put_u16(out, at, header.chunk_samples);
	detail::put_u16(out, at, header.chunk_count);
	detail::put_u32(out, at, 0u);
	return true;
}

/// Lee y valida la cabecera AUZ2. No accede fuera de `in`.
[[nodiscard]] inline bool read_header(Span<const u8> in, Header& header) noexcept {
	if (in.size() < kHeaderBytes || in[0] != 'A' || in[1] != 'U' || in[2] != 'Z' || in[3] != '2') {
		return false;
	}
	usize at = 4u;
	header.version = detail::get_u16(in, at);
	const u16 header_bytes = detail::get_u16(in, at);
	if (header.version != kVersion || header_bytes < kHeaderBytes || header_bytes > in.size()) {
		return false;
	}
	header.sample_rate = detail::get_u32(in, at);
	header.channels = in[at++];
	header.bits = in[at++];
	(void)detail::get_u16(in, at);
	header.total_samples = detail::get_u32(in, at);
	header.chunk_samples = detail::get_u16(in, at);
	header.chunk_count = detail::get_u16(in, at);
	(void)detail::get_u32(in, at);
	return header.channels == 1u && header.bits == 8u && header.chunk_samples != 0u;
}

/// Lee el descriptor del chunk y devuelve el payload restante en `payload`.
[[nodiscard]] inline bool read_chunk(Span<const u8> in, usize& cursor, Chunk& chunk,
	Span<const u8>& payload) noexcept {
	if (cursor + kChunkHeaderBytes > in.size()) {
		return false;
	}
	const u16 raw = detail::get_u16(in, cursor);
	const u16 packed = detail::get_u16(in, cursor);
	const u8 codec = in[cursor++];
	cursor += 3u;
	if (cursor + packed > in.size()) {
		return false;
	}
	chunk = {raw, packed, static_cast<pcm_codec::Codec>(codec)};
	payload = in.subspan(cursor, packed);
	cursor += packed;
	return raw != 0u;
}

/// Decodifica un chunk AUZ2 en PCM8. El destino debe tener `raw_samples` bytes.
inline s32 decode_chunk(const Chunk& chunk, Span<const u8> payload, Span<u8> dst) noexcept {
	if (dst.size() != chunk.raw_samples || payload.size() != chunk.packed_bytes) {
		return -1;
	}
	return pcm_codec::decode(payload, dst, static_cast<u8>(chunk.codec));
}

} // namespace eng::audio::auz2
