#pragma once

/// \file media.hpp
/// **Interfaz de medios de audio**: punto único para reconocer un medio y despachar su lectura,
/// independientemente del **contenedor** (PCM crudo, AUZX o composición ACP1) y del **códec**
/// (`pcm_codec::Codec`). Sobre ella se apoyan el streaming (`pcm_stream.hpp`) y el juego.
///
/// Un "medio" es un blob en memoria (leído de ROM, de un AUI/asset o de disquete). `open()`
/// detecta el contenedor por su magic (o lo asume PCM si no lo hay) y rellena `Info`; a partir
/// de ahí se accede por **chunks** con `chunk_samples(i)`/`decode_chunk(i, dst)` o, para ACP1,
/// por track mediante `decode_track_window`/`mix_window`.
///
/// ```text
///   blob ──open──► Info {container, codec, rate, channels, bits, total_samples, num_chunks}
///                        │
///        chunk_size(i) ──┴──► decode_chunk(i, dst) ──► PCM 8-bit (s8)
/// ```
///
/// Contenedores: `Pcm` (PCM crudo mono 8-bit), `Auzx` (`auzx.hpp`) y `Acp1` (`acp1.hpp`). El códec es
/// el de `pcm_codec` (None/DeltaRle/FibDelta/ImaAdpcm/ZX0/DeltaZx0), ya sea el de la cabecera
/// AUZX o `None` para PCM crudo.

#include <eng/audio/auzx.hpp>
#include <eng/audio/acp1.hpp>
#include <eng/audio/pcm_codec.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::media {

/// Contenedor detectado.
enum class Container : eng::u8 {
	Pcm = 0u,  ///< PCM crudo mono 8-bit (sin cabecera): todo el blob es audio
	Auzx = 1u, ///< contenedor AUZX (cabecera + índice de chunks)
	Acp1 = 2u, ///< composición ACP1; se decodifica por track con `decode_track_window`
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
	acp1::Info acp1_info {}; ///< válido solo si `container == Acp1`
};

/// Reconoce el medio `blob` y rellena `out`. PCM crudo si no hay magic AUZX/ACP1; `false` si un
/// contenedor reconocido es corrupto o si el blob está vacío.
[[nodiscard]] inline bool open(eng::Span<const eng::u8> blob, Info& out) noexcept {
	if (blob.size() == 0u) {
		return false;
	}
	const bool auzx_magic = blob.size() >= 4u && blob[0] == 'A' && blob[1] == 'U' &&
				blob[2] == 'Z' && blob[3] == 'X';
	const bool acp1_magic = blob.size() >= 4u && blob[0] == 'A' && blob[1] == 'C' &&
				blob[2] == 'P' && blob[3] == '1';
	if (acp1_magic) {
		acp1::Info composition {};
		if (!acp1::parse(blob, composition)) return false;
		out.container = Container::Acp1;
		out.codec = pcm_codec::Codec::None;
		out.sample_rate = static_cast<eng::u16>(composition.sample_rate);
		out.channels = composition.track_count;
		out.bits = 8u;
		out.total_samples = composition.total_samples;
		out.chunk_samples = 0u;
		out.num_chunks = 0u;
		out.auzx_header = {};
		out.acp1_info = composition;
		return true;
	}
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
		out.acp1_info = {};
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
	out.acp1_info = {};
	return true;
}

/// Muestras DEScomprimidas del chunk `index` (el último puede ser más corto).
[[nodiscard]] inline eng::u32 chunk_samples(const Info& info, eng::u16 index) noexcept {
	if (info.container == Container::Pcm) {
		return info.total_samples;
	}
	if (info.container == Container::Acp1) return 0u;
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
	if (info.container == Container::Acp1) { size_out = 0u; return {}; }
	return auzx::chunk(blob, info.auzx_header, index, size_out);
}

/// Decodifica el chunk `index` a PCM 8-bit (s8) en `dst`. Devuelve las muestras escritas o `-1`.
[[nodiscard]] inline eng::s32 decode_chunk(eng::Span<const eng::u8> blob, const Info& info,
					   eng::u16 index, eng::Span<eng::u8> dst) noexcept {
	eng::u32 sz = 0u;
	if (info.container == Container::Acp1) return -1;
	const eng::Span<const eng::u8> body = chunk_data(blob, info, index, sz);
	if (sz == 0u) {
		return -1;
	}
	return pcm_codec::decode(body, dst, static_cast<eng::u8>(info.codec));
}

