#pragma once

/// \file media.hpp
/// **Interfaz de medios de audio**: punto único para reconocer un medio y despachar su lectura,
/// independientemente del **contenedor** (PCM crudo o AUZX) y del **códec**
/// (`pcm_codec::Codec`). Sobre ella se apoyan el streaming (`pcm_stream.hpp`) y el juego.
///
/// Un "medio" es un blob en memoria (leído de ROM, de un AUI/asset o de disquete). `open()`
/// detecta el contenedor por su magic (o lo asume PCM si no lo hay) y rellena `Info`; a partir
/// de ahí se accede por **chunks** con `chunk_size(i)`/`decode_chunk(i, dst)`.
///
/// ```text
///   blob ──open──► Info {container, codec, rate, channels, bits, total_samples, num_chunks}
///                        │
///        chunk_size(i) ──┴──► decode_chunk(i, dst) ──► PCM 8-bit (s8)
/// ```
///
/// Contenedores: `Pcm` (PCM crudo mono 8-bit, sin cabecera) y `Auzx` (`auzx.hpp`). El códec es
/// el de `pcm_codec` (None/DeltaRle/FibDelta/ImaAdpcm/ZX0/DeltaZx0), ya sea el de la cabecera
/// AUZX o `None` para PCM crudo.

#include <eng/audio/auzx.hpp>
#include <eng/audio/pcm_codec.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::media {

/// Contenedor detectado.
enum class Container : eng::u8 {
	Pcm = 0u,  ///< PCM crudo mono 8-bit (sin cabecera): todo el blob es audio
	Auzx = 1u, ///< contenedor AUZX (cabecera + índice de chunks)
};

/// Descripción de un medio ya reconocido.
struct Info {
	Container container = Container::Pcm;
	pcm_codec::Codec codec = pcm_codec::Codec::None;
	eng::u16 sample_rate = 0u;
	eng::u16 channels = 1u;
	eng::u8 bits = 8u;
	eng::u32 total_samples = 0u;
	eng::u16 chunk_samples = 0u;
	eng::u16 num_chunks = 0u;
	auzx::Header auzx_header {}; ///< válido solo si `container == Auzx`
};

/// Reconoce el medio `blob` y rellena `out`. PCM crudo si no hay magic AUZX (y el tamaño es
/// múltiplo de la muestra); `false` si es un AUZX corrupto o un blob vacío.
[[nodiscard]] inline bool open(eng::Span<const eng::u8> blob, Info& out) noexcept {
	if (blob.size() == 0u) {
		return false;
	}
	const bool auzx_magic = blob.size() >= 4u && blob[0] == 'A' && blob[1] == 'U' &&
				blob[2] == 'Z' && blob[3] == 'X';
	if (auzx_magic) {
		auzx::Header h {};
		if (!auzx::parse(blob, h)) {
			return false;
		}
		out.container = Container::Auzx;
		out.codec = static_cast<pcm_codec::Codec>(h.compression);
		out.sample_rate = h.sample_rate;
		out.channels = h.channels;
		out.bits = h.bits;
		out.total_samples = h.total_samples;
		out.chunk_samples = h.chunk_samples;
		out.num_chunks = h.num_chunks;
		out.auzx_header = h;
		return true;
	}
	// PCM crudo: el blob entero es audio (mono 8-bit).
	out.container = Container::Pcm;
	out.codec = pcm_codec::Codec::None;
	out.sample_rate = 0u; // lo fija el llamador (no viene en el blob)
	out.channels = 1u;
	out.bits = 8u;
	out.total_samples = static_cast<eng::u32>(blob.size());
	out.chunk_samples = static_cast<eng::u16>(blob.size() > 0xffffu ? 0xffffu : blob.size());
	out.num_chunks = 1u;
	out.auzx_header = {};
	return true;
}

/// Muestras DEScomprimidas del chunk `index` (el último puede ser más corto).
[[nodiscard]] inline eng::u32 chunk_samples(const Info& info, eng::u16 index) noexcept {
	if (info.container == Container::Pcm) {
		return info.total_samples;
	}
	if (index + 1u < info.num_chunks) {
		return info.chunk_samples;
	}
	const eng::u32 done = static_cast<eng::u32>(index) * info.chunk_samples;
	return (info.total_samples > done) ? (info.total_samples - done) : 0u;
}

/// Payload **comprimido** del chunk `index` (para AUZX); para PCM es el blob entero.
[[nodiscard]] inline eng::Span<const eng::u8> chunk_data(eng::Span<const eng::u8> blob,
							 const Info& info, eng::u16 index,
							 eng::u32& size_out) noexcept {
	if (info.container == Container::Pcm) {
		size_out = static_cast<eng::u32>(blob.size());
		return blob;
	}
	return auzx::chunk(blob, info.auzx_header, index, size_out);
}

/// Decodifica el chunk `index` a PCM 8-bit (s8) en `dst`. Devuelve las muestras escritas o `-1`.
[[nodiscard]] inline eng::s32 decode_chunk(eng::Span<const eng::u8> blob, const Info& info,
					   eng::u16 index, eng::Span<eng::u8> dst) noexcept {
	eng::u32 sz = 0u;
	const eng::Span<const eng::u8> body = chunk_data(blob, info, index, sz);
	if (sz == 0u) {
		return -1;
	}
	return pcm_codec::decode(body, dst, static_cast<eng::u8>(info.codec));
}

} // namespace eng::audio::media
