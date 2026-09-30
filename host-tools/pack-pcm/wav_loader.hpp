#pragma once

/// \file wav_loader.hpp
/// Loader host-only de WAV PCM lineal a PCM8 mono con signo.
///
/// La herramienta de PC necesita aceptar WAV sin llevar ese parser al runtime Amiga. La cabecera
/// permanece independiente del engine y trabaja sobre `std::vector`, porque solo se usa durante la
/// preparación offline. El resultado sí cruza la frontera mediante `eng::Span` cuando se codifica.

#include <cstdio>
#include <cstring>
#include <vector>

#include <eng/core/types/types.hpp>

namespace pack_pcm {

/// Conjunto host-only de stems PCM8 firmados y frecuencia común de origen.
struct WavStems {
	/// Canales independientes en orden de intercalado de la fuente.
	std::vector<std::vector<eng::u8>> channels;
	/// Tasa detectada en el WAV o impuesta por el override.
	eng::u16 sample_rate = 0u;
};

/// Lee un entero little-endian de 16 bits desde un fichero WAV ya cargado.
[[nodiscard]] inline eng::u16 read_u16(const std::vector<eng::u8>& bytes,
	eng::usize offset) noexcept {
	return static_cast<eng::u16>(bytes[offset]) |
	       static_cast<eng::u16>(static_cast<eng::u16>(bytes[offset + 1u]) << 8u);
}

/// Lee un entero little-endian de 32 bits desde un fichero WAV ya cargado.
[[nodiscard]] inline eng::u32 read_u32(const std::vector<eng::u8>& bytes,
	eng::usize offset) noexcept {
	return static_cast<eng::u32>(bytes[offset]) |
	       (static_cast<eng::u32>(bytes[offset + 1u]) << 8u) |
	       (static_cast<eng::u32>(bytes[offset + 2u]) << 16u) |
	       (static_cast<eng::u32>(bytes[offset + 3u]) << 24u);
}

/// Carga el fichero completo para que las comprobaciones de offsets sean simples y deterministas.
[[nodiscard]] inline bool read_file(const char* path, std::vector<eng::u8>& bytes) {
	std::FILE* file = std::fopen(path, "rb");
	if (file == nullptr) return false;
	std::fseek(file, 0, SEEK_END);
	const long length = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	if (length <= 0) { std::fclose(file); return false; }
	bytes.resize(static_cast<eng::usize>(length));
	const bool ok = std::fread(bytes.data(), 1u, bytes.size(), file) == bytes.size();
	std::fclose(file);
	return ok;
}

/// Separa WAV PCM lineal de uno a ocho canales y 8/16 bits en stems PCM8 con signo.
/// `rate_override == 0` conserva la frecuencia WAV; el resultado devuelve la tasa efectiva.
[[nodiscard]] inline bool load_stems(const char* path, WavStems& stems, eng::u16 rate_override = 0u) {
	std::vector<eng::u8> input;
	if (!read_file(path, input) || input.size() < 12u ||
		std::memcmp(input.data(), "RIFF", 4u) != 0 ||
		std::memcmp(input.data() + 8u, "WAVE", 4u) != 0) return false;
	eng::usize fmt = 0u, fmt_size = 0u, data = 0u, data_size = 0u;
	for (eng::usize offset = 12u; offset + 8u <= input.size();) {
		const eng::u32 size = read_u32(input, offset + 4u);
		const eng::usize payload = offset + 8u;
		if (payload > input.size() || static_cast<eng::usize>(size) > input.size() - payload) return false;
		if (std::memcmp(input.data() + offset, "fmt ", 4u) == 0) { fmt = payload; fmt_size = size; }
		if (std::memcmp(input.data() + offset, "data", 4u) == 0) { data = payload; data_size = size; }
		const eng::usize advance = static_cast<eng::usize>(size) + (size & 1u);
		if (advance > input.size() - payload) return false;
		offset = payload + advance;
	}
	if (fmt == 0u || fmt_size < 16u || data == 0u || read_u16(input, fmt) != 1u) return false;
	const eng::u16 channels = read_u16(input, fmt + 2u);
	const eng::u32 wav_rate = read_u32(input, fmt + 4u);
	const eng::u16 align = read_u16(input, fmt + 12u);
	const eng::u16 bits = read_u16(input, fmt + 14u);
	if (channels == 0u || channels > 8u || (bits != 8u && bits != 16u) || wav_rate == 0u ||
		wav_rate > 65535u || align != static_cast<eng::u16>(channels * (bits / 8u)) ||
		data_size % align != 0u) return false;
	const eng::usize frames = data_size / align;
	stems.channels.assign(channels, std::vector<eng::u8>(frames));
	for (eng::usize frame = 0u; frame < frames; ++frame) {
		for (eng::u16 channel = 0u; channel < channels; ++channel) {
			const eng::usize sample = data + frame * align + channel * (bits / 8u);
			const eng::s32 signed_sample = bits == 8u ? static_cast<eng::s32>(input[sample]) - 128 :
				static_cast<eng::s32>(static_cast<eng::s16>(read_u16(input, sample))) >> 8u;
			stems.channels[channel][frame] = static_cast<eng::u8>(signed_sample);
		}
	}
	stems.sample_rate = rate_override == 0u ? static_cast<eng::u16>(wav_rate) : rate_override;
	return true;
}

/// Mezcla los canales preservados a mono PCM8 con signo.
[[nodiscard]] inline bool downmix(const WavStems& stems, std::vector<eng::u8>& pcm) {
	if (stems.channels.empty()) return false;
	const eng::usize frames = stems.channels[0].size();
	for (const auto& channel : stems.channels) if (channel.size() != frames) return false;
	pcm.resize(frames);
	for (eng::usize frame = 0u; frame < frames; ++frame) {
		eng::s32 sum = 0;
		for (const auto& channel : stems.channels) sum += static_cast<eng::s8>(channel[frame]);
		pcm[frame] = static_cast<eng::u8>(sum / static_cast<eng::s32>(stems.channels.size()));
	}
	return true;
}

/// Carga WAV como un único stem mono, promediando los canales preservados por `load_stems`.
[[nodiscard]] inline bool load(const char* path, std::vector<eng::u8>& pcm, eng::u16& rate,
	eng::u16 rate_override = 0u) {
	WavStems stems {};
	if (!load_stems(path, stems, rate_override) || !downmix(stems, pcm)) return false;
	rate = stems.sample_rate;
	return true;
}

} // namespace pack_pcm