/// Decodifica una ventana de un track ACP1 v1 en PCM8 firmado.
/// `scratch` debe caber el chunk AUZX descomprimido más grande. Devuelve muestras escritas.
[[nodiscard]] inline eng::s32 decode_track_window(eng::Span<const eng::u8> blob, const Info& info,
	eng::u8 track_index, eng::u32 first_sample, eng::Span<eng::u8> dst,
	eng::Span<eng::u8> scratch) noexcept {
	if (info.container != Container::Acp1 || track_index >= info.acp1_info.track_count || dst.empty()) return -1;
	acp1::Track track {};
	if (!acp1::track(blob, info.acp1_info, track_index, track) || first_sample >= track.duration) return -1;
	acp1::Unit unit {};
	if (!acp1::unit(blob, info.acp1_info, static_cast<eng::u16>(track.unit_id), unit)) return -1;
	Info unit_info {};
	if (!open(unit.payload, unit_info) || unit_info.container != Container::Auzx || scratch.size() < unit_info.chunk_samples) return -1;
	const eng::u32 requested = static_cast<eng::u32>(dst.size() < track.duration - first_sample
		? dst.size() : track.duration - first_sample);
	const eng::u32 end_sample = first_sample + requested;
	eng::u32 written = 0u;
	for (eng::u16 chunk_index = 0u; chunk_index < unit_info.num_chunks; ++chunk_index) {
		const eng::u32 chunk_start = static_cast<eng::u32>(chunk_index) * unit_info.chunk_samples;
		const eng::u32 count = chunk_samples(unit_info, chunk_index);
		const eng::u32 chunk_end = chunk_start + count;
		if (chunk_start >= end_sample || chunk_end <= first_sample) continue;
		if (count > scratch.size()) return -1;
		const eng::s32 decoded = decode_chunk(unit.payload, unit_info, chunk_index, {scratch.data(), count});
		if (decoded != static_cast<eng::s32>(count)) return -1;
		const eng::u32 copy_start = chunk_start > first_sample ? chunk_start : first_sample;
		const eng::u32 copy_end = chunk_end < end_sample ? chunk_end : end_sample;
		for (eng::u32 sample = copy_start; sample < copy_end; ++sample) dst[written++] = scratch[sample - chunk_start];
	}
	return written == requested ? static_cast<eng::s32>(written) : -1;
}

/// Mezcla todas las pistas ACP1 en una ventana y satura al rango PCM8 firmado.
/// `scratch` cubre el chunk AUZX máximo y `accumulator` cubre las muestras de salida solicitadas.
[[nodiscard]] inline eng::s32 mix_window(eng::Span<const eng::u8> blob, const Info& info,
	eng::u32 first_sample, eng::Span<eng::u8> dst, eng::Span<eng::u8> scratch,
	eng::Span<eng::s16> accumulator) noexcept {
	if (info.container != Container::Acp1 || dst.empty() || accumulator.size() < dst.size() ||
		first_sample >= info.total_samples) return -1;
	const eng::u32 count = static_cast<eng::u32>(dst.size() < info.total_samples - first_sample
		? dst.size() : info.total_samples - first_sample);
	for (eng::u32 i = 0u; i < count; ++i) accumulator[i] = 0;
	for (eng::u8 track_index = 0u; track_index < info.acp1_info.track_count; ++track_index) {
		const eng::s32 decoded = decode_track_window(blob, info, track_index, first_sample,
			{dst.data(), count}, scratch);
		if (decoded != static_cast<eng::s32>(count)) return -1;
		for (eng::u32 i = 0u; i < count; ++i) {
			accumulator[i] = static_cast<eng::s16>(accumulator[i] + static_cast<eng::s8>(dst[i]));
		}
	}
	for (eng::u32 i = 0u; i < count; ++i) {
		eng::s16 sample = accumulator[i];
		if (sample > 127) sample = 127;
		if (sample < -128) sample = -128;
		dst[i] = static_cast<eng::u8>(static_cast<eng::s8>(sample));
	}
	return static_cast<eng::s32>(count);
}

} // namespace eng::audio::media
